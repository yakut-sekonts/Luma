#include "core/render/Sharpening.h"
#include "core/image/Bayer.h"
#include "core/render/NoiseReduction.h"
#include <algorithm>
#include <cmath>

namespace luma {
namespace {
double luminance(const std::array<float, 3> &rgb) {
    for (auto value : rgb)
        if (!std::isfinite(value))
            throw std::runtime_error("Non-finite developed pixel in sharpening.");
    return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
}
std::vector<float> gaussian(const std::vector<float> &source, int width, int height, double sigma,
                            CancellationToken stop, const std::function<void(double)> &progress) {
    checkCancellation(stop);
    const int radius = static_cast<int>(std::ceil(3 * sigma));
    const int diameter = radius * 2 + 1;
    std::vector<double> kernel(static_cast<std::size_t>(diameter));
    double total = 0;
    for (int k = -radius; k <= radius; ++k) {
        const auto distance = static_cast<double>(k) / sigma;
        const auto weight = std::exp(-0.5 * distance * distance);
        kernel[static_cast<std::size_t>(k + radius)] = weight;
        total += weight;
    }
    for (auto &weight : kernel)
        weight /= total;
    std::vector<float> result(source.size());
    std::vector<float> rows(static_cast<std::size_t>(diameter) * width);
    std::vector<int> rowIDs(static_cast<std::size_t>(diameter), -1);
    for (int y = 0; y < height; ++y) {
        checkCancellation(stop);
        for (int k = -radius; k <= radius; ++k) {
            const int sy = std::clamp(y + k, 0, height - 1);
            const int slot = sy % diameter;
            if (rowIDs[static_cast<std::size_t>(slot)] == sy)
                continue;
            for (int x = 0; x < width; ++x) {
                double value = 0;
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int sx = std::clamp(x + dx, 0, width - 1);
                    value += source[static_cast<std::size_t>(sy) * width + sx] *
                             kernel[static_cast<std::size_t>(dx + radius)];
                }
                rows[static_cast<std::size_t>(slot) * width + x] = static_cast<float>(value);
            }
            rowIDs[static_cast<std::size_t>(slot)] = sy;
        }
        for (int x = 0; x < width; ++x) {
            double value = 0;
            for (int k = -radius; k <= radius; ++k) {
                const int sy = std::clamp(y + k, 0, height - 1);
                value += rows[static_cast<std::size_t>(sy % diameter) * width + x] *
                         kernel[static_cast<std::size_t>(k + radius)];
            }
            result[static_cast<std::size_t>(y) * width + x] = static_cast<float>(value);
        }
        if (progress && (y % std::max(1, height / 200) == 0 || y + 1 == height))
            progress(0.5 + 0.5 * static_cast<double>(y + 1) / height);
    }
    return result;
}
} // namespace
SharpeningPass::SharpeningPass(const FloatImage &input, const DevelopPipeline &pipeline,
                               const EditDocument &edits, CancellationToken stop, double scale,
                               DetailDiagnostic diagnostic,
                               const std::function<void(double)> &progress,
                               const NoiseReductionPass *noise,
                               std::span<const float> developedPixels)
    : width_(input.width), height_(input.height), amount_(edits.sharpenAmount / 100),
      detail_(edits.sharpenDetail / 100), masking_(edits.sharpenMasking / 100),
      diagnostic_(diagnostic) {
    edits.validate();
    input.validateCoverage();
    const auto count = pixelCount(width_, height_);
    if (!developedPixels.empty() && developedPixels.size() != input.rgb.size())
        throw std::runtime_error("Invalid accelerated sharpening input.");
    if (input.rgb.size() != count * 3 || !std::isfinite(scale) || scale < 1.0 / 20000 ||
        scale > 1 || diagnostic < DetailDiagnostic::none ||
        diagnostic > DetailDiagnostic::noiseColor)
        throw std::runtime_error("Invalid sharpening input, scale or diagnostic mode.");
    checkCancellation(stop);
    if (diagnostic_ == DetailDiagnostic::noiseLuminance ||
        diagnostic_ == DetailDiagnostic::noiseColor)
        return;
    if (!edits.enabled(Correction::sharpening) ||
        (amount_ == 0 && diagnostic_ == DetailDiagnostic::none))
        return;
    luminance_.resize(count);
    for (int y = 0; y < height_; ++y) {
        checkCancellation(stop);
        for (int x = 0; x < width_; ++x) {
            const auto index = static_cast<std::size_t>(y) * width_ + x;
            const auto offset = index * 3;
            const auto rgb = noise && noise->active() ? noise->pixel({}, x, y)
                             : !developedPixels.empty()
                                 ? std::array{developedPixels[offset], developedPixels[offset + 1],
                                              developedPixels[offset + 2]}
                                 : pipeline.pixel({input.rgb[offset], input.rgb[offset + 1],
                                                   input.rgb[offset + 2]});
            luminance_[index] = static_cast<float>(luminance(rgb));
            if (!std::isfinite(luminance_[index]))
                throw std::runtime_error("Non-finite luminance in sharpening.");
        }
        if (progress && (y % std::max(1, height_ / 200) == 0 || y + 1 == height_))
            progress(0.5 * static_cast<double>(y + 1) / height_);
    }
    if (input.alpha.empty()) {
        blurred_ =
            gaussian(luminance_, width_, height_, edits.sharpenRadius * scale, stop, progress);
    } else {
        std::vector<float> weighted(count);
        for (std::size_t i = 0; i < count; ++i)
            weighted[i] = luminance_[i] * input.alpha[i];
        blurred_ = gaussian(weighted, width_, height_, edits.sharpenRadius * scale, stop, progress);
        std::vector<float>().swap(weighted);
        const auto coverage =
            gaussian(input.alpha, width_, height_, edits.sharpenRadius * scale, stop, {});
        for (std::size_t i = 0; i < count; ++i)
            blurred_[i] = coverage[i] > 1e-8F ? blurred_[i] / coverage[i] : luminance_[i];
    }
    checkCancellation(stop);
}
double SharpeningPass::mask(int x, int y) const {
    if (masking_ == 0)
        return 1;
    const auto at = [&](int sx, int sy) {
        return static_cast<double>(blurred_[static_cast<std::size_t>(sy) * width_ + sx]);
    };
    const auto dx = (at(std::min(x + 1, width_ - 1), y) - at(std::max(x - 1, 0), y)) / 2;
    const auto dy = (at(x, std::min(y + 1, height_ - 1)) - at(x, std::max(y - 1, 0))) / 2;
    const auto gradient = std::hypot(dx, dy) / std::max(0.02, std::abs(at(x, y)));
    const auto low = 0.005 + 0.25 * masking_;
    const auto t = std::clamp((gradient - low) / 0.05, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
double SharpeningPass::residual(std::size_t index) const {
    const double difference = static_cast<double>(luminance_[index]) - blurred_[index];
    const auto threshold =
        (1 - detail_) * 0.02 * std::max(0.02, std::abs(static_cast<double>(luminance_[index])));
    return std::copysign(std::max(0.0, std::abs(difference) - threshold), difference);
}
std::array<float, 3> SharpeningPass::pixel(std::array<float, 3> developed, int x, int y) const {
    if (!active())
        return developed;
    if (x < 0 || x >= width_ || y < 0 || y >= height_)
        throw std::runtime_error("Sharpening pixel is outside the image.");
    const auto index = static_cast<std::size_t>(y) * width_ + x;
    const double delta = amount_ * residual(index) * mask(x, y);
    if (diagnostic_ != DetailDiagnostic::none) {
        double value = 0;
        switch (diagnostic_) {
        case DetailDiagnostic::amount:
            value = luminance_[index] + delta;
            break;
        case DetailDiagnostic::radius:
            value = 0.5 + 4 * (static_cast<double>(luminance_[index]) - blurred_[index]);
            break;
        case DetailDiagnostic::detail:
            value = 0.5 + 4 * residual(index);
            break;
        case DetailDiagnostic::masking:
            value = mask(x, y);
            break;
        case DetailDiagnostic::noiseLuminance:
        case DetailDiagnostic::noiseColor:
        case DetailDiagnostic::none:
            break;
        }
        const auto gray = static_cast<float>(std::clamp(value, 0.0, 1.0));
        return {gray, gray, gray};
    }
    for (auto &channel : developed) {
        channel = static_cast<float>(channel + delta);
        if (!std::isfinite(channel))
            throw std::runtime_error("Non-finite sharpened output.");
    }
    return developed;
}
} // namespace luma
