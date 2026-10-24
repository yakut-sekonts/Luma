#include "core/render/AutoAdjust.h"
#include "core/image/Bayer.h"
#include "core/render/AdvancedColor.h"
#include "core/render/CpuRenderer.h"
#include <algorithm>
#include <cmath>
namespace luma {
EditDocument HistogramAutoAdjust::estimate(const FloatImage &image, const EditDocument &current,
                                           const PhotoMetadata &metadata,
                                           RawColorCalibration calibration,
                                           CancellationToken stop) const {
    auto base = current;
    base.exposure = base.contrast = base.highlights = base.shadows = base.whites = base.blacks =
        base.vibrance = 0;
    for (auto c :
         {Correction::exposure, Correction::contrast, Correction::highlights, Correction::shadows,
          Correction::whites, Correction::blacks, Correction::vibrance})
        base.setEnabled(c, true);
    const auto small = standardPreview(image, 512, stop);
    const auto output =
        renderCPU(small, base, metadata.orientation, {}, stop, calibration, DetailDiagnostic::none,
                  static_cast<double>(small.width) / std::max(1, metadata.width), metadata, {},
                  nullptr, {true});
    if (!output.histogram || output.histogram->weight < 1)
        throw std::runtime_error("Not enough visible pixels for Auto Adjust.");
    const auto &histogram = *output.histogram;
    const auto percentile = [&](double p) {
        double sum = 0;
        for (std::size_t i = 0; i < 256; ++i) {
            sum += histogram.bins[3][i];
            if (sum >= p * histogram.weight)
                return decodeSRGB(static_cast<double>(i) / 255);
        }
        return 1.0;
    };
    const double p01 = percentile(.01), p10 = percentile(.10), median = percentile(.5),
                 p90 = percentile(.9), p99 = percentile(.99);
    if (median <= 1e-5 || p99 - p01 < 1e-5)
        throw std::runtime_error("Auto Adjust needs a scene with usable tonal variation.");
    base.exposure = std::clamp(std::log2(.18 / median), -4.0, 4.0);
    const double gain = std::exp2(base.exposure);
    base.contrast =
        std::clamp((.65 - (encodeSRGB(p90 * gain) - encodeSRGB(p10 * gain))) * 70, -35.0, 35.0);
    base.highlights = std::clamp(-50 * std::log2(std::max(1.0, p99 * gain / .9)), -100.0, 0.0);
    base.shadows = std::clamp((.06 - p10 * gain) * 400, -40.0, 60.0);
    base.whites = std::clamp((.95 - p99 * gain) * 30, -40.0, 30.0);
    base.blacks = std::clamp((.005 - p01 * gain) * 400, -40.0, 15.0);
    double saturation = 0, weight = 0;
    for (std::size_t i = 0; i < output.rgba.size(); i += 4) {
        const double a = output.rgba[i + 3] / 255.0;
        const auto color =
            toHSL({output.rgba[i] / 255.0, output.rgba[i + 1] / 255.0, output.rgba[i + 2] / 255.0});
        saturation += color.saturation * a;
        weight += a;
    }
    base.vibrance = weight > 0 ? std::clamp((.35 - saturation / weight) * 40, -10.0, 15.0) : 0;
    base.validate();
    checkCancellation(stop);
    return base;
}
} // namespace luma
