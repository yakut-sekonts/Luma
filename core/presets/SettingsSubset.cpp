#include "core/presets/SettingsSubset.h"
#include <set>
#include <stdexcept>

namespace luma {
void validateSettingsGroups(std::uint32_t groups) {
    if (!groups || (groups & ~allSettingsGroups))
        throw std::invalid_argument("Select at least one supported settings group.");
}
std::uint32_t settingsCorrections(std::uint32_t groups) {
    validateSettingsGroups(groups);
    const auto flag = [](Correction correction) { return static_cast<std::uint32_t>(correction); };
    std::uint32_t mask = 0;
    if (groups & 1)
        mask |= flag(Correction::exposure) | flag(Correction::contrast) |
                flag(Correction::highlights) | flag(Correction::shadows) |
                flag(Correction::whites) | flag(Correction::blacks);
    if (groups & 2)
        mask |= flag(Correction::whiteBalance) | flag(Correction::vibrance) |
                flag(Correction::saturation);
    if (groups & 4)
        mask |= flag(Correction::curves);
    if (groups & 8)
        mask |= flag(Correction::hsl);
    if (groups & 16)
        mask |= flag(Correction::sharpening) | flag(Correction::luminanceNoise) |
                flag(Correction::colorNoise);
    if (groups & 32)
        mask |= flag(Correction::crop);
    if (groups & 64)
        mask |= flag(Correction::lens);
    if (groups & 128)
        mask |= flag(Correction::transform);
    for (const auto &parameter : creativeParameters)
        if (groups & parameter.group)
            mask |= parameter.correction;
    return mask;
}
std::uint32_t settingsParameterGroup(std::string_view name) {
    if (name.starts_with("points.")) {
        EditDocument::curveChannel(name.substr(7));
        return 4;
    }
    if (name == "whiteBalance")
        return 2;
    if (isCreativeParameter(name))
        return creativeParameter(name).group;
    // Sparse presets currently support Light/Color/Curves/HSL/Detail only.
    EditDocument{}.parameter(name);
    const auto correction = static_cast<std::uint32_t>(EditDocument::correction(name));
    for (std::uint32_t group = 1; group <= 16; group <<= 1)
        if (settingsCorrections(group) & correction)
            return group;
    throw std::invalid_argument("Unsupported sparse preset parameter.");
}
std::uint32_t parameterSelectionGroups(std::span<const std::string> parameters) {
    if (parameters.empty() || parameters.size() > 128)
        throw std::invalid_argument("Invalid sparse preset parameter count.");
    std::set<std::string> unique;
    std::uint32_t groups = 0;
    for (const auto &name : parameters) {
        if (!unique.insert(name).second)
            throw std::invalid_argument("Duplicate sparse preset parameter.");
        groups |= settingsParameterGroup(name);
    }
    return groups;
}
EditDocument mergeSettings(const EditDocument &current, const EditDocument &source,
                           std::uint32_t groups, std::span<const std::string> parameters) {
    const auto mask = settingsCorrections(groups);
    current.validate();
    source.validate();
    auto result = current;
    if (!parameters.empty()) {
        parameterSelectionGroups(parameters);
        std::vector<std::pair<std::string, double>> values;
        std::uint32_t selectedCorrections = 0;
        bool whiteBalance = false;
        for (const auto &name : parameters) {
            if (!(settingsParameterGroup(name) & groups))
                continue;
            if (name.starts_with("points.")) {
                const auto channel = EditDocument::curveChannel(std::string_view(name).substr(7));
                result.curves[channel] = source.curves[channel];
                selectedCorrections |= static_cast<std::uint32_t>(Correction::curves);
            } else if (name == "whiteBalance") {
                whiteBalance = true;
                selectedCorrections |= static_cast<std::uint32_t>(Correction::whiteBalance);
            } else {
                values.emplace_back(name, source.parameter(name));
                selectedCorrections |= static_cast<std::uint32_t>(EditDocument::correction(name));
            }
        }
        result.setParameters(values);
        if (whiteBalance)
            result.customWhiteBalance = source.customWhiteBalance;
        result.enabledCorrections = (current.enabledCorrections & ~selectedCorrections) |
                                    (source.enabledCorrections & selectedCorrections);
        result.creative.enabled = (current.creative.enabled & ~selectedCorrections) |
                                  (source.creative.enabled & selectedCorrections);
        result.validate();
        return result;
    }
    if (groups & 1) {
        result.exposure = source.exposure;
        result.contrast = source.contrast;
        result.highlights = source.highlights;
        result.shadows = source.shadows;
        result.whites = source.whites;
        result.blacks = source.blacks;
    }
    if (groups & 2) {
        result.temperature = source.temperature;
        result.tint = source.tint;
        result.customWhiteBalance = source.customWhiteBalance;
        result.vibrance = source.vibrance;
        result.saturation = source.saturation;
    }
    if (groups & 4) {
        result.curveShadows = source.curveShadows;
        result.curveDarks = source.curveDarks;
        result.curveLights = source.curveLights;
        result.curveHighlights = source.curveHighlights;
        result.curves = source.curves;
    }
    if (groups & 8)
        result.hsl = source.hsl;
    if (groups & 16) {
        result.sharpenAmount = source.sharpenAmount;
        result.sharpenRadius = source.sharpenRadius;
        result.sharpenDetail = source.sharpenDetail;
        result.sharpenMasking = source.sharpenMasking;
        result.noiseLuminance = source.noiseLuminance;
        result.noiseLuminanceDetail = source.noiseLuminanceDetail;
        result.noiseLuminanceContrast = source.noiseLuminanceContrast;
        result.noiseColor = source.noiseColor;
        result.noiseColorDetail = source.noiseColorDetail;
        result.noiseColorSmoothness = source.noiseColorSmoothness;
    }
    if (groups & 32)
        result.crop = source.crop;
    if (groups & 64)
        result.lens = source.lens;
    if (groups & 128)
        result.transform = source.transform;
    for (std::size_t i = 0; i < creativeParameters.size(); ++i)
        if (groups & creativeParameters[i].group)
            result.creative.values[i] = source.creative.values[i];
    result.creative.enabled = (current.creative.enabled & ~mask) | (source.creative.enabled & mask);
    result.enabledCorrections =
        (current.enabledCorrections & ~mask) | (source.enabledCorrections & mask);
    result.validate();
    return result;
}
} // namespace luma
