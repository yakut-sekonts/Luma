#pragma once
#include "core/edits/CreativeSettings.h"
#include "core/edits/CropSettings.h"
#include "core/edits/LensSettings.h"
#include "core/edits/LocalMasks.h"
#include "core/edits/Retouch.h"
#include "core/edits/TransformSettings.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace luma {
enum class Correction : std::uint32_t {
    whiteBalance = 1,
    exposure = 2,
    contrast = 4,
    highlights = 8,
    shadows = 16,
    whites = 32,
    blacks = 64,
    vibrance = 128,
    saturation = 256,
    curves = 512,
    hsl = 1024,
    sharpening = 2048,
    luminanceNoise = 4096,
    colorNoise = 8192,
    crop = 16384,
    lens = 32768,
    transform = 65536,
    presence = 131072,
    pointColor = 262144,
    grading = 524288,
    vignette = 1048576,
    grain = 2097152
};
struct CurvePoint {
    double x = 0;
    double y = 0;
    bool operator==(const CurvePoint &) const = default;
};
struct PointCurve {
    std::vector<CurvePoint> points{{0, 0}, {1, 1}};
    bool operator==(const PointCurve &) const = default;
    void validate() const;
};
struct HSLBand {
    double hue = 0;
    double saturation = 0;
    double luminance = 0;
    bool operator==(const HSLBand &) const = default;
};
inline constexpr std::array<std::string_view, 4> curveChannels{"rgb", "red", "green", "blue"};
inline constexpr std::array<std::string_view, 8> hslBands{"red",  "orange", "yellow", "green",
                                                          "aqua", "blue",   "purple", "magenta"};
struct EditDocument {
    static constexpr int schemaVersion = 8;
    double exposure = 0;
    double temperature = 6500;
    double tint = 0;
    bool customWhiteBalance = false;
    double contrast = 0;
    double highlights = 0;
    double shadows = 0;
    double whites = 0;
    double blacks = 0;
    double vibrance = 0;
    double saturation = 0;
    std::uint32_t enabledCorrections = 98303;
    double sharpenAmount = 0;
    double sharpenRadius = 1;
    double sharpenDetail = 25;
    double sharpenMasking = 0;
    double noiseLuminance = 0;
    double noiseLuminanceDetail = 50;
    double noiseLuminanceContrast = 0;
    double noiseColor = 0;
    double noiseColorDetail = 50;
    double noiseColorSmoothness = 50;
    CropSettings crop{};
    LensSettings lens{};
    TransformSettings transform{};
    double curveShadows = 0;
    double curveDarks = 0;
    double curveLights = 0;
    double curveHighlights = 0;
    std::array<PointCurve, 4> curves{};
    std::array<HSLBand, 8> hsl{};
    std::vector<LocalMask> localMasks{};
    std::vector<RetouchStroke> retouch{};
    CreativeSettings creative{};
    bool operator==(const EditDocument &) const = default;
    bool enabled(Correction correction) const {
        const auto bit = static_cast<std::uint32_t>(correction);
        return ((bit & creativeCorrectionMask ? creative.enabled : enabledCorrections) & bit) != 0;
    }
    void setEnabled(Correction correction, bool value) {
        const auto mask = static_cast<std::uint32_t>(correction);
        auto &flags = mask & creativeCorrectionMask ? creative.enabled : enabledCorrections;
        flags = value ? flags | mask : flags & ~mask;
    }
    void validate() const;
    double parameter(std::string_view name) const;
    void setParameter(std::string_view name, double value);
    void setParameters(const std::vector<std::pair<std::string, double>> &values);
    static Correction correction(std::string_view name);
    static std::size_t curveChannel(std::string_view name);
    static std::size_t hslBand(std::string_view name);
};
} // namespace luma
