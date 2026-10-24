#include "core/merge/HdrMerge.h"
#include "core/image/Bayer.h"
#include <algorithm>
#include <cmath>

namespace luma {
namespace {
struct Pixel {
    std::array<double, 3> rgb{};
    double alpha = 0;
};
Pixel sample(const FloatImage &image, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x > 1 || y > 1)
        return {};
    x = std::clamp(x * image.width - .5, 0.0, image.width - 1.0);
    y = std::clamp(y * image.height - .5, 0.0, image.height - 1.0);
    const int ix = static_cast<int>(x), iy = static_cast<int>(y);
    Pixel result;
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
            const int sx = std::min(ix + dx, image.width - 1),
                      sy = std::min(iy + dy, image.height - 1);
            const double weight =
                (dx ? x - ix : 1 - x + ix) * (dy ? y - iy : 1 - y + iy) * image.coverage(sx, sy);
            result.alpha += weight;
            const auto i = (static_cast<std::size_t>(sy) * image.width + sx) * 3;
            for (std::size_t c = 0; c < 3; ++c)
                result.rgb[c] += image.rgb[i + c] * weight;
        }
    if (result.alpha > 1e-8)
        for (auto &v : result.rgb)
            v /= result.alpha;
    return result;
}
double confidence(const std::array<double, 3> &rgb) {
    for (double v : rgb)
        if (!std::isfinite(v))
            throw std::runtime_error("Non-finite HDR sample.");
    const double maximum = *std::max_element(rgb.begin(), rgb.end());
    const double luminance = std::max(0.0, .2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2]);
    // Downweight both read noise and clipped highlights. A small nonzero
    // fallback keeps pixels usable when all exposures clip.
    const double dark = std::clamp(luminance / .08, 0.0, 1.0);
    const double bright = std::clamp((1.0 - maximum) / .15, 0.0, 1.0);
    return std::max(1e-5, dark * bright);
}
void validate(const MergeFrame &frame) {
    if (!frame.image || !std::isfinite(frame.exposureEV) || std::abs(frame.exposureEV) > 50)
        throw std::runtime_error("Invalid HDR frame/exposure.");
    frame.image->validateCoverage();
    frame.toReference.validate();
}
} // namespace
FloatImage mergeHDR(const MergeFrame &reference, std::size_t frameCount,
                    const std::function<MergeFrame(std::size_t)> &load, const HdrOptions &options,
                    CancellationToken stop, const std::function<void(double)> &progress) {
    validate(reference);
    if (!load || frameCount < 1 || frameCount > 8 || !std::isfinite(options.deghost) ||
        options.deghost < 0 || options.deghost > 1)
        throw std::runtime_error("HDR requires 2–9 frames and valid deghosting strength.");
    const auto &base = *reference.image;
    FloatImage output(base.width, base.height);
    output.alpha = base.alpha;
    std::vector<float> weights(pixelCount(base.width, base.height));
    for (int y = 0; y < base.height; ++y) {
        checkCancellation(stop);
        for (int x = 0; x < base.width; ++x) {
            const auto i = static_cast<std::size_t>(y) * base.width + x;
            const double w =
                confidence({base.rgb[i * 3], base.rgb[i * 3 + 1], base.rgb[i * 3 + 2]}) *
                base.coverage(x, y);
            weights[i] = static_cast<float>(w);
            for (std::size_t c = 0; c < 3; ++c)
                output.rgb[i * 3 + c] = static_cast<float>(base.rgb[i * 3 + c] * w);
        }
    }
    for (std::size_t f = 0; f < frameCount; ++f) {
        checkCancellation(stop);
        const auto frame = load(f);
        validate(frame);
        const double differenceEV = reference.exposureEV - frame.exposureEV;
        if (std::abs(differenceEV) > 16)
            throw std::runtime_error("HDR exposure range exceeds 16 stops.");
        const double gain = std::exp2(differenceEV);
        const auto lookup = options.alignFrames ? frame.toReference.inverse() : Homography{};
        for (int y = 0; y < base.height; ++y) {
            checkCancellation(stop);
            for (int x = 0; x < base.width; ++x) {
                const auto p = lookup.point((x + .5) / base.width, (y + .5) / base.height);
                const auto candidate = sample(*frame.image, p[0], p[1]);
                if (candidate.alpha < .99 || base.coverage(x, y) < .99F)
                    continue;
                const auto i = static_cast<std::size_t>(y) * base.width + x;
                const std::array<double, 3> original{base.rgb[i * 3], base.rgb[i * 3 + 1],
                                                     base.rgb[i * 3 + 2]};
                double weight = confidence(candidate.rgb);
                if (options.deghost > 0 && confidence(original) > .02 && weight > .02) {
                    double difference = 0, energy = .02;
                    for (std::size_t c = 0; c < 3; ++c) {
                        difference += std::abs(candidate.rgb[c] * gain - original[c]);
                        energy += std::abs(candidate.rgb[c] * gain) + std::abs(original[c]);
                    }
                    const double threshold = std::lerp(.45, .08, options.deghost);
                    const double motion =
                        std::clamp((difference / energy - threshold) / threshold, 0.0, 1.0);
                    weight *= 1 - motion * motion * (3 - 2 * motion);
                }
                weights[i] += static_cast<float>(weight);
                for (std::size_t c = 0; c < 3; ++c)
                    output.rgb[i * 3 + c] += static_cast<float>(candidate.rgb[c] * gain * weight);
            }
            if (progress)
                progress((f + static_cast<double>(y + 1) / base.height) / frameCount);
        }
    }
    for (std::size_t i = 0; i < weights.size(); ++i) {
        if ((i & 65535) == 0)
            checkCancellation(stop);
        for (std::size_t c = 0; c < 3; ++c)
            output.rgb[i * 3 + c] = weights[i] > 0 ? output.rgb[i * 3 + c] / weights[i] : 0;
    }
    return output;
}
} // namespace luma
