#pragma once
#include "core/merge/HdrMerge.h"
#include <vector>
namespace luma {
enum class PanoramaProjection { perspective, cylindrical, spherical };
struct PanoramaOptions {
    PanoramaProjection projection = PanoramaProjection::cylindrical;
    double fieldOfView = 60;   // Horizontal field of view of one input frame.
    bool boundaryWarp = false; // Stretch contiguous row boundaries instead of rectangular crop.
};
// Matrices map normalized top-left source coordinates into the first frame.
// The returned float image is cropped to its largest fully covered rectangle.
FloatImage mergePanorama(int referenceWidth, int referenceHeight,
                         const std::vector<Homography> &placements,
                         const std::function<MergeFrame(std::size_t)> &load,
                         const PanoramaOptions &options, CancellationToken stop = {},
                         const std::function<void(double)> &progress = {});
} // namespace luma
