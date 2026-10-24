#pragma once
#include "core/edits/Retouch.h"
#include "core/image/FloatImage.h"
#include "core/jobs/Cancellation.h"
#include <optional>

namespace luma {
class RetouchPass {
  public:
    RetouchPass(const FloatImage &source, const std::vector<RetouchStroke> &strokes,
                CancellationToken stop = {});
    const FloatImage &image() const {
        return result_ ? *result_ : source_;
    }

  private:
    const FloatImage &source_;
    std::optional<FloatImage> result_;
};
// Deterministic donor search against the surrounding ring, excluding the
// complete target stroke. Persists a translation, never regenerates on export.
std::array<double, 2> findRetouchSource(const FloatImage &image, const RetouchStroke &stroke,
                                        CancellationToken stop = {});
} // namespace luma
