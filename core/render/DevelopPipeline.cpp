#include "core/render/DevelopPipeline.h"
#include "core/color/WhiteBalance.h"
#include "core/render/CreativeColor.h"
#include <algorithm>
#include <cmath>

namespace luma {
namespace {
double amount(const EditDocument &edits, Correction correction, double value) {
    return edits.enabled(correction) ? value / 100 : 0;
}
} // namespace
DevelopPipeline::DevelopPipeline(EditDocument edits, RawColorCalibration calibration)
    : edits_(edits), calibration_(calibration), gains_(calibration.asShotGains),
      curves_{ToneCurve(edits.curves[0]), ToneCurve(edits.curves[1]), ToneCurve(edits.curves[2]),
              ToneCurve(edits.curves[3])} {
    edits.validate();
    creativeColorActive_ = creativeColorActive(edits);
    for (auto value : calibration.cameraToSRGB)
        if (!std::isfinite(value))
            throw std::runtime_error("Invalid camera color matrix.");
    for (auto value : gains_)
        if (!std::isfinite(value) || value <= 0)
            throw std::runtime_error("Invalid As Shot WB.");
    if (edits.customWhiteBalance && edits.enabled(Correction::whiteBalance))
        gains_ = whiteBalanceGains(edits.temperature, edits.tint, calibration);
    exposureGain_ =
        static_cast<float>(std::exp2(edits.enabled(Correction::exposure) ? edits.exposure : 0));
    curvesActive_ = edits.enabled(Correction::curves) &&
                    (edits.curveShadows != 0 || edits.curveDarks != 0 || edits.curveLights != 0 ||
                     edits.curveHighlights != 0 ||
                     std::any_of(edits.curves.begin(), edits.curves.end(),
                                 [](const PointCurve &curve) { return curve != PointCurve{}; }));
    hslActive_ = edits.enabled(Correction::hsl) &&
                 std::any_of(edits.hsl.begin(), edits.hsl.end(),
                             [](const HSLBand &band) { return band != HSLBand{}; });
}
std::vector<float> DevelopPipeline::gpuParameters() const {
    std::vector<float> parameters(58 + 4 * 4097);
    std::copy(calibration_.cameraToSRGB.begin(), calibration_.cameraToSRGB.end(),
              parameters.begin());
    std::copy(gains_.begin(), gains_.end(), parameters.begin() + 9);
    parameters[12] = exposureGain_;
    parameters[13] = static_cast<float>(amount(edits_, Correction::contrast, edits_.contrast));
    parameters[14] = static_cast<float>(amount(edits_, Correction::shadows, edits_.shadows));
    parameters[15] = static_cast<float>(amount(edits_, Correction::highlights, edits_.highlights));
    parameters[16] = static_cast<float>(amount(edits_, Correction::blacks, edits_.blacks));
    parameters[17] = static_cast<float>(amount(edits_, Correction::whites, edits_.whites));
    parameters[18] = static_cast<float>(amount(edits_, Correction::saturation, edits_.saturation));
    parameters[19] = static_cast<float>(amount(edits_, Correction::vibrance, edits_.vibrance));
    parameters[20] = curvesActive_ ? 1 : 0;
    parameters[21] = hslActive_ ? 1 : 0;
    parameters[22] = static_cast<float>(edits_.curveShadows);
    parameters[23] = static_cast<float>(edits_.curveDarks);
    parameters[24] = static_cast<float>(edits_.curveLights);
    parameters[25] = static_cast<float>(edits_.curveHighlights);
    for (std::size_t i = 0; i < edits_.hsl.size(); ++i) {
        parameters[26 + i * 3] = static_cast<float>(edits_.hsl[i].hue);
        parameters[27 + i * 3] = static_cast<float>(edits_.hsl[i].saturation);
        parameters[28 + i * 3] = static_cast<float>(edits_.hsl[i].luminance);
    }
    for (std::size_t i = 0; i < curves_.size(); ++i) {
        parameters[50 + i * 2] = static_cast<float>(curves_[i].startSlope());
        parameters[51 + i * 2] = static_cast<float>(curves_[i].endSlope());
        const auto samples = curves_[i].samples();
        std::transform(samples.begin(), samples.end(), parameters.begin() + 58 + i * 4097,
                       [](double value) { return static_cast<float>(value); });
    }
    return parameters;
}
std::array<float, 3> DevelopPipeline::pixel(std::array<float, 3> camera) const {
    for (std::size_t c = 0; c < 3; ++c) {
        if (!std::isfinite(camera[c]))
            throw std::runtime_error("Non-finite input pixel.");
        camera[c] *= gains_[c];
    }
    std::array<float, 3> rgb{};
    const auto &m = calibration_.cameraToSRGB;
    for (std::size_t c = 0; c < 3; ++c)
        rgb[c] = (m[c * 3] * camera[0] + m[c * 3 + 1] * camera[1] + m[c * 3 + 2] * camera[2]) *
                 exposureGain_;
    const double luminance = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
    const double contrast = amount(edits_, Correction::contrast, edits_.contrast);
    const double shadows = amount(edits_, Correction::shadows, edits_.shadows);
    const double highlights = amount(edits_, Correction::highlights, edits_.highlights);
    const double blacks = amount(edits_, Correction::blacks, edits_.blacks);
    const double whites = amount(edits_, Correction::whites, edits_.whites);
    // Neutral tone has an exact gain of one. In particular, spatial Presence
    // alone must not evaluate logarithms/exponentials twice per source pixel.
    if (luminance > 1e-8 &&
        (contrast != 0 || shadows != 0 || highlights != 0 || blacks != 0 || whites != 0)) {
        // Smooth log-domain luminance remapping preserves chromatic ratios and HDR.
        const double l = std::log2(luminance / 0.18);
        const double shadowWeight = 1 / (1 + std::exp2(l / 2));
        const double highlightWeight = 1 - shadowWeight;
        const double blackWeight = shadowWeight * shadowWeight;
        const double whiteWeight = highlightWeight * highlightWeight;
        const double delta = contrast * 0.5 * std::tanh(l / 2) + shadows * shadowWeight * 2 +
                             highlights * highlightWeight * 2 + blacks * blackWeight +
                             whites * whiteWeight;
        const auto gain = static_cast<float>(std::exp2(delta));
        for (auto &value : rgb)
            value *= gain;
    }
    const double gray = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
    const auto [minimum, maximum] = std::minmax_element(rgb.begin(), rgb.end());
    const double chroma =
        std::clamp((*maximum - *minimum) / std::max(1e-6F, std::abs(*maximum)), 0.0F, 1.0F);
    const double saturation = amount(edits_, Correction::saturation, edits_.saturation);
    const double vibrance = amount(edits_, Correction::vibrance, edits_.vibrance);
    const double gain = std::max(0.0, (1 + saturation) * (1 + vibrance * (1 - chroma)));
    for (auto &value : rgb)
        value = static_cast<float>(gray + (value - gray) * gain);
    if (curvesActive_ || hslActive_) {
        std::array<double, 3> encoded{};
        for (std::size_t c = 0; c < 3; ++c) {
            encoded[c] = encodeSRGB(rgb[c]);
            if (curvesActive_)
                encoded[c] =
                    curves_[c + 1].value(curves_[0].value(parametricCurve(encoded[c], edits_)));
        }
        if (hslActive_)
            encoded = mixHSL(encoded, edits_.hsl);
        for (std::size_t c = 0; c < 3; ++c)
            rgb[c] = static_cast<float>(decodeSRGB(encoded[c]));
    }
    return creativeColorActive_ ? applyCreativeColor(rgb, edits_) : rgb;
}
} // namespace luma
