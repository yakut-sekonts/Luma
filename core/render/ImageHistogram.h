#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace luma {
enum class ClippingPreview : std::uint8_t { none = 0, shadows = 1, highlights = 2, both = 3 };
struct PreviewAnalysisOptions {
    // Appended to cache identity by the editor; never enabled for exports.
    bool histogram = false;
    ClippingPreview clipping = ClippingPreview::none;
    bool pointColorRange = false;
};

// Display-referred sRGB bins from processed, scene-linear sRGB before monitor
// conversion or diagnostic overlays. Coverage weights exclude transparent areas.
struct ImageHistogram {
    struct PreparedSample {
        std::array<std::uint8_t, 4> bins{};
        float coverage = 0;
        std::uint8_t clipping = 0;
    };
    static PreparedSample prepare(const std::array<float, 3> &rgb, float coverage);
    void add(const PreparedSample &sample);
    std::array<std::array<double, 256>, 4> bins{}; // R, G, B, encoded linear luminance
    double weight = 0, shadows = 0, highlights = 0;
    std::uint32_t samples = 0;
    void add(const std::array<float, 3> &rgb, float coverage);
};

// Midpoints of a regular grid with at most 512 samples along each axis.
// Sampling happens after geometry/detail/local adjustments, without a second render.
class HistogramSamplingGrid {
  public:
    HistogramSamplingGrid(int width, int height);
    bool contains(int x, int y) const;

  private:
    std::vector<bool> columns_, rows_;
};

// Shadows: all channels <= 0. Highlights: at least one channel >= 1.
// These are output-referred boundaries, not camera-sensor saturation tests.
std::uint8_t clippingFlags(const std::array<float, 3> &rgb);
} // namespace luma
