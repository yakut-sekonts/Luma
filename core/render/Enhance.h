#pragma once
#include "core/edits/EditDocument.h"
#include "core/edits/QuickLookTypes.h"
#include "core/raw/RawDecoder.h"

namespace luma {
struct EnhanceResult {
    std::vector<QuickLookParameterDelta> deltas;
    std::string provider;
    std::string providerVersion;
    std::string model;
    std::string modelDigest;
    bool deterministicFallback = false;
    bool operator==(const EnhanceResult &) const = default;
};

class DeterministicEnhance final {
  public:
    EnhanceResult estimate(const FloatImage &image, const EditDocument &current,
                           const PhotoMetadata &metadata, RawColorCalibration calibration,
                           const CancellationToken &stop = {}) const;
};
} // namespace luma
