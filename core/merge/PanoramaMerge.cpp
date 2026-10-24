#include "core/merge/PanoramaMerge.h"
#include "core/image/Bayer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace luma {
namespace {
struct Projection {
    int width, height;
    PanoramaProjection type;
    double focal;
    Projection(int w, int h, const PanoramaOptions &options)
        : width(w), height(h), type(options.projection),
          focal(w / (2 * std::tan(options.fieldOfView * std::numbers::pi / 360))) {
        pixelCount(w, h);
        if (options.projection < PanoramaProjection::perspective ||
            options.projection > PanoramaProjection::spherical ||
            !std::isfinite(options.fieldOfView) || options.fieldOfView < 10 ||
            options.fieldOfView > 140)
            throw std::runtime_error("Invalid panorama projection or field of view.");
    }
    std::array<double, 2> project(double u, double v) const {
        if (type == PanoramaProjection::perspective)
            return {u * width, v * height};
        const double x = (u - .5) * width / focal, y = (v - .5) * height / focal;
        const double vertical = y / std::sqrt(1 + x * x);
        return {focal * std::atan(x) + width * .5,
                focal * (type == PanoramaProjection::spherical ? std::atan(vertical) : vertical) +
                    height * .5};
    }
    std::array<double, 2> unproject(double x, double y) const {
        if (type == PanoramaProjection::perspective)
            return {x / width, y / height};
        const double angle = (x - width * .5) / focal;
        if (std::abs(angle) >= 1.5)
            return {-1e9, -1e9};
        const double px = std::tan(angle), vertical = (y - height * .5) / focal;
        const double py = (type == PanoramaProjection::spherical ? std::tan(vertical) : vertical) *
                          std::sqrt(1 + px * px);
        return {px * focal / width + .5, py * focal / height + .5};
    }
};
struct Bounds {
    double left = std::numeric_limits<double>::infinity(), top = left;
    double right = -left, bottom = -left;
    void extend(std::array<double, 2> p) {
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || std::abs(p[0]) > 1e6 ||
            std::abs(p[1]) > 1e6)
            throw std::runtime_error("Panorama alignment exceeds its coordinate budget.");
        left = std::min(left, p[0]);
        right = std::max(right, p[0]);
        top = std::min(top, p[1]);
        bottom = std::max(bottom, p[1]);
    }
};
Bounds bounds(const Homography &placement, const Projection &projection) {
    placement.validate();
    Bounds result;
    const double denominator = placement.homogeneous(0, 0)[2];
    for (int i = 0; i <= 64; ++i) {
        const double t = i / 64.0;
        for (const auto &p :
             {std::array{t, 0.}, std::array{t, 1.}, std::array{0., t}, std::array{1., t}}) {
            if (denominator * placement.homogeneous(p[0], p[1])[2] <= 0)
                throw std::runtime_error(
                    "Panorama crosses a projection horizon. Use a smaller sequence.");
            const auto mapped = placement.point(p[0], p[1]);
            result.extend(projection.project(mapped[0], mapped[1]));
        }
    }
    return result;
}
struct Rectangle {
    int left = 0, top = 0, width = 0, height = 0;
};
Rectangle coveredRectangle(const std::vector<float> &coverage, int width, int height,
                           CancellationToken stop) {
    std::vector<int> heights(static_cast<std::size_t>(width));
    std::vector<int> stack;
    Rectangle best;
    for (int y = 0; y < height; ++y) {
        checkCancellation(stop);
        for (int x = 0; x < width; ++x)
            heights[static_cast<std::size_t>(x)] =
                coverage[static_cast<std::size_t>(y) * width + x] > 1e-8F
                    ? heights[static_cast<std::size_t>(x)] + 1
                    : 0;
        stack.clear();
        for (int x = 0; x <= width; ++x) {
            const int current = x == width ? 0 : heights[static_cast<std::size_t>(x)];
            while (!stack.empty() && heights[static_cast<std::size_t>(stack.back())] > current) {
                const int h = heights[static_cast<std::size_t>(stack.back())];
                stack.pop_back();
                const int left = stack.empty() ? 0 : stack.back() + 1, w = x - left;
                if (static_cast<std::int64_t>(w) * h >
                    static_cast<std::int64_t>(best.width) * best.height)
                    best = {left, y - h + 1, w, h};
            }
            if (x < width)
                stack.push_back(x);
        }
    }
    return best;
}
FloatImage warpBoundaries(const FloatImage &sum, const std::vector<float> &weights,
                          CancellationToken stop) {
    struct Span {
        int first = -1, last = -1;
    };
    std::vector<Span> spans(static_cast<std::size_t>(sum.height));
    int widest = 0;
    for (int y = 0; y < sum.height; ++y) {
        checkCancellation(stop);
        auto &span = spans[static_cast<std::size_t>(y)];
        for (int x = 0; x < sum.width; ++x) {
            if (weights[static_cast<std::size_t>(y) * sum.width + x] <= 1e-8F)
                continue;
            if (span.first < 0)
                span.first = x;
            span.last = x;
        }
        widest = std::max(widest, span.last < 0 ? 0 : span.last - span.first + 1);
    }
    // Restrict extrapolation: no row may be stretched by more than 2×. Rows
    // with sparse coverage at the top/bottom still need a small crop.
    const int minimum = std::max(22, widest / 2);
    int first = 0, last = sum.height - 1;
    const auto usable = [&](int y) {
        const auto &span = spans[static_cast<std::size_t>(y)];
        return span.first >= 0 && span.last - span.first + 1 >= minimum;
    };
    while (first <= last && !usable(first))
        ++first;
    while (last >= first && !usable(last))
        --last;
    if (widest < 22 || last - first + 1 < 22)
        throw std::runtime_error(
            "Not enough continuous coverage for boundary warp. Use auto crop.");
    FloatImage result(widest, last - first + 1);
    for (int y = first; y <= last; ++y) {
        checkCancellation(stop);
        if (!usable(y))
            throw std::runtime_error(
                "Boundary warp cannot bridge a gap between frames. Use auto crop.");
        const auto &span = spans[static_cast<std::size_t>(y)];
        for (int x = span.first; x <= span.last; ++x)
            if (weights[static_cast<std::size_t>(y) * sum.width + x] <= 1e-8F)
                throw std::runtime_error(
                    "Boundary warp cannot fill interior holes. Use auto crop.");
        for (int x = 0; x < widest; ++x) {
            const double position =
                span.first + x * static_cast<double>(span.last - span.first) / (widest - 1);
            const int left = static_cast<int>(position), right = std::min(left + 1, span.last);
            const auto a = static_cast<std::size_t>(y) * sum.width + left,
                       b = static_cast<std::size_t>(y) * sum.width + right,
                       destination = (static_cast<std::size_t>(y - first) * widest + x) * 3;
            const double fraction = position - left;
            for (std::size_t c = 0; c < 3; ++c)
                result.rgb[destination + c] =
                    static_cast<float>((1 - fraction) * sum.rgb[a * 3 + c] / weights[a] +
                                       fraction * sum.rgb[b * 3 + c] / weights[b]);
        }
    }
    return result;
}
} // namespace
FloatImage mergePanorama(int referenceWidth, int referenceHeight,
                         const std::vector<Homography> &placements,
                         const std::function<MergeFrame(std::size_t)> &load,
                         const PanoramaOptions &options, CancellationToken stop,
                         const std::function<void(double)> &progress) {
    if (placements.size() < 2 || placements.size() > 24 || !load)
        throw std::runtime_error("Panorama requires 2–24 overlapping frames.");
    const Projection projection(referenceWidth, referenceHeight, options);
    Bounds canvas;
    std::vector<Bounds> regions;
    for (const auto &placement : placements) {
        const auto region = bounds(placement, projection);
        regions.push_back(region);
        canvas.extend({region.left, region.top});
        canvas.extend({region.right, region.bottom});
    }
    const double originX = std::floor(canvas.left), originY = std::floor(canvas.top);
    const int width = static_cast<int>(std::ceil(canvas.right) - originX),
              height = static_cast<int>(std::ceil(canvas.bottom) - originY);
    FloatImage sum(width, height);
    std::vector<float> weights(pixelCount(width, height));
    for (std::size_t frameIndex = 0; frameIndex < placements.size(); ++frameIndex) {
        checkCancellation(stop);
        const auto frame = load(frameIndex);
        if (!frame.image || !std::isfinite(frame.exposureEV) || std::abs(frame.exposureEV) > 4)
            throw std::runtime_error("Invalid panorama input or exposure compensation.");
        const auto &image = *frame.image;
        image.validateCoverage();
        const auto inverse = placements[frameIndex].inverse();
        const double gain = std::exp2(frame.exposureEV);
        const auto &region = regions[frameIndex];
        const int left = std::max(0, static_cast<int>(std::floor(region.left - originX))),
                  right = std::min(width, static_cast<int>(std::ceil(region.right - originX))),
                  top = std::max(0, static_cast<int>(std::floor(region.top - originY))),
                  bottom = std::min(height, static_cast<int>(std::ceil(region.bottom - originY)));
        for (int y = top; y < bottom; ++y) {
            checkCancellation(stop);
            for (int x = left; x < right; ++x) {
                const auto flat = projection.unproject(originX + x + .5, originY + y + .5);
                const auto uv = inverse.point(flat[0], flat[1]);
                if (uv[0] < 0 || uv[0] > 1 || uv[1] < 0 || uv[1] > 1)
                    continue;
                const double sx = std::clamp(uv[0] * image.width - .5, 0.0, image.width - 1.0),
                             sy = std::clamp(uv[1] * image.height - .5, 0.0, image.height - 1.0);
                const int ix = static_cast<int>(sx), iy = static_cast<int>(sy);
                std::array<double, 3> color{};
                double alpha = 0;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const int px = std::min(ix + dx, image.width - 1),
                                  py = std::min(iy + dy, image.height - 1);
                        const double w = (dx ? sx - ix : 1 - sx + ix) *
                                         (dy ? sy - iy : 1 - sy + iy) * image.coverage(px, py);
                        alpha += w;
                        const auto offset = (static_cast<std::size_t>(py) * image.width + px) * 3;
                        for (std::size_t c = 0; c < 3; ++c)
                            color[c] += image.rgb[offset + c] * w;
                    }
                if (alpha < .999)
                    continue;
                const double edge = std::min({uv[0], 1 - uv[0], uv[1], 1 - uv[1]});
                const double w = std::max(1e-4, std::clamp(edge / .08, 0.0, 1.0));
                const auto i = static_cast<std::size_t>(y) * width + x;
                weights[i] += static_cast<float>(w);
                for (std::size_t c = 0; c < 3; ++c) {
                    const auto value = color[c] * gain / alpha;
                    if (!std::isfinite(value))
                        throw std::runtime_error("Non-finite panorama input.");
                    sum.rgb[i * 3 + c] += static_cast<float>(value * w);
                }
            }
            if (progress)
                progress(.9 * (frameIndex + static_cast<double>(y - top + 1) / (bottom - top)) /
                         placements.size());
        }
    }
    if (options.boundaryWarp) {
        auto result = warpBoundaries(sum, weights, stop);
        if (progress)
            progress(1);
        return result;
    }
    const auto crop = coveredRectangle(weights, width, height, stop);
    if (crop.width < 22 || crop.height < 22)
        throw std::runtime_error("Panorama has no sufficiently large covered rectangle.");
    FloatImage result(crop.width, crop.height);
    for (int y = 0; y < crop.height; ++y) {
        checkCancellation(stop);
        for (int x = 0; x < crop.width; ++x) {
            const auto source = static_cast<std::size_t>(y + crop.top) * width + x + crop.left,
                       destination = static_cast<std::size_t>(y) * crop.width + x;
            for (std::size_t c = 0; c < 3; ++c)
                result.rgb[destination * 3 + c] = sum.rgb[source * 3 + c] / weights[source];
        }
    }
    if (progress)
        progress(1);
    return result;
}
} // namespace luma
