#pragma once
#include "core/edits/EditDocument.h"
#include <array>
#include <span>
#include <vector>

namespace luma {
double encodeSRGB(double linear);
double decodeSRGB(double encoded);
class ToneCurve {
  public:
    explicit ToneCurve(const PointCurve &curve = {});
    double value(double x) const;
    std::span<const double> samples() const {
        return samples_;
    }
    double startSlope() const {
        return startSlope_;
    }
    double endSlope() const {
        return endSlope_;
    }

  private:
    std::vector<double> samples_;
    double startSlope_ = 1;
    double endSlope_ = 1;
};
struct HSLColor {
    double hue = 0; // Degrees in [0,360).
    double saturation = 0;
    double luminance = 0;
};
HSLColor toHSL(const std::array<double, 3> &rgb);
std::array<double, 3> fromHSL(HSLColor color);
std::array<double, 8> hueWeights(double hue);
double parametricCurve(double x, const EditDocument &edits);
std::array<double, 3> mixHSL(std::array<double, 3> rgb, const std::array<HSLBand, 8> &bands);
} // namespace luma
