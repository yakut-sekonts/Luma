#pragma once
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace luma {
enum class RetouchMode { clone, heal, remove };
struct RetouchStroke {
    std::string id;
    RetouchMode mode = RetouchMode::heal;
    bool enabled = true;
    // Coordinates refer to the post-optics, pre-crop source canvas.
    std::vector<std::array<double, 2>> points;
    double radius = .025; // Fraction of the short image edge.
    double feather = .75;
    double opacity = 1;
    // Remove resolves its donor once, on commit, and persists this translation.
    double sourceOffsetX = 0, sourceOffsetY = 0;
    bool operator==(const RetouchStroke &) const = default;
};
void validateRetouch(const std::vector<RetouchStroke> &strokes);
std::string encodeRetouch(const std::vector<RetouchStroke> &strokes);
std::vector<RetouchStroke> decodeRetouch(std::string_view text);
} // namespace luma
