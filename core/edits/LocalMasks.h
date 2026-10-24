#pragma once
#include "core/edits/BitmapMask.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace luma {
enum class MaskShape : std::uint8_t { linear, radial, brush, luminance, color, bitmap };
enum class MaskOperation : std::uint8_t { add, subtract, intersect };
inline constexpr std::array<std::string_view, 8> localParameters{
    "exposure", "contrast", "highlights", "shadows", "whites", "blacks", "vibrance", "saturation"};
struct BrushPoint {
    double x = 0, y = 0;
    bool operator==(const BrushPoint &) const = default;
};
struct BrushStroke {
    bool erase = false;
    double radius = 0.04, opacity = 100;
    std::vector<BrushPoint> points;
    bool operator==(const BrushStroke &) const = default;
};
struct MaskGeometry {
    MaskShape shape = MaskShape::linear;
    double x1 = 0.5, y1 = 0.25, x2 = 0.5, y2 = 0.75;
    double angle = 0, feather = 100;
    double rangeLow = 0.25, rangeHigh = 0.75, colorTolerance = 0.15;
    std::array<double, 3> referenceColor{0.5, 0.5, 0.5};
    std::vector<BrushStroke> strokes;
    BitmapMask bitmap;
    bool operator==(const MaskGeometry &) const = default;
    void validate() const;
    double parameter(std::string_view name) const;
    void setParameter(std::string_view name, double value);
};
struct MaskComponent {
    std::string id;
    MaskOperation operation = MaskOperation::add;
    bool enabled = true, inverted = false;
    MaskGeometry geometry;
    bool operator==(const MaskComponent &) const = default;
};
struct LocalMask : MaskGeometry {
    std::string id, name;
    bool enabled = true, inverted = false;
    double opacity = 100;
    std::array<double, 8> adjustments{};
    std::vector<MaskComponent> components;
    bool operator==(const LocalMask &) const = default;
    void validate() const;
    bool neutral() const;
    double parameter(std::string_view name) const;
    void setParameter(std::string_view name, double value);
    // Convenience evaluation; renderers compile MaskCoverage once per mask.
    double coverage(double x, double y, std::array<float, 3> reference = {},
                    double aspect = 1) const;
};
void validateLocalMasks(const std::vector<LocalMask> &masks);
std::string encodeLocalMasks(const std::vector<LocalMask> &masks);
std::vector<LocalMask> decodeLocalMasks(std::string_view text);
} // namespace luma
