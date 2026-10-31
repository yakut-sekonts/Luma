#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace luma {
enum class DisplayMapping { clip, adaptiveHDR };
/// Converts scene-linear sRGB to a requested ICC display profile; empty means sRGB.
class DisplayTransform {
  public:
    explicit DisplayTransform(std::span<const std::uint8_t> profile = {});
    ~DisplayTransform();
    DisplayTransform(const DisplayTransform &) = delete;
    DisplayTransform &operator=(const DisplayTransform &) = delete;
    void convert(std::span<const float> rgb, std::span<std::uint8_t> rgba,
                 DisplayMapping mapping = DisplayMapping::clip);

  private:
    void *transform_ = nullptr;
    std::vector<float> mapped_;
};
} // namespace luma
