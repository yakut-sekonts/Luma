#pragma once
#include "core/color/WhiteBalance.h"
#include "core/edits/EditDocument.h"
#include "core/image/FloatImage.h"
#include "core/metadata/PhotoMetadata.h"
#include <optional>

namespace luma {
// nullopt: gray-world estimate of the visible crop. A point: small neutral
// patch at normalized displayed coordinates, including EXIF/crop/transform.
// Uses camera-space source preview after optics/retouch, before Develop/Detail,
// local masks, display conversion and overlays. It never reads screen pixels.
WhiteBalanceEstimate sampleWhiteBalance(const FloatImage &source, const EditDocument &edits,
                                        const PhotoMetadata &metadata,
                                        const RawColorCalibration &calibration,
                                        std::optional<std::array<double, 2>> point = std::nullopt,
                                        CancellationToken stop = {});
} // namespace luma
