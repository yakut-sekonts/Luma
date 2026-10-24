#include "core/render/MaskReference.h"
#include "core/image/Bayer.h"
#include "core/lens/LensCorrection.h"
#include "core/render/DevelopPipeline.h"
#include "core/render/NoiseReduction.h"
#include "core/render/Presence.h"
#include "core/render/RetouchPass.h"
#include <algorithm>
#include <cmath>
namespace luma {
std::array<float, 3> sampleMaskReference(const FloatImage &source, const EditDocument &edits,
                                         const PhotoMetadata &metadata,
                                         RawColorCalibration calibration, double x, double y,
                                         double detailScale, CancellationToken stop) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1)
        throw std::runtime_error("Range sample is outside the photo.");
    source.validateCoverage();
    edits.validate();
    checkCancellation(stop);
    const LensCorrectionPass optics(source, edits, metadata, stop);
    const RetouchPass retouch(optics.image(), edits.retouch, stop);
    const auto &image = retouch.image();
    const DevelopPipeline pipeline(edits, calibration);
    const NoiseReductionPass noise(image, pipeline, edits, stop, detailScale);
    const SharpeningPass sharpening(image, pipeline, edits, stop, detailScale,
                                    DetailDiagnostic::none, {}, &noise);
    const auto detailPixel = [&](int sx, int sy) {
        const auto i = (static_cast<std::size_t>(sy) * image.width + sx) * 3;
        const auto developed =
            noise.active() ? noise.pixel({}, sx, sy)
                           : pipeline.pixel({image.rgb[i], image.rgb[i + 1], image.rgb[i + 2]});
        return sharpening.pixel(developed, sx, sy);
    };
    const PresencePass presence(image, edits, detailPixel, stop, detailScale);
    const double px = std::clamp(x * image.width - .5, 0.0, image.width - 1.0),
                 py = std::clamp(y * image.height - .5, 0.0, image.height - 1.0);
    const int sx = static_cast<int>(std::floor(px)), sy = static_cast<int>(std::floor(py));
    const double fx = px - sx, fy = py - sy;
    std::array<double, 3> sum{};
    double alpha = 0;
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
            checkCancellation(stop);
            const int ix = std::min(sx + dx, image.width - 1),
                      iy = std::min(sy + dy, image.height - 1);
            const double weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) * image.coverage(ix, iy);
            if (weight == 0)
                continue;
            const auto color = presence.pixel(detailPixel(ix, iy), ix, iy);
            alpha += weight;
            for (std::size_t c = 0; c < 3; ++c)
                sum[c] += color[c] * weight;
        }
    if (alpha <= 1e-8)
        throw std::runtime_error("Cannot sample a transparent pixel.");
    return {static_cast<float>(sum[0] / alpha), static_cast<float>(sum[1] / alpha),
            static_cast<float>(sum[2] / alpha)};
}
} // namespace luma
