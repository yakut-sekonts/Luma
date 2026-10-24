#include "core/color/WhiteBalance.h"
#include "core/image/Bayer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace luma {
namespace {
std::array<double, 9> inverse(const std::array<float, 9> &m) {
    const double d = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                     m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (!std::isfinite(d) || std::abs(d) < 1e-9)
        throw std::runtime_error("Camera matrix cannot support manual WB.");
    return {(m[4] * m[8] - m[5] * m[7]) / d, (m[2] * m[7] - m[1] * m[8]) / d,
            (m[1] * m[5] - m[2] * m[4]) / d, (m[5] * m[6] - m[3] * m[8]) / d,
            (m[0] * m[8] - m[2] * m[6]) / d, (m[2] * m[3] - m[0] * m[5]) / d,
            (m[3] * m[7] - m[4] * m[6]) / d, (m[1] * m[6] - m[0] * m[7]) / d,
            (m[0] * m[4] - m[1] * m[3]) / d};
}
} // namespace
std::array<float, 3> whiteBalanceGains(double temperature, double tint,
                                       const RawColorCalibration &calibration) {
    // Kang et al. 2002 Planckian locus approximation, 2000–25000 K.
    const double t = 1000 / temperature;
    const double x = temperature <= 4000
                         ? -0.2661239 * t * t * t - 0.2343589 * t * t + 0.8776956 * t + 0.179910
                         : -3.0258469 * t * t * t + 2.1070379 * t * t + 0.2226347 * t + 0.240390;
    const double y0 =
        temperature <= 2222
            ? -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683
        : temperature <= 4000
            ? -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867
            : 3.0817580 * x * x * x - 5.87338670 * x * x + 3.75112997 * x - 0.37001483;
    const double y = y0 + tint * 0.0001;
    const double X = x / y, Z = (1 - x - y) / y;
    const std::array<double, 3> rgb{3.2404542 * X - 1.5371385 - 0.4985314 * Z,
                                    -0.9692660 * X + 1.8760108 + 0.0415560 * Z,
                                    0.0556434 * X - 0.2040259 + 1.0572252 * Z};
    const auto m = inverse(calibration.cameraToSRGB);
    std::array<double, 3> neutral{};
    for (std::size_t c = 0; c < 3; ++c)
        neutral[c] =
            std::max(0.01, m[c * 3] * rgb[0] + m[c * 3 + 1] * rgb[1] + m[c * 3 + 2] * rgb[2]);
    return {static_cast<float>(neutral[1] / neutral[0]), 1,
            static_cast<float>(neutral[1] / neutral[2])};
}
WhiteBalanceEstimate estimateWhiteBalance(const std::array<double, 3> &neutral,
                                          const RawColorCalibration &calibration,
                                          CancellationToken stop) {
    for (const auto value : calibration.cameraToSRGB)
        if (!std::isfinite(value))
            throw std::runtime_error("Invalid camera color matrix.");
    const auto m = inverse(calibration.cameraToSRGB);
    std::array<double, 3> desired{};
    for (std::size_t c = 0; c < 3; ++c) {
        // M * diag(gains) * sample = k * (1,1,1). Camera channels need
        // not themselves be equal when the rendered working RGB is neutral.
        const auto white = m[c * 3] + m[c * 3 + 1] + m[c * 3 + 2];
        if (!std::isfinite(neutral[c]) || neutral[c] <= 1e-8 || !std::isfinite(white) ||
            white <= 1e-8)
            throw std::runtime_error("This sample cannot produce a neutral white balance.");
        desired[c] = white / neutral[c];
    }
    const double red = std::log(desired[0] / desired[1]);
    const double blue = std::log(desired[2] / desired[1]);
    const auto error = [&](double mired, double tint) {
        const auto gains = whiteBalanceGains(1'000'000 / mired, tint, calibration);
        const double r = std::log(gains[0] / gains[1]) - red;
        const double b = std::log(gains[2] / gains[1]) - blue;
        return r * r + b * b;
    };
    double best = std::numeric_limits<double>::infinity(), bestMired = 40, bestTint = 0;
    const auto consider = [&](double mired, double tint) {
        mired = std::clamp(mired, 40.0, 500.0);
        tint = std::clamp(tint, -150.0, 150.0);
        const auto candidate = error(mired, tint);
        if (candidate < best) {
            best = candidate;
            bestMired = mired;
            bestTint = tint;
        }
    };
    // Bounded global search then local refinement in reciprocal temperature.
    // This also handles the manual model's channel floor and locus branches.
    for (int t = 0; t <= 64; ++t) {
        checkCancellation(stop);
        for (int tint = 0; tint <= 32; ++tint)
            consider(40 + 460.0 * t / 64, -150 + 300.0 * tint / 32);
    }
    double temperatureStep = 460.0 / 64, tintStep = 300.0 / 32;
    for (int iteration = 0; iteration < 24; ++iteration) {
        checkCancellation(stop);
        const auto center = bestMired, centerTint = bestTint;
        for (int dt = -1; dt <= 1; ++dt)
            for (int di = -1; di <= 1; ++di)
                consider(center + dt * temperatureStep, centerTint + di * tintStep);
        temperatureStep *= 0.5;
        tintStep *= 0.5;
    }
    if (!std::isfinite(best))
        throw std::runtime_error("This sample cannot produce a neutral white balance.");
    WhiteBalanceEstimate result;
    result.temperature = std::clamp(std::round(1'000'000 / bestMired), 2000.0, 25000.0);
    result.tint = std::clamp(std::round(bestTint * 100) / 100, -150.0, 150.0);
    // Report material model/range mismatch; do not claim every arbitrary color
    // can be neutralized by the supported Temperature/Tint rectangle.
    result.limited = error(1'000'000 / result.temperature, result.tint) > 0.03 * 0.03;
    checkCancellation(stop);
    return result;
}
} // namespace luma
