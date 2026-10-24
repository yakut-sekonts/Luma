#include "core/render/NoiseReduction.h"
#include "core/image/Bayer.h"
#include <algorithm>
#include <cmath>

namespace luma {
namespace {
// A separable joint bilateral approximation: range weights always use the
// original guide in both directions. Chroma channels share those weights.
std::vector<float> filter(const std::vector<float> &guide, const std::vector<float> &alpha,
                          int width, int height, int component, double sigma, double range,
                          CancellationToken stop, const std::function<void(double)> &progress) {
    checkCancellation(stop);
    const int radius = static_cast<int>(std::ceil(3 * sigma));
    const int diameter = 2 * radius + 1;
    std::vector<double> spatial(static_cast<std::size_t>(diameter));
    for (int k = -radius; k <= radius; ++k)
        spatial[static_cast<std::size_t>(k + radius)] = std::exp(-0.5 * (k / sigma) * (k / sigma));
    std::vector<float> output(pixelCount(width, height));
    std::vector<float> rows(static_cast<std::size_t>(diameter) * width);
    std::vector<int> ids(static_cast<std::size_t>(diameter), -1);
    const auto weight = [&](std::size_t center, std::size_t neighbor, int k) {
        const double normalization =
            std::max(0.1, std::abs(static_cast<double>(guide[center * 3])));
        const double dy =
            (static_cast<double>(guide[neighbor * 3]) - guide[center * 3]) / normalization;
        double distance = dy * dy / (range * range);
        if (component != 0) {
            // Preserve luminance edges independently of Color Detail.
            distance = dy * dy / (0.08 * 0.08);
            for (std::size_t c = 1; c < 3; ++c) {
                const double delta =
                    (static_cast<double>(guide[neighbor * 3 + c]) - guide[center * 3 + c]) /
                    normalization;
                distance += delta * delta / (range * range);
            }
        }
        const double result =
            spatial[static_cast<std::size_t>(k + radius)] * std::exp(-0.5 * distance);
        return alpha.empty() ? result : result * alpha[neighbor];
    };
    for (int y = 0; y < height; ++y) {
        checkCancellation(stop);
        for (int k = -radius; k <= radius; ++k) {
            const int sy = std::clamp(y + k, 0, height - 1);
            const int slot = sy % diameter;
            if (ids[static_cast<std::size_t>(slot)] == sy)
                continue;
            for (int x = 0; x < width; ++x) {
                const auto center = static_cast<std::size_t>(sy) * width + x;
                double sum = 0, total = 0;
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int sx = std::clamp(x + dx, 0, width - 1);
                    const auto neighbor = static_cast<std::size_t>(sy) * width + sx;
                    const auto w = weight(center, neighbor, dx);
                    sum += w * guide[neighbor * 3 + static_cast<std::size_t>(component)];
                    total += w;
                }
                rows[static_cast<std::size_t>(slot) * width + x] =
                    total > 1e-12 ? static_cast<float>(sum / total)
                                  : guide[center * 3 + static_cast<std::size_t>(component)];
            }
            ids[static_cast<std::size_t>(slot)] = sy;
        }
        for (int x = 0; x < width; ++x) {
            const auto center = static_cast<std::size_t>(y) * width + x;
            double sum = 0, total = 0;
            for (int k = -radius; k <= radius; ++k) {
                const int sy = std::clamp(y + k, 0, height - 1);
                const auto neighbor = static_cast<std::size_t>(sy) * width + x;
                const auto w = weight(center, neighbor, k);
                sum += w * rows[static_cast<std::size_t>(sy % diameter) * width + x];
                total += w;
            }
            output[center] = total > 1e-12
                                 ? static_cast<float>(sum / total)
                                 : guide[center * 3 + static_cast<std::size_t>(component)];
        }
        if (progress && (y % std::max(1, height / 200) == 0 || y + 1 == height))
            progress(static_cast<double>(y + 1) / height);
    }
    return output;
}
} // namespace
NoiseReductionPass::NoiseReductionPass(const FloatImage &input, const DevelopPipeline &pipeline,
                                       const EditDocument &edits, CancellationToken stop,
                                       double scale, DetailDiagnostic diagnostic,
                                       const std::function<void(double)> &progress,
                                       std::span<const float> developedPixels)
    : width_(input.width), height_(input.height), diagnostic_(diagnostic) {
    edits.validate();
    input.validateCoverage();
    const auto count = pixelCount(width_, height_);
    if (!developedPixels.empty() && developedPixels.size() != input.rgb.size())
        throw std::runtime_error("Invalid accelerated noise reduction input.");
    if (input.rgb.size() != count * 3 || !std::isfinite(scale) || scale < 1.0 / 20000 || scale > 1)
        throw std::runtime_error("Invalid noise reduction image or scale.");
    checkCancellation(stop);
    const double luma = edits.enabled(Correction::luminanceNoise) ? edits.noiseLuminance / 100 : 0;
    const double color = edits.enabled(Correction::colorNoise) ? edits.noiseColor / 100 : 0;
    const bool diagnosticActive = diagnostic == DetailDiagnostic::noiseLuminance ||
                                  diagnostic == DetailDiagnostic::noiseColor;
    if (luma == 0 && color == 0 && !diagnosticActive)
        return;
    components_.resize(count * 3);
    const auto report = [&](double value) {
        if (progress)
            progress(value);
    };
    for (int y = 0; y < height_; ++y) {
        checkCancellation(stop);
        for (int x = 0; x < width_; ++x) {
            const auto i = (static_cast<std::size_t>(y) * width_ + x) * 3;
            const auto rgb =
                developedPixels.empty()
                    ? pipeline.pixel({input.rgb[i], input.rgb[i + 1], input.rgb[i + 2]})
                    : std::array{developedPixels[i], developedPixels[i + 1],
                                 developedPixels[i + 2]};
            for (auto value : rgb)
                if (!std::isfinite(value))
                    throw std::runtime_error("Non-finite noise reduction pixel.");
            const double Y = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
            components_[i] = static_cast<float>(Y);
            components_[i + 1] = static_cast<float>(rgb[0] - Y);
            components_[i + 2] = static_cast<float>(rgb[2] - Y);
            for (std::size_t c = 0; c < 3; ++c)
                if (!std::isfinite(components_[i + c]))
                    throw std::runtime_error("Noise components exceed Float32.");
        }
        if (y % std::max(1, height_ / 200) == 0 || y + 1 == height_)
            report(0.1 * static_cast<double>(y + 1) / height_);
    }
    if (color > 0) {
        const auto sigma = (0.5 + 2.5 * edits.noiseColorSmoothness / 100) * scale;
        const auto range = 0.02 + 0.6 * (1 - edits.noiseColorDetail / 100);
        auto u = filter(components_, input.alpha, width_, height_, 1, sigma, range, stop,
                        [&](double v) { report(0.1 + 0.3 * v); });
        auto v = filter(components_, input.alpha, width_, height_, 2, sigma, range, stop,
                        [&](double value) { report(0.4 + 0.3 * value); });
        for (int y = 0; y < height_; ++y) {
            checkCancellation(stop);
            for (int x = 0; x < width_; ++x) {
                const auto i = static_cast<std::size_t>(y) * width_ + x;
                components_[i * 3 + 1] = static_cast<float>(
                    components_[i * 3 + 1] + color * (u[i] - components_[i * 3 + 1]));
                components_[i * 3 + 2] = static_cast<float>(
                    components_[i * 3 + 2] + color * (v[i] - components_[i * 3 + 2]));
            }
        }
    }
    if (luma > 0) {
        const auto range = 0.015 + 0.25 * (1 - edits.noiseLuminanceDetail / 100);
        auto Y = filter(components_, input.alpha, width_, height_, 0, 1.5 * scale, range, stop,
                        [&](double value) { report(0.7 + 0.25 * value); });
        for (int y = 0; y < height_; ++y) {
            checkCancellation(stop);
            for (int x = 0; x < width_; ++x) {
                const auto i = static_cast<std::size_t>(y) * width_ + x;
                const double center = components_[i * 3];
                const double residual = center - Y[i];
                const auto retain =
                    edits.noiseLuminanceContrast / 100 *
                    std::clamp(std::abs(residual) / (range * std::max(0.1, std::abs(center)) * 0.5),
                               0.0, 1.0);
                components_[i * 3] = static_cast<float>(center - luma * (1 - retain) * residual);
            }
        }
    }
    report(1);
    checkCancellation(stop);
}
std::array<float, 3> NoiseReductionPass::pixel(std::array<float, 3> developed, int x, int y) const {
    if (!active())
        return developed;
    if (x < 0 || x >= width_ || y < 0 || y >= height_)
        throw std::runtime_error("Noise pixel outside image.");
    const auto i = (static_cast<std::size_t>(y) * width_ + x) * 3;
    double Y = components_[i], u = components_[i + 1], v = components_[i + 2];
    if (diagnostic_ == DetailDiagnostic::noiseLuminance) {
        u = 0;
        v = 0;
    }
    if (diagnostic_ == DetailDiagnostic::noiseColor)
        Y = 0.5;
    std::array<float, 3> result{static_cast<float>(Y + u),
                                static_cast<float>(Y - (0.2126 * u + 0.0722 * v) / 0.7152),
                                static_cast<float>(Y + v)};
    for (auto value : result)
        if (!std::isfinite(value))
            throw std::runtime_error("Non-finite noise output.");
    return result;
}
} // namespace luma
