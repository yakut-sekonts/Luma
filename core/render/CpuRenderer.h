#pragma once
#include "core/color/RawColorCalibration.h"
#include "core/edits/EditDocument.h"
#include "core/image/FloatImage.h"
#include "core/jobs/Cancellation.h"
#include "core/metadata/PhotoMetadata.h"
#include "core/render/DevelopAccelerator.h"
#include "core/render/ImageHistogram.h"
#include "core/render/LocalAdjustments.h"
#include "core/render/Sharpening.h"
#include <cstdint>
#include <optional>
#include <span>

namespace luma {
struct DisplayImage {
    int width;
    int height;
    std::vector<std::uint8_t> rgba;
    std::optional<ImageHistogram> histogram = std::nullopt;
};
FloatImage standardPreview(const FloatImage &input, int maximumDimension,
                           luma::CancellationToken stop = {});
DisplayImage renderCPU(const FloatImage &input, EditDocument edits, int orientation = 0,
                       std::span<const std::uint8_t> displayProfile = {},
                       luma::CancellationToken stop = {}, RawColorCalibration calibration = {},
                       DetailDiagnostic diagnostic = DetailDiagnostic::none, double detailScale = 1,
                       PhotoMetadata metadata = {}, MaskOverlay overlay = {},
                       DevelopAccelerator *accelerator = nullptr,
                       PreviewAnalysisOptions analysis = {}, unsigned maximumWorkers = 4);
float applyExposure(float linearValue, double ev);
} // namespace luma
