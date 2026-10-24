#pragma once
#include "core/edits/EditDocument.h"
#include "core/raw/RawDecoder.h"
namespace luma {
class IAutoAdjustProvider {
  public:
    virtual ~IAutoAdjustProvider() = default;
    virtual EditDocument estimate(const FloatImage &image, const EditDocument &current,
                                  const PhotoMetadata &metadata, RawColorCalibration calibration,
                                  CancellationToken stop = {}) const = 0;
};
class HistogramAutoAdjust final : public IAutoAdjustProvider {
  public:
    EditDocument estimate(const FloatImage &, const EditDocument &, const PhotoMetadata &,
                          RawColorCalibration, CancellationToken = {}) const override;
};
} // namespace luma
