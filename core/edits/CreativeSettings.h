#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace luma {
struct CreativeParameter {
    std::string_view name, label;
    double minimum, maximum, initial;
    std::uint32_t group, correction;
};
inline constexpr std::array creativeParameters{
    CreativeParameter{"presence.texture", "Texture", -100, 100, 0, 256, 131072},
    CreativeParameter{"presence.clarity", "Clarity", -100, 100, 0, 256, 131072},
    CreativeParameter{"presence.dehaze", "Dehaze", -100, 100, 0, 256, 131072},
    CreativeParameter{"point.referenceHue", "Reference hue", 0, 360, 0, 512, 262144},
    CreativeParameter{"point.referenceSaturation", "Reference saturation", 0, 100, 50, 512, 262144},
    CreativeParameter{"point.referenceLuminance", "Reference luminance", 0, 100, 50, 512, 262144},
    CreativeParameter{"point.hueRange", "Hue range", 1, 180, 30, 512, 262144},
    CreativeParameter{"point.saturationRange", "Saturation range", 1, 100, 30, 512, 262144},
    CreativeParameter{"point.luminanceRange", "Luminance range", 1, 100, 30, 512, 262144},
    CreativeParameter{"point.hue", "Hue shift", -180, 180, 0, 512, 262144},
    CreativeParameter{"point.saturation", "Saturation", -100, 100, 0, 512, 262144},
    CreativeParameter{"point.luminance", "Luminance", -100, 100, 0, 512, 262144},
    CreativeParameter{"grading.shadows.hue", "Shadows hue", 0, 360, 0, 1024, 524288},
    CreativeParameter{"grading.shadows.saturation", "Shadows saturation", 0, 100, 0, 1024, 524288},
    CreativeParameter{"grading.shadows.luminance", "Shadows luminance", -100, 100, 0, 1024, 524288},
    CreativeParameter{"grading.midtones.hue", "Midtones hue", 0, 360, 0, 1024, 524288},
    CreativeParameter{"grading.midtones.saturation", "Midtones saturation", 0, 100, 0, 1024,
                      524288},
    CreativeParameter{"grading.midtones.luminance", "Midtones luminance", -100, 100, 0, 1024,
                      524288},
    CreativeParameter{"grading.highlights.hue", "Highlights hue", 0, 360, 0, 1024, 524288},
    CreativeParameter{"grading.highlights.saturation", "Highlights saturation", 0, 100, 0, 1024,
                      524288},
    CreativeParameter{"grading.highlights.luminance", "Highlights luminance", -100, 100, 0, 1024,
                      524288},
    CreativeParameter{"grading.global.hue", "Global hue", 0, 360, 0, 1024, 524288},
    CreativeParameter{"grading.global.saturation", "Global saturation", 0, 100, 0, 1024, 524288},
    CreativeParameter{"grading.global.luminance", "Global luminance", -100, 100, 0, 1024, 524288},
    CreativeParameter{"grading.blending", "Blending", 0, 100, 50, 1024, 524288},
    CreativeParameter{"grading.balance", "Balance", -100, 100, 0, 1024, 524288},
    CreativeParameter{"vignette.amount", "Amount", -100, 100, 0, 2048, 1048576},
    CreativeParameter{"vignette.midpoint", "Midpoint", 0, 100, 50, 2048, 1048576},
    CreativeParameter{"vignette.roundness", "Roundness", -100, 100, 0, 2048, 1048576},
    CreativeParameter{"vignette.feather", "Feather", 0, 100, 50, 2048, 1048576},
    CreativeParameter{"vignette.highlights", "Highlights", 0, 100, 0, 2048, 1048576},
    CreativeParameter{"grain.amount", "Amount", 0, 100, 0, 2048, 2097152},
    CreativeParameter{"grain.size", "Size", 0, 100, 25, 2048, 2097152},
    CreativeParameter{"grain.roughness", "Roughness", 0, 100, 50, 2048, 2097152}};
inline constexpr std::uint32_t creativeCorrectionMask = 4063232;
bool isCreativeParameter(std::string_view name);
const CreativeParameter &creativeParameter(std::string_view name);
struct CreativeSettings {
    std::array<double, creativeParameters.size()> values{};
    std::uint32_t enabled = creativeCorrectionMask;
    CreativeSettings();
    double parameter(std::string_view name) const;
    void setParameter(std::string_view name, double value);
    void validate() const;
    bool operator==(const CreativeSettings &) const = default;
};
std::string encodeCreative(const CreativeSettings &settings);
CreativeSettings decodeCreative(std::string_view text);
} // namespace luma
