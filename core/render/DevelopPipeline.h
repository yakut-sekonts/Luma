#pragma once
#include "core/color/RawColorCalibration.h"
#include "core/edits/EditDocument.h"
#include "core/render/AdvancedColor.h"
#include <array>

namespace luma {
class DevelopPipeline {
  public:
    explicit DevelopPipeline(EditDocument edits, RawColorCalibration calibration = {});
    std::array<float, 3> pixel(std::array<float, 3> camera) const;
    // Packed float layout shared with DevelopShader:
    // basic[20], flags[2], parametric[4], HSL[24],
    // curve endpoint slopes[8], four 4097-sample PCHIP tables.
    std::vector<float> gpuParameters() const;
    bool gpuCompatible() const {
        // Combined curve/HSL HDR remapping amplifies FP32 rounding beyond the
        // qualified error budget. Preserve the double-precision CPU reference.
        return !creativeColorActive_ && !(curvesActive_ && hslActive_);
    }

  private:
    bool creativeColorActive_ = false;
    EditDocument edits_;
    RawColorCalibration calibration_;
    std::array<float, 3> gains_;
    float exposureGain_ = 1;
    std::array<ToneCurve, 4> curves_;
    bool curvesActive_ = false;
    bool hslActive_ = false;
};
} // namespace luma
