#include "core/render/Enhance.h"
#include "core/image/Bayer.h"
#include "core/quick/MatchLook.h"
#include "core/render/AutoAdjust.h"
#include <array>

namespace luma {
EnhanceResult DeterministicEnhance::estimate(const FloatImage &image, const EditDocument &current,
                                             const PhotoMetadata &metadata,
                                             RawColorCalibration calibration,
                                             const CancellationToken &stop) const {
    current.validate();
    const auto adjusted =
        HistogramAutoAdjust{}.estimate(image, current, metadata, calibration, stop);
    const std::array references{adjusted};
    auto deltas = matchLookDeltas(current, references,
                                  {.includeExposure = true, .includeWhiteBalance = false});
    checkCancellation(stop);
    return {std::move(deltas), "apertone.histogram-enhance", "1.0", {}, {}, true};
}
} // namespace luma
