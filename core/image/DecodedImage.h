#pragma once
#include "core/color/RawColorCalibration.h"
#include "core/image/DecodeDiagnostics.h"
#include "core/image/FloatImage.h"
#include "core/metadata/PhotoMetadata.h"

namespace luma {
struct DecodedImage {
    FloatImage image;
    PhotoMetadata metadata;
    RawColorCalibration calibration;
    DecodeDiagnostics diagnostics;
};
} // namespace luma
