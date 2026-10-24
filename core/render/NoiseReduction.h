#pragma once
#include "core/render/Sharpening.h"

namespace luma {
class NoiseReductionPass {
  public:
    NoiseReductionPass(const FloatImage &input, const DevelopPipeline &pipeline,
                       const EditDocument &edits, CancellationToken stop = {}, double scale = 1,
                       DetailDiagnostic diagnostic = DetailDiagnostic::none,
                       const std::function<void(double)> &progress = {},
                       std::span<const float> developedPixels = {});
    std::array<float, 3> pixel(std::array<float, 3> developed, int x, int y) const;
    bool active() const {
        return !components_.empty();
    }

  private:
    int width_, height_;
    DetailDiagnostic diagnostic_;
    // Linear luminance and R-Y/B-Y, not RGB. Empty for the neutral path.
    std::vector<float> components_;
};
} // namespace luma
