#include "core/render/WhiteBalanceSampler.h"
#include "core/image/Bayer.h"
#include "core/lens/LensCorrection.h"
#include "core/render/CropGeometry.h"
#include "core/render/RetouchPass.h"
#include <algorithm>
#include <cmath>

namespace luma {
WhiteBalanceEstimate sampleWhiteBalance(const FloatImage &source, const EditDocument &edits,
                                        const PhotoMetadata &metadata,
                                        const RawColorCalibration &calibration,
                                        std::optional<std::array<double, 2>> point,
                                        CancellationToken stop) {
    checkCancellation(stop);
    source.validateCoverage();
    edits.validate();
    if (point)
        for (const auto coordinate : *point)
            if (!std::isfinite(coordinate) || coordinate < 0 || coordinate > 1)
                throw std::runtime_error("Invalid white balance sample position.");
    for (const auto gain : calibration.asShotGains)
        if (!std::isfinite(gain) || gain <= 0)
            throw std::runtime_error("Invalid As Shot WB.");
    for (const auto value : calibration.cameraToSRGB)
        if (!std::isfinite(value))
            throw std::runtime_error("Invalid camera color matrix.");
    const LensCorrectionPass optics(source, edits, metadata, stop);
    const RetouchPass retouch(optics.image(), edits.retouch, stop);
    const auto &image = retouch.image();
    const CropGeometry geometry(image.width, image.height, metadata.orientation, edits);
    struct Sample {
        std::array<double, 3> rgb{};
        double coverage = 0;
    };
    const auto sample = [&](double x, double y) {
        Sample result;
        if (!geometry.covered(x, y))
            return result;
        const auto position = geometry.source(x, y);
        const auto sx = std::clamp(position[0], 0.0, static_cast<double>(image.width - 1));
        const auto sy = std::clamp(position[1], 0.0, static_cast<double>(image.height - 1));
        const int x0 = static_cast<int>(std::floor(sx)), y0 = static_cast<int>(std::floor(sy));
        const double fx = sx - x0, fy = sy - y0;
        const std::array<double, 4> weights{(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy,
                                            fx * fy};
        for (int i = 0; i < 4; ++i) {
            const int px = std::min(x0 + (i & 1), image.width - 1);
            const int py = std::min(y0 + (i >> 1), image.height - 1);
            const auto weight = weights[i] * image.coverage(px, py);
            if (weight <= 0)
                continue;
            const auto offset = (static_cast<std::size_t>(py) * image.width + px) * 3;
            for (std::size_t c = 0; c < 3; ++c) {
                if (!std::isfinite(image.rgb[offset + c]))
                    return Sample{};
                result.rgb[c] += weight * image.rgb[offset + c];
            }
            result.coverage += weight;
        }
        if (result.coverage <= 1e-8)
            return Sample{};
        for (auto &channel : result.rgb) {
            channel /= result.coverage;
            if (channel <= 1e-8)
                return Sample{};
        }
        // Reject unreliable shadows/highlights in As Shot working RGB. Current
        // user exposure/WB/tone/local adjustments never affect sample admission.
        const auto &m = calibration.cameraToSRGB;
        for (std::size_t c = 0; c < 3; ++c) {
            double working = 0;
            for (std::size_t k = 0; k < 3; ++k)
                working += m[c * 3 + k] * result.rgb[k] * calibration.asShotGains[k];
            if (!std::isfinite(working) || working < 0.002 || working >= 0.98)
                return Sample{};
        }
        return result;
    };
    std::array<double, 3> sum{};
    double weight = 0;
    int accepted = 0;
    const auto accumulate = [&](const Sample &value) {
        if (value.coverage <= 1e-8)
            return;
        for (std::size_t c = 0; c < 3; ++c)
            sum[c] += value.rgb[c] * value.coverage;
        weight += value.coverage;
        ++accepted;
    };
    int required = 1;
    if (point) {
        const double cx = std::clamp((*point)[0] * geometry.width() - .5, 0.0,
                                     static_cast<double>(geometry.width() - 1));
        const double cy = std::clamp((*point)[1] * geometry.height() - .5, 0.0,
                                     static_cast<double>(geometry.height() - 1));
        // An invalid center must not silently select an opaque neighbor.
        if (sample(cx, cy).coverage <= 1e-8)
            throw std::runtime_error(
                "Choose a midtone neutral area away from transparency and clipping.");
        for (int dy = -2; dy <= 2; ++dy) {
            checkCancellation(stop);
            for (int dx = -2; dx <= 2; ++dx) {
                const double x = cx + dx, y = cy + dy;
                if (x >= 0 && y >= 0 && x <= geometry.width() - 1 && y <= geometry.height() - 1)
                    accumulate(sample(x, y));
            }
        }
    } else {
        const int columns = std::min(192, geometry.width()),
                  rows = std::min(192, geometry.height());
        required = std::min(16, columns * rows);
        for (int y = 0; y < rows; ++y) {
            checkCancellation(stop);
            for (int x = 0; x < columns; ++x)
                accumulate(sample((x + .5) * geometry.width() / columns - .5,
                                  (y + .5) * geometry.height() / rows - .5));
        }
    }
    if (accepted < required || weight < required * 0.1)
        throw std::runtime_error("Not enough usable pixels to estimate white balance.");
    for (auto &channel : sum)
        channel /= weight;
    return estimateWhiteBalance(sum, calibration, stop);
}
} // namespace luma
