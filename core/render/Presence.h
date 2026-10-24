#pragma once
#include "core/edits/EditDocument.h"
#include "core/image/FloatImage.h"
#include "core/jobs/Cancellation.h"
#include <functional>
namespace luma {
class PresencePass {
  public:
    PresencePass(const FloatImage &image, const EditDocument &edits,
                 const std::function<std::array<float, 3>(int, int)> &pixel,
                 CancellationToken stop = {}, double scale = 1);
    std::array<float, 3> pixel(std::array<float, 3> color, int x, int y) const;

  private:
    bool active_ = false;
    int width_;
    double texture_ = 0, clarity_ = 0, dehaze_ = 0;
    std::vector<float> fine_, coarse_;
};
} // namespace luma
