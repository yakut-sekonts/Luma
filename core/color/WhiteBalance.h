#pragma once
#include "core/color/RawColorCalibration.h"
#include "core/jobs/Cancellation.h"

namespace luma {
// The existing Temperature/Tint model shared by rendering and estimation.
// Callers supply validated values: 2000–25000 K, Tint −150…150.
std::array<float, 3> whiteBalanceGains(double temperature, double tint,
                                       const RawColorCalibration &calibration);
struct WhiteBalanceEstimate {
    double temperature = 6500;
    double tint = 0;
    bool limited = false;
};
// Fit camera-space neutral RGB to the renderer's actual gain model. The result
// uses existing EditDocument parameters, not a second WB transform or schema.
WhiteBalanceEstimate estimateWhiteBalance(const std::array<double, 3> &neutral,
                                          const RawColorCalibration &calibration,
                                          CancellationToken stop = {});
} // namespace luma
