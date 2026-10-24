#pragma once
#include "core/image/FloatImage.h"
#include "core/jobs/Cancellation.h"
#include "core/render/DevelopPipeline.h"
#include <vector>

namespace luma {
class DevelopAccelerator {
  public:
    virtual ~DevelopAccelerator() = default;
    // Empty means unsupported/unavailable: the caller uses the CPU reference.
    // A successful result is scene-linear RGB with exactly input.rgb.size()
    // samples; coverage, geometry, Detail, local masks and ICC remain separate.
    virtual std::vector<float> develop(const FloatImage &input, const DevelopPipeline &pipeline,
                                       CancellationToken stop) = 0;
};
} // namespace luma
