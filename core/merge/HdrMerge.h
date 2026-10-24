#pragma once
#include "core/image/FloatImage.h"
#include "core/jobs/Cancellation.h"
#include "core/math/Homography.h"
#include <functional>
#include <memory>

namespace luma {
struct MergeFrame {
    std::shared_ptr<const FloatImage> image;
    double exposureEV = 0; // log2(exposure time * ISO / aperture²), relative values are valid.
    // Floating-image normalized top-left coordinates -> reference normalized coordinates.
    Homography toReference;
};
struct HdrOptions {
    double deghost = .5; // 0 = off, 1 = most conservative.
    bool alignFrames = true;
};
// Loads one additional full-resolution frame at a time. The caller owns source
// identity checking; alignment is estimated on bounded previews before this call.
FloatImage mergeHDR(const MergeFrame &reference, std::size_t frameCount,
                    const std::function<MergeFrame(std::size_t)> &load, const HdrOptions &options,
                    CancellationToken stop = {}, const std::function<void(double)> &progress = {});
} // namespace luma
