#include "core/render/MaskCoverage.h"
#include "core/render/AdvancedColor.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
namespace luma {
namespace {
double smooth(double value) {
    value = std::clamp(value, 0.0, 1.0);
    return value * value * (3 - 2 * value);
}
double edge(double distance, double radius, double feather) {
    if (feather == 0)
        return distance <= radius ? 1 : 0;
    return 1 - smooth((distance / radius - (1 - feather)) / feather);
}
} // namespace
std::array<double, 3> maskReferenceColor(std::array<float, 3> linear) {
    std::array<double, 3> result;
    for (std::size_t c = 0; c < 3; ++c) {
        if (!std::isfinite(linear[c]))
            throw std::runtime_error("Invalid range reference pixel.");
        result[c] = std::clamp(encodeSRGB(linear[c]), 0.0, 1.0);
    }
    return result;
}
double maskReferenceLuminance(std::array<float, 3> linear) {
    for (float value : linear)
        if (!std::isfinite(value))
            throw std::runtime_error("Invalid range reference pixel.");
    return std::clamp(encodeSRGB(0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2]), 0.0,
                      1.0);
}
PrimitiveCoverage::PrimitiveCoverage(MaskGeometry geometry, double aspect)
    : geometry_(std::move(geometry)) {
    geometry_.validate();
    if (!std::isfinite(aspect) || aspect <= 0)
        throw std::runtime_error("Invalid brush aspect ratio.");
    xScale_ = std::max(1.0, aspect);
    yScale_ = std::max(1.0, 1 / aspect);
    for (std::size_t s = 0; s < geometry_.strokes.size(); ++s) {
        const auto &stroke = geometry_.strokes[s];
        for (std::size_t p = 0; p < stroke.points.size(); ++p) {
            const auto &a = stroke.points[p == 0 ? 0 : p - 1], &b = stroke.points[p];
            segments_.push_back(
                {{a.x * xScale_, a.y * yScale_}, {b.x * xScale_, b.y * yScale_}, stroke.radius, s});
        }
    }
    if (!segments_.empty())
        build(0, segments_.size());
}
int PrimitiveCoverage::build(std::size_t first, std::size_t count) {
    Node node;
    node.first = first;
    node.count = count;
    node.bounds = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                   -std::numeric_limits<double>::infinity(),
                   -std::numeric_limits<double>::infinity()};
    for (std::size_t i = first; i < first + count; ++i) {
        const auto &s = segments_[i];
        for (std::size_t axis = 0; axis < 2; ++axis) {
            node.bounds[axis] =
                std::min(node.bounds[axis], std::min(s.a[axis], s.b[axis]) - s.radius);
            node.bounds[axis + 2] =
                std::max(node.bounds[axis + 2], std::max(s.a[axis], s.b[axis]) + s.radius);
        }
    }
    const int index = static_cast<int>(nodes_.size());
    nodes_.push_back(node);
    if (count > 8) {
        const std::size_t axis =
            node.bounds[2] - node.bounds[0] >= node.bounds[3] - node.bounds[1] ? 0 : 1;
        const auto middle = first + count / 2;
        std::nth_element(segments_.begin() + static_cast<std::ptrdiff_t>(first),
                         segments_.begin() + static_cast<std::ptrdiff_t>(middle),
                         segments_.begin() + static_cast<std::ptrdiff_t>(first + count),
                         [&](const Segment &a, const Segment &b) {
                             return a.a[axis] + a.b[axis] < b.a[axis] + b.b[axis];
                         });
        const int left = build(first, count / 2), right = build(middle, count - count / 2);
        nodes_[static_cast<std::size_t>(index)].left = left;
        nodes_[static_cast<std::size_t>(index)].right = right;
    }
    return index;
}
void PrimitiveCoverage::query(int index, double x, double y,
                              std::array<double, 256> &amounts) const {
    const auto &node = nodes_[static_cast<std::size_t>(index)];
    const auto &box = node.bounds;
    if (x < box[0] || y < box[1] || x > box[2] || y > box[3])
        return;
    if (node.left >= 0) {
        query(node.left, x, y, amounts);
        query(node.right, x, y, amounts);
        return;
    }
    for (std::size_t i = node.first; i < node.first + node.count; ++i) {
        const auto &s = segments_[i];
        if (amounts[s.stroke] == 1)
            continue;
        const double dx = s.b[0] - s.a[0], dy = s.b[1] - s.a[1], length = dx * dx + dy * dy;
        const double t =
            length == 0 ? 0
                        : std::clamp(((x - s.a[0]) * dx + (y - s.a[1]) * dy) / length, 0.0, 1.0);
        const double d = std::hypot(x - s.a[0] - t * dx, y - s.a[1] - t * dy);
        amounts[s.stroke] = std::max(amounts[s.stroke], edge(d, s.radius, geometry_.feather / 100));
    }
}
double PrimitiveCoverage::coverage(double x, double y, std::array<float, 3> reference) const {
    if (!std::isfinite(x) || !std::isfinite(y))
        return 0;
    const auto &g = geometry_;
    const double softness = g.feather / 100;
    switch (g.shape) {
    case MaskShape::bitmap:
        return g.bitmap.coverage(x, y);
    case MaskShape::linear: {
        const double dx = g.x2 - g.x1, dy = g.y2 - g.y1,
                     t = ((x - g.x1) * dx + (y - g.y1) * dy) / (dx * dx + dy * dy);
        return softness == 0 ? (t >= .5 ? 1 : 0) : smooth((t - (1 - softness) / 2) / softness);
    }
    case MaskShape::radial: {
        const double r = g.angle * std::numbers::pi / 180, c = std::cos(r), s = std::sin(r),
                     dx = x - g.x1, dy = y - g.y1;
        return edge(std::hypot((c * dx + s * dy) / g.x2, (-s * dx + c * dy) / g.y2), 1, softness);
    }
    case MaskShape::brush: {
        std::array<double, 256> amounts{};
        if (!nodes_.empty())
            query(0, x * xScale_, y * yScale_, amounts);
        double coverage = 0;
        for (std::size_t i = 0; i < g.strokes.size(); ++i) {
            const auto &stroke = g.strokes[i];
            const double amount = amounts[i] * stroke.opacity / 100;
            coverage = stroke.erase ? coverage * (1 - amount) : coverage + (1 - coverage) * amount;
        }
        return coverage;
    }
    case MaskShape::luminance: {
        const double value = maskReferenceLuminance(reference), width = softness * .25;
        if (value >= g.rangeLow && value <= g.rangeHigh)
            return 1;
        if (width == 0)
            return 0;
        return value < g.rangeLow ? smooth((value - g.rangeLow + width) / width)
                                  : smooth((g.rangeHigh + width - value) / width);
    }
    case MaskShape::color: {
        const auto color = maskReferenceColor(reference);
        double squared = 0;
        for (std::size_t c = 0; c < 3; ++c)
            squared += (color[c] - g.referenceColor[c]) * (color[c] - g.referenceColor[c]);
        const double distance = std::sqrt(squared / 3);
        return g.colorTolerance == 0 ? (distance == 0 ? 1 : 0)
                                     : edge(distance, g.colorTolerance, softness);
    }
    }
    throw std::runtime_error("Unknown mask primitive.");
}
MaskCoverage::MaskCoverage(const LocalMask &mask, double aspect)
    : base_(mask, aspect), enabled_(mask.enabled), inverted_(mask.inverted),
      opacity_(mask.opacity / 100) {
    mask.validate();
    for (const auto &component : mask.components)
        if (component.enabled)
            operands_.push_back({component.operation, component.inverted,
                                 PrimitiveCoverage(component.geometry, aspect)});
}
double MaskCoverage::coverage(double x, double y, std::array<float, 3> reference) const {
    if (!enabled_ || !std::isfinite(x) || !std::isfinite(y))
        return 0;
    double a = base_.coverage(x, y, reference);
    for (const auto &operand : operands_) {
        const double raw = operand.primitive.coverage(x, y, reference),
                     b = operand.inverted ? 1 - raw : raw;
        switch (operand.operation) {
        case MaskOperation::add:
            a = a + (1 - a) * b;
            break;
        case MaskOperation::subtract:
            a *= 1 - b;
            break;
        case MaskOperation::intersect:
            a *= b;
            break;
        }
    }
    return std::clamp(inverted_ ? 1 - a : a, 0.0, 1.0) * opacity_;
}
double LocalMask::coverage(double x, double y, std::array<float, 3> reference,
                           double aspect) const {
    return MaskCoverage(*this, aspect).coverage(x, y, reference);
}
} // namespace luma
