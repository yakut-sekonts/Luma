#pragma once
#include "core/image/FloatImage.h"
#include "core/jobs/Cancellation.h"

namespace luma {
void checkCancellation(luma::CancellationToken stop);
FloatImage demosaicBilinear(const BayerFrame &frame, luma::CancellationToken stop = {});
FloatImage demosaicEdgeAware(const BayerFrame &frame, luma::CancellationToken stop = {});
void cameraToLinearSRGB(FloatImage &image, const std::array<float, 9> &matrix,
                        luma::CancellationToken stop = {});
} // namespace luma
