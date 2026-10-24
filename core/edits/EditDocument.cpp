#include "core/edits/EditDocument.h"
#include <array>
#include <string>

namespace luma {
namespace {
struct Parameter {
    std::string_view name;
    double EditDocument::*member;
    double minimum;
    double maximum;
    Correction correction;
};
constexpr std::array parameters{
    Parameter{"exposure", &EditDocument::exposure, -5, 5, Correction::exposure},
    Parameter{"temperature", &EditDocument::temperature, 2000, 25000, Correction::whiteBalance},
    Parameter{"tint", &EditDocument::tint, -150, 150, Correction::whiteBalance},
    Parameter{"contrast", &EditDocument::contrast, -100, 100, Correction::contrast},
    Parameter{"highlights", &EditDocument::highlights, -100, 100, Correction::highlights},
    Parameter{"shadows", &EditDocument::shadows, -100, 100, Correction::shadows},
    Parameter{"whites", &EditDocument::whites, -100, 100, Correction::whites},
    Parameter{"blacks", &EditDocument::blacks, -100, 100, Correction::blacks},
    Parameter{"vibrance", &EditDocument::vibrance, -100, 100, Correction::vibrance},
    Parameter{"saturation", &EditDocument::saturation, -100, 100, Correction::saturation},
    Parameter{"curve.shadows", &EditDocument::curveShadows, -100, 100, Correction::curves},
    Parameter{"curve.darks", &EditDocument::curveDarks, -100, 100, Correction::curves},
    Parameter{"curve.lights", &EditDocument::curveLights, -100, 100, Correction::curves},
    Parameter{"curve.highlights", &EditDocument::curveHighlights, -100, 100, Correction::curves},
    Parameter{"sharpen.amount", &EditDocument::sharpenAmount, 0, 150, Correction::sharpening},
    Parameter{"sharpen.radius", &EditDocument::sharpenRadius, 0.5, 3, Correction::sharpening},
    Parameter{"sharpen.detail", &EditDocument::sharpenDetail, 0, 100, Correction::sharpening},
    Parameter{"sharpen.masking", &EditDocument::sharpenMasking, 0, 100, Correction::sharpening},
    Parameter{"noise.luminance", &EditDocument::noiseLuminance, 0, 100, Correction::luminanceNoise},
    Parameter{"noise.luminanceDetail", &EditDocument::noiseLuminanceDetail, 0, 100,
              Correction::luminanceNoise},
    Parameter{"noise.luminanceContrast", &EditDocument::noiseLuminanceContrast, 0, 100,
              Correction::luminanceNoise},
    Parameter{"noise.color", &EditDocument::noiseColor, 0, 100, Correction::colorNoise},
    Parameter{"noise.colorDetail", &EditDocument::noiseColorDetail, 0, 100, Correction::colorNoise},
    Parameter{"noise.colorSmoothness", &EditDocument::noiseColorSmoothness, 0, 100,
              Correction::colorNoise}};
const Parameter &find(std::string_view name) {
    for (const auto &parameter : parameters) {
        if (parameter.name == name)
            return parameter;
    }
    throw std::runtime_error("Unknown edit parameter: " + std::string(name));
}
double HSLBand::*hslMember(std::string_view name) {
    if (name == "hue")
        return &HSLBand::hue;
    if (name == "saturation")
        return &HSLBand::saturation;
    if (name == "luminance")
        return &HSLBand::luminance;
    throw std::runtime_error("Unknown HSL parameter.");
}
std::pair<std::size_t, double HSLBand::*> hslParameter(std::string_view name) {
    const auto separator = name.find('.', 4);
    if (separator == std::string_view::npos)
        throw std::runtime_error("Invalid HSL parameter.");
    return {EditDocument::hslBand(name.substr(4, separator - 4)),
            hslMember(name.substr(separator + 1))};
}
} // namespace
void PointCurve::validate() const {
    if (points.size() < 2 || points.size() > 32 || points.front().x != 0 || points.back().x != 1)
        throw std::runtime_error("Curve requires 2–32 points with fixed x endpoints at 0 and 1.");
    double previous = -1;
    for (const auto &point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 || point.x > 1 ||
            point.y < 0 || point.y > 1 || point.x - previous < 0.001 - 1e-12)
            throw std::runtime_error(
                "Curve points must be finite, bounded and separated by at least 0.001.");
        previous = point.x;
    }
}
void EditDocument::validate() const {
    validateLocalMasks(localMasks);
    validateRetouch(retouch);
    creative.validate();
    for (const auto &parameter : parameters) {
        const auto value = this->*parameter.member;
        if (!std::isfinite(value) || value < parameter.minimum || value > parameter.maximum)
            throw std::runtime_error("Invalid edit parameter: " + std::string(parameter.name));
    }
    if ((enabledCorrections & ~131071U) != 0)
        throw std::runtime_error("Unknown enabled correction flags.");
    crop.validate();
    lens.validate();
    transform.validate();
    for (const auto &curve : curves)
        curve.validate();
    for (const auto &band : hsl) {
        for (double value : {band.hue, band.saturation, band.luminance})
            if (!std::isfinite(value) || value < -100 || value > 100)
                throw std::runtime_error("HSL values must be finite and within -100…100.");
    }
}
double EditDocument::parameter(std::string_view name) const {
    if (isCreativeParameter(name))
        return creative.parameter(name);
    if (name.starts_with("transform."))
        return transform.parameter(name);
    if (name.starts_with("lens."))
        return lens.parameter(name);
    if (name.starts_with("crop."))
        return crop.parameter(name);
    if (name.starts_with("hsl.")) {
        const auto [band, member] = hslParameter(name);
        return hsl[band].*member;
    }
    return this->*find(name).member;
}
void EditDocument::setParameter(std::string_view name, double value) {
    setParameters({{std::string(name), value}});
}
void EditDocument::setParameters(const std::vector<std::pair<std::string, double>> &values) {
    auto next = *this;
    std::vector<std::pair<std::string, double>> cropValues, transformValues;
    for (const auto &[name, value] : values) {
        if (isCreativeParameter(name))
            next.creative.setParameter(name, value);
        else if (name.starts_with("transform."))
            transformValues.emplace_back(name, value);
        else if (name.starts_with("lens."))
            next.lens.setParameter(name, value);
        else if (name.starts_with("crop."))
            cropValues.emplace_back(name, value);
        else if (name.starts_with("hsl.")) {
            const auto [band, member] = hslParameter(name);
            next.hsl[band].*member = value;
        } else
            next.*find(name).member = value;
        if (name == "temperature" || name == "tint")
            next.customWhiteBalance = true;
    }
    next.crop.setParameters(cropValues);
    next.transform.setParameters(transformValues);
    next.validate();
    *this = next;
}
Correction EditDocument::correction(std::string_view name) {
    if (isCreativeParameter(name))
        return static_cast<Correction>(creativeParameter(name).correction);
    if (name == "presence")
        return Correction::presence;
    if (name == "pointColor")
        return Correction::pointColor;
    if (name == "grading")
        return Correction::grading;
    if (name == "vignette")
        return Correction::vignette;
    if (name == "grain")
        return Correction::grain;
    if (name == "transform")
        return Correction::transform;
    if (name.starts_with("transform.")) {
        TransformSettings{}.parameter(name);
        return Correction::transform;
    }
    if (name == "lens")
        return Correction::lens;
    if (name.starts_with("lens.")) {
        LensSettings{}.parameter(name);
        return Correction::lens;
    }
    if (name == "crop")
        return Correction::crop;
    if (name.starts_with("crop.")) {
        CropSettings{}.parameter(name);
        return Correction::crop;
    }
    if (name == "luminanceNoise")
        return Correction::luminanceNoise;
    if (name == "colorNoise")
        return Correction::colorNoise;
    if (name == "curves")
        return Correction::curves;
    if (name == "hsl")
        return Correction::hsl;
    if (name == "sharpening")
        return Correction::sharpening;
    if (name.starts_with("hsl.")) {
        hslParameter(name);
        return Correction::hsl;
    }
    return name == "whiteBalance" ? Correction::whiteBalance : find(name).correction;
}
std::size_t EditDocument::curveChannel(std::string_view name) {
    for (std::size_t i = 0; i < curveChannels.size(); ++i)
        if (curveChannels[i] == name)
            return i;
    throw std::runtime_error("Unknown curve channel.");
}
std::size_t EditDocument::hslBand(std::string_view name) {
    for (std::size_t i = 0; i < hslBands.size(); ++i)
        if (hslBands[i] == name)
            return i;
    throw std::runtime_error("Unknown HSL band.");
}
} // namespace luma
