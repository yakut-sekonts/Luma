#include "core/render/RetouchPass.h"
#include "core/image/Bayer.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace luma {
namespace {
struct Shape {
    std::vector<std::array<double, 2>> points;
    double radius = 0, feather = 0;
    int left = 0, top = 0, right = 0, bottom = 0;
    Shape(const RetouchStroke &stroke, int w, int h)
        : radius(stroke.radius * std::min(w, h)), feather(stroke.feather) {
        double minX = w, maxX = 0, minY = h, maxY = 0;
        for (const auto &p : stroke.points) {
            const double x = p[0] * w - .5, y = p[1] * h - .5;
            points.push_back({x, y});
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
        left = std::max(0, static_cast<int>(std::floor(minX - radius * 1.5)));
        top = std::max(0, static_cast<int>(std::floor(minY - radius * 1.5)));
        right = std::min(w - 1, static_cast<int>(std::ceil(maxX + radius * 1.5)));
        bottom = std::min(h - 1, static_cast<int>(std::ceil(maxY + radius * 1.5)));
    }
    double distance(double x, double y) const {
        double best = std::numeric_limits<double>::infinity();
        auto a = points.front();
        for (const auto &b : points) {
            const double dx = b[0] - a[0], dy = b[1] - a[1], length = dx * dx + dy * dy;
            const double t =
                length > 1e-12 ? std::clamp(((x - a[0]) * dx + (y - a[1]) * dy) / length, 0.0, 1.0)
                               : 0;
            best = std::min(best, std::hypot(x - a[0] - t * dx, y - a[1] - t * dy));
            a = b;
        }
        return best;
    }
    double coverage(double x, double y) const {
        const double d = distance(x, y) / radius;
        if (d >= 1)
            return 0;
        if (feather <= 0 || d <= 1 - feather)
            return 1;
        const double t = (1 - d) / feather;
        return t * t * (3 - 2 * t);
    }
};
struct Sample {
    std::array<double, 3> rgb{};
    double alpha = 0;
};
Sample sample(const FloatImage &image, double x, double y) {
    if (x < 0 || y < 0 || x > image.width - 1 || y > image.height - 1)
        return {};
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    Sample value;
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
            const int sx = std::min(x0 + dx, image.width - 1),
                      sy = std::min(y0 + dy, image.height - 1);
            const double weight = (dx ? x - x0 : 1 - (x - x0)) * (dy ? y - y0 : 1 - (y - y0)) *
                                  image.coverage(sx, sy);
            value.alpha += weight;
            const auto offset = (static_cast<std::size_t>(sy) * image.width + sx) * 3;
            for (std::size_t c = 0; c < 3; ++c)
                value.rgb[c] += image.rgb[offset + c] * weight;
        }
    if (value.alpha > 1e-8)
        for (auto &v : value.rgb)
            v /= value.alpha;
    return value;
}
struct RingPoint {
    int x, y;
    Sample value;
};
std::vector<RingPoint> ring(const FloatImage &image, const Shape &shape, CancellationToken stop) {
    std::vector<RingPoint> result;
    // Bounded, fixed sampling of the perimeter for fitting/search.
    const int step = std::max(1, static_cast<int>(std::ceil(
                                     std::sqrt(static_cast<double>((shape.right - shape.left + 1) *
                                                                   (shape.bottom - shape.top + 1)) /
                                               2048))));
    for (int y = shape.top; y <= shape.bottom; y += step) {
        checkCancellation(stop);
        for (int x = shape.left; x <= shape.right; x += step) {
            const auto d = shape.distance(x, y) / shape.radius;
            if (d >= 1 && d <= 1.5 && image.coverage(x, y) > .99F)
                result.push_back({x, y, sample(image, x, y)});
        }
    }
    return result;
}
void apply(FloatImage &image, const RetouchStroke &stroke, CancellationToken stop) {
    const Shape shape(stroke, image.width, image.height);
    const double dx = stroke.sourceOffsetX * image.width, dy = stroke.sourceOffsetY * image.height;
    std::array<double, 3> delta{};
    if (stroke.mode != RetouchMode::clone) {
        double weight = 0;
        for (const auto &p : ring(image, shape, stop)) {
            const auto source = sample(image, p.x + dx, p.y + dy);
            if (source.alpha < .99)
                continue;
            weight += 1;
            for (std::size_t c = 0; c < 3; ++c)
                delta[c] += p.value.rgb[c] - source.rgb[c];
        }
        if (weight > 0)
            for (auto &value : delta)
                value /= weight;
    }
    const int width = shape.right - shape.left + 1, height = shape.bottom - shape.top + 1;
    // Snapshot the donor before any write, including overlapping clone strokes.
    std::vector<Sample> donor(pixelCount(width, height));
    for (int y = shape.top; y <= shape.bottom; ++y) {
        checkCancellation(stop);
        for (int x = shape.left; x <= shape.right; ++x)
            donor[static_cast<std::size_t>(y - shape.top) * width + x - shape.left] =
                sample(image, x + dx, y + dy);
    }
    for (int y = shape.top; y <= shape.bottom; ++y) {
        checkCancellation(stop);
        for (int x = shape.left; x <= shape.right; ++x) {
            if (image.coverage(x, y) <= 0)
                continue;
            const auto &source =
                donor[static_cast<std::size_t>(y - shape.top) * width + x - shape.left];
            const double a = shape.coverage(x, y) * stroke.opacity * source.alpha;
            if (a <= 0)
                continue;
            const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 3;
            for (std::size_t c = 0; c < 3; ++c)
                image.rgb[offset + c] = static_cast<float>(std::lerp(
                    static_cast<double>(image.rgb[offset + c]), source.rgb[c] + delta[c], a));
        }
    }
}
} // namespace
RetouchPass::RetouchPass(const FloatImage &source, const std::vector<RetouchStroke> &strokes,
                         CancellationToken stop)
    : source_(source) {
    validateRetouch(strokes);
    if (std::none_of(strokes.begin(), strokes.end(),
                     [](const auto &s) { return s.enabled && s.opacity > 0; }))
        return;
    source.validateCoverage();
    result_ = source;
    for (const auto &s : strokes) {
        checkCancellation(stop);
        if (s.enabled && s.opacity > 0)
            apply(*result_, s, stop);
    }
}
std::array<double, 2> findRetouchSource(const FloatImage &image, const RetouchStroke &stroke,
                                        CancellationToken stop) {
    validateRetouch({stroke});
    image.validateCoverage();
    const Shape shape(stroke, image.width, image.height);
    const auto boundary = ring(image, shape, stop);
    if (boundary.size() < 6)
        throw std::runtime_error("Not enough surrounding detail. Choose a source manually.");
    double best = std::numeric_limits<double>::infinity();
    std::array<double, 2> offset{};
    // Search candidates on a deterministic coarse lattice, then refine locally.
    const auto score = [&](double dx, double dy) {
        // Require a fully separate donor with valid image coverage for all brush points.
        for (const auto &p : shape.points) {
            const double x = p[0] + dx, y = p[1] + dy;
            if (x < shape.radius || y < shape.radius || x + shape.radius >= image.width ||
                y + shape.radius >= image.height || shape.distance(x, y) <= 2 * shape.radius)
                return std::numeric_limits<double>::infinity();
        }
        std::array<double, 3> mean{};
        double squared = 0;
        for (const auto &p : boundary) {
            const auto candidate = sample(image, p.x + dx, p.y + dy);
            if (candidate.alpha < .99)
                return std::numeric_limits<double>::infinity();
            for (std::size_t c = 0; c < 3; ++c) {
                const double difference = candidate.rgb[c] - p.value.rgb[c];
                if (!std::isfinite(difference))
                    return std::numeric_limits<double>::infinity();
                mean[c] += difference;
                squared += difference * difference;
            }
        }
        // Fit a brightness/color offset but penalize large color changes.
        for (const auto value : mean)
            squared -= .9 * value * value / boundary.size();
        return squared / boundary.size();
    };
    const double step = std::max(2.0, std::min(image.width, image.height) / 24.0);
    const int columns = static_cast<int>(std::floor(2.0 * image.width / step));
    const int rows = static_cast<int>(std::floor(2.0 * image.height / step));
    for (int row = 0; row <= rows; ++row) {
        checkCancellation(stop);
        const double dy = -image.height + row * step;
        for (int column = 0; column <= columns; ++column) {
            const double dx = -image.width + column * step;
            const double cost = score(dx, dy);
            if (cost < best) {
                best = cost;
                offset = {dx, dy};
            }
        }
    }
    if (!std::isfinite(best))
        throw std::runtime_error("No suitable source found. Choose a source manually.");
    const int refinementLevels = std::ilogb(step) + 1;
    for (int level = 1; level <= refinementLevels; ++level) {
        const double stride = std::ldexp(step, -level);
        const auto center = offset;
        for (int y = -1; y <= 1; ++y) {
            checkCancellation(stop);
            for (int x = -1; x <= 1; ++x) {
                const double dx = center[0] + stride * x, dy = center[1] + stride * y;
                const double cost = score(dx, dy);
                if (cost < best) {
                    best = cost;
                    offset = {dx, dy};
                }
            }
        }
    }
    return {offset[0] / image.width, offset[1] / image.height};
}
} // namespace luma
