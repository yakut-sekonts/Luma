#pragma once
#include "core/edits/BitmapMask.h"
#include "core/image/FloatImage.h"
#include "core/jobs/Cancellation.h"
#include <array>
#include <functional>
#include <span>
#include <string>

namespace luma::ai {
enum class ExecutionLocation { local, cloud };
enum class Selection { subject, background, object, sky };
struct RgbaImage {
    int width = 0, height = 0;
    std::span<const std::uint8_t> pixels;
    void validate() const {
        if (pixels.size() != pixelCount(width, height) * 4)
            throw std::runtime_error("Invalid AI input image.");
    }
};
using Progress = std::function<void(double)>;
class ISegmentationProvider {
  public:
    virtual ~ISegmentationProvider() = default;
    virtual std::string identifier() const = 0;
    virtual ExecutionLocation location() const = 0;
    virtual bool supports(Selection selection) const = 0;
    // Point coordinates are normalized top-left image coordinates.
    virtual BitmapMask segment(RgbaImage image, Selection selection, std::array<double, 2> point,
                               CancellationToken stop, const Progress &progress = {}) = 0;
};
class IDenoiseProvider {
  public:
    virtual ~IDenoiseProvider() = default;
    virtual std::string identifier() const = 0;
    virtual ExecutionLocation location() const = 0;
    virtual FloatImage denoise(const FloatImage &linear, double amount, CancellationToken stop,
                               const Progress &progress = {}) = 0;
};
class IEmbeddingProvider {
  public:
    virtual ~IEmbeddingProvider() = default;
    virtual std::string identifier() const = 0;
    virtual ExecutionLocation location() const = 0;
    virtual std::vector<float> imageEmbedding(RgbaImage image, CancellationToken stop) = 0;
    virtual std::vector<float> textEmbedding(const std::string &text, CancellationToken stop) = 0;
};
} // namespace luma::ai
