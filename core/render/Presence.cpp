#include "core/render/Presence.h"
#include "core/image/Bayer.h"
#include "core/render/AdvancedColor.h"
#include <algorithm>
#include <cmath>
namespace luma {
namespace {
std::vector<float> blur(const std::vector<float> &values, const FloatImage &image, int radius,
                        CancellationToken stop) {
    const int w = image.width, h = image.height;
    std::vector<float> horizontal(values.size()), coverage(values.size()), result(values.size());
    for (int y = 0; y < h; ++y) {
        checkCancellation(stop);
        double sum = 0, alpha = 0;
        for (int x = -radius; x < w; ++x) {
            const int entering = x + radius, leaving = x - radius - 1;
            if (entering < w) {
                const auto i = static_cast<std::size_t>(y) * w + entering;
                const double a = image.coverage(entering, y);
                sum += values[i] * a;
                alpha += a;
            }
            if (leaving >= 0) {
                const auto i = static_cast<std::size_t>(y) * w + leaving;
                const double a = image.coverage(leaving, y);
                sum -= values[i] * a;
                alpha -= a;
            }
            if (x >= 0) {
                const auto i = static_cast<std::size_t>(y) * w + x;
                horizontal[i] = static_cast<float>(sum);
                coverage[i] = static_cast<float>(alpha);
            }
        }
    }
    // Traverse contiguous rows while retaining each column's original summation
    // order. The former column walk repeatedly evicted neighboring cache lines.
    std::vector<double> sums(static_cast<std::size_t>(w)), alphas(static_cast<std::size_t>(w));
    for (int y = -radius; y < h; ++y) {
        checkCancellation(stop);
        const int entering = y + radius, leaving = y - radius - 1;
        for (int x = 0; x < w; ++x) {
            auto &sum = sums[static_cast<std::size_t>(x)];
            auto &alpha = alphas[static_cast<std::size_t>(x)];
            if (entering < h) {
                const auto i = static_cast<std::size_t>(entering) * w + x;
                sum += horizontal[i];
                alpha += coverage[i];
            }
            if (leaving >= 0) {
                const auto i = static_cast<std::size_t>(leaving) * w + x;
                sum -= horizontal[i];
                alpha -= coverage[i];
            }
            if (y >= 0) {
                const auto i = static_cast<std::size_t>(y) * w + x;
                result[i] = alpha > 1e-8 ? static_cast<float>(sum / alpha) : values[i];
            }
        }
    }
    return result;
}
} // namespace
PresencePass::PresencePass(const FloatImage &image, const EditDocument &e,
                           const std::function<std::array<float, 3>(int, int)> &pixel,
                           CancellationToken stop, double scale)
    : width_(image.width) {
    if (!e.enabled(Correction::presence))
        return;
    texture_ = e.creative.values[0] / 100;
    clarity_ = e.creative.values[1] / 100;
    dehaze_ = e.creative.values[2] / 100;
    active_ = texture_ != 0 || clarity_ != 0 || dehaze_ != 0;
    if (texture_ == 0 && clarity_ == 0)
        return;
    std::vector<float> luminance(pixelCount(image.width, image.height));
    for (int y = 0; y < image.height; ++y) {
        checkCancellation(stop);
        for (int x = 0; x < image.width; ++x) {
            const auto c = pixel(x, y);
            luminance[static_cast<std::size_t>(y) * image.width + x] = static_cast<float>(
                std::log2(std::max(1e-6, .2126 * c[0] + .7152 * c[1] + .0722 * c[2])));
        }
    }
    if (texture_ != 0)
        fine_ = blur(luminance, image, std::max(1, static_cast<int>(std::lround(3 * scale))), stop);
    if (clarity_ != 0)
        coarse_ =
            blur(luminance, image, std::max(1, static_cast<int>(std::lround(24 * scale))), stop);
}
std::array<float, 3> PresencePass::pixel(std::array<float, 3> color, int x, int y) const {
    if (!active_)
        return color;
    const double y0 = .2126 * color[0] + .7152 * color[1] + .0722 * color[2];
    if (y0 > 1e-6) {
        const auto i = static_cast<std::size_t>(y) * width_ + x;
        const double l = std::log2(y0);
        double delta = 0;
        if (!fine_.empty())
            delta += texture_ * .5 * std::clamp(l - fine_[i], -2.0, 2.0);
        if (!coarse_.empty())
            delta += clarity_ * .7 * std::clamp(l - coarse_[i], -2.0, 2.0);
        const double gain = std::exp2(delta);
        for (auto &c : color)
            c = static_cast<float>(c * gain);
    }
    if (dehaze_ > 0) {
        const double dark = std::clamp(
            static_cast<double>(std::min({color[0], color[1], color[2]})) / .85, 0.0, .95);
        const double transmission = std::max(.25, 1 - dehaze_ * .8 * dark);
        for (auto &c : color)
            c = static_cast<float>((c - .85) / transmission + .85);
    } else if (dehaze_ < 0)
        for (auto &c : color)
            c = static_cast<float>(std::lerp(static_cast<double>(c), .85, -dehaze_ * .5));
    return color;
}
} // namespace luma
