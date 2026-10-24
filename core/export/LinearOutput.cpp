#include "core/export/LinearOutput.h"
#include "core/image/Bayer.h"
#include "core/lens/LensCorrection.h"
#include "core/render/CreativeColor.h"
#include "core/render/CropGeometry.h"
#include "core/render/DevelopPipeline.h"
#include "core/render/LocalAdjustments.h"
#include "core/render/NoiseReduction.h"
#include "core/render/Presence.h"
#include "core/render/RetouchPass.h"
#include <algorithm>
#include <cmath>

namespace luma {
LinearOutput::LinearOutput(int w, int h) : width(w), height(h), rgba(pixelCount(w, h) * 4) {}
void LinearOutput::validate() const {
    if (rgba.size() != pixelCount(width, height) * 4)
        throw std::runtime_error("Invalid linear output buffer.");
    for (std::size_t i = 0; i < rgba.size(); ++i)
        if (!std::isfinite(rgba[i]) || (i % 4 == 3 && (rgba[i] < 0 || rgba[i] > 1)))
            throw std::runtime_error("Invalid linear output sample/coverage.");
}
LinearOutput renderLinearOutput(const DecodedRaw &decoded, const EditDocument &edits,
                                CancellationToken stop,
                                const std::function<void(double)> &progress) {
    edits.validate();
    decoded.image.validateCoverage();
    const LensCorrectionPass optics(decoded.image, edits, decoded.metadata, stop);
    const RetouchPass retouch(optics.image(), edits.retouch, stop);
    const auto &input = retouch.image();
    const CropGeometry geometry(input.width, input.height, decoded.metadata.orientation, edits);
    const double sourceAspect =
        decoded.metadata.width > 0 && decoded.metadata.height > 0
            ? static_cast<double>(decoded.metadata.width) / decoded.metadata.height
            : static_cast<double>(input.width) / input.height;
    const LocalAdjustmentPass local(edits.localMasks, sourceAspect);
    const DevelopPipeline pipeline(edits, decoded.calibration);
    const NoiseReductionPass noise(input, pipeline, edits, stop, 1, DetailDiagnostic::none,
                                   [&](double value) {
                                       if (progress)
                                           progress(value * 0.2);
                                   });
    const SharpeningPass sharpening(
        input, pipeline, edits, stop, 1, DetailDiagnostic::none,
        [&](double value) {
            if (progress)
                progress(0.2 + value * 0.2);
        },
        &noise);
    const auto detailPixel = [&](int sx, int sy) {
        const auto i = (static_cast<std::size_t>(sy) * input.width + sx) * 3;
        const auto developed =
            noise.active() ? noise.pixel({}, sx, sy)
                           : pipeline.pixel({input.rgb[i], input.rgb[i + 1], input.rgb[i + 2]});
        return sharpening.pixel(developed, sx, sy);
    };
    const PresencePass presence(input, edits, detailPixel, stop);
    LinearOutput result(geometry.width(), geometry.height());
    for (int y = 0; y < result.height; ++y) {
        checkCancellation(stop);
        for (int x = 0; x < result.width; ++x) {
            auto rgb = geometry.sample(x, y, [&](int sx, int sy) {
                auto color = local.pixel(presence.pixel(detailPixel(sx, sy), sx, sy),
                                         (sx + 0.5) / input.width, (sy + 0.5) / input.height);
                if (!input.alpha.empty())
                    for (auto &value : color)
                        value *= input.coverage(sx, sy);
                return color;
            });
            const float alpha = input.alpha.empty() ? (geometry.covered(x, y) ? 1.F : 0.F)
                                                    : geometry.sample(x, y, [&](int sx, int sy) {
                                                          const auto value = input.coverage(sx, sy);
                                                          return std::array{value, value, value};
                                                      })[0];
            if (!input.alpha.empty())
                for (auto &value : rgb)
                    value = alpha > 1e-8F ? value / alpha : 0;
            rgb = applyOutputEffects(rgb, edits, x, y, result.width, result.height);
            const auto target = (static_cast<std::size_t>(y) * result.width + x) * 4;
            for (std::size_t c = 0; c < 3; ++c)
                result.rgba[target + c] = rgb[c];
            result.rgba[target + 3] = alpha;
        }
        if (progress)
            progress(0.4 + 0.6 * (y + 1.0) / result.height);
    }
    return result;
}
namespace {
using Weights = std::vector<std::vector<std::pair<int, double>>>;
Weights resizeWeights(int source, int target) {
    Weights result(static_cast<std::size_t>(target));
    const double scale = static_cast<double>(source) / target;
    for (int i = 0; i < target; ++i) {
        auto &weights = result[static_cast<std::size_t>(i)];
        if (scale > 1) {
            const double first = i * scale, last = (i + 1) * scale;
            for (int sample = static_cast<int>(std::floor(first));
                 sample < std::min(source, static_cast<int>(std::ceil(last))); ++sample) {
                const double weight =
                    std::max(0.0, std::min(last, sample + 1.0) -
                                      std::max(first, static_cast<double>(sample))) /
                    scale;
                if (weight > 0)
                    weights.emplace_back(sample, weight);
            }
        } else {
            const double center = std::clamp((i + 0.5) * scale - 0.5, 0.0, source - 1.0);
            const int first = static_cast<int>(std::floor(center)),
                      last = std::min(first + 1, source - 1);
            const double fraction = center - first;
            weights.emplace_back(first, 1 - fraction);
            if (last != first)
                weights.emplace_back(last, fraction);
        }
    }
    return result;
}
} // namespace
LinearOutput resizeOutput(LinearOutput input, const ExportOptions &options,
                          CancellationToken stop) {
    input.validate();
    const auto [width, height] = options.dimensions(input.width, input.height);
    if (width == input.width && height == input.height)
        return input;
    const auto horizontal = resizeWeights(input.width, width),
               vertical = resizeWeights(input.height, height);
    LinearOutput temporary(width, input.height);
    for (int y = 0; y < input.height; ++y) {
        checkCancellation(stop);
        for (int x = 0; x < width; ++x) {
            const auto target = (static_cast<std::size_t>(y) * width + x) * 4;
            for (const auto &[sample, weight] : horizontal[static_cast<std::size_t>(x)]) {
                const auto source = (static_cast<std::size_t>(y) * input.width + sample) * 4;
                const double alpha = input.rgba[source + 3];
                for (std::size_t c = 0; c < 3; ++c)
                    temporary.rgba[target + c] +=
                        static_cast<float>(input.rgba[source + c] * alpha * weight);
                temporary.rgba[target + 3] += static_cast<float>(alpha * weight);
            }
        }
    }
    // Release the source before allocating the final resized surface.
    input.rgba.clear();
    input.rgba.shrink_to_fit();
    LinearOutput output(width, height);
    for (int y = 0; y < height; ++y) {
        checkCancellation(stop);
        for (int x = 0; x < width; ++x) {
            const auto target = (static_cast<std::size_t>(y) * width + x) * 4;
            for (const auto &[sample, weight] : vertical[static_cast<std::size_t>(y)]) {
                const auto source = (static_cast<std::size_t>(sample) * width + x) * 4;
                for (std::size_t c = 0; c < 4; ++c)
                    output.rgba[target + c] +=
                        static_cast<float>(temporary.rgba[source + c] * weight);
            }
            auto &alpha = output.rgba[target + 3];
            alpha = std::clamp(alpha, 0.0F, 1.0F);
            for (std::size_t c = 0; c < 3; ++c)
                output.rgba[target + c] = alpha > 1e-8F ? output.rgba[target + c] / alpha : 0;
        }
    }
    return output;
}
void sharpenOutput(LinearOutput &image, const ExportOptions &options, CancellationToken stop) {
    image.validate();
    options.validate();
    if (options.outputMedium == OutputMedium::off)
        return;
    const auto count = pixelCount(image.width, image.height);
    const double sigma = options.outputMedium == OutputMedium::screen  ? 0.55
                         : options.outputMedium == OutputMedium::matte ? 0.9
                                                                       : 0.7;
    const double base = options.outputMedium == OutputMedium::screen  ? 0.5
                        : options.outputMedium == OutputMedium::matte ? 0.9
                                                                      : 0.65;
    const double amount = base * (options.outputAmount == SharpenAmount::low    ? 0.5
                                  : options.outputAmount == SharpenAmount::high ? 1.5
                                                                                : 1);
    const int radius = static_cast<int>(std::ceil(3 * sigma));
    std::vector<double> weights(static_cast<std::size_t>(2 * radius + 1));
    double sum = 0;
    for (int d = -radius; d <= radius; ++d) {
        const double weight = std::exp(-d * d / (2 * sigma * sigma));
        weights[static_cast<std::size_t>(d + radius)] = weight;
        sum += weight;
    }
    for (auto &weight : weights)
        weight /= sum;
    std::vector<float> horizontal(count), coverage(count);
    for (int y = 0; y < image.height; ++y) {
        checkCancellation(stop);
        for (int x = 0; x < image.width; ++x) {
            const auto target = static_cast<std::size_t>(y) * image.width + x;
            for (int d = -radius; d <= radius; ++d) {
                const int sx = std::clamp(x + d, 0, image.width - 1);
                const auto source = (static_cast<std::size_t>(y) * image.width + sx) * 4;
                const double weight =
                    weights[static_cast<std::size_t>(d + radius)] * image.rgba[source + 3];
                horizontal[target] += static_cast<float>((image.rgba[source] * 0.2126 +
                                                          image.rgba[source + 1] * 0.7152 +
                                                          image.rgba[source + 2] * 0.0722) *
                                                         weight);
                coverage[target] += static_cast<float>(weight);
            }
        }
    }
    // Luminance/coverage are immutable while the destination colors change.
    for (int y = 0; y < image.height; ++y) {
        checkCancellation(stop);
        for (int x = 0; x < image.width; ++x) {
            const auto target = (static_cast<std::size_t>(y) * image.width + x) * 4;
            if (image.rgba[target + 3] == 0)
                continue;
            double blur = 0, alpha = 0;
            for (int d = -radius; d <= radius; ++d) {
                const auto source =
                    static_cast<std::size_t>(std::clamp(y + d, 0, image.height - 1)) * image.width +
                    x;
                const double weight = weights[static_cast<std::size_t>(d + radius)];
                blur += horizontal[source] * weight;
                alpha += coverage[source] * weight;
            }
            const double luminance = image.rgba[target] * 0.2126 + image.rgba[target + 1] * 0.7152 +
                                     image.rgba[target + 2] * 0.0722;
            const double delta = alpha > 1e-8 ? amount * (luminance - blur / alpha) : 0;
            for (std::size_t c = 0; c < 3; ++c)
                image.rgba[target + c] = static_cast<float>(
                    luminance > 1e-8 ? image.rgba[target + c] * (luminance + delta) / luminance
                                     : image.rgba[target + c] + delta);
        }
    }
}
} // namespace luma
