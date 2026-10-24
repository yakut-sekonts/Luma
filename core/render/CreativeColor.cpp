#include "core/render/CreativeColor.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace luma {
namespace {
double smooth(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}
HSLColor encodedHSL(const std::array<float, 3> &rgb) {
    return toHSL({std::clamp(encodeSRGB(rgb[0]), 0.0, 1.0),
                  std::clamp(encodeSRGB(rgb[1]), 0.0, 1.0),
                  std::clamp(encodeSRGB(rgb[2]), 0.0, 1.0)});
}
double noise(int x, int y) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x9e3779b9U ^
                      static_cast<std::uint32_t>(y) * 0x85ebca6bU ^ 0x4c554d41U;
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return static_cast<double>(h) / 4294967295.0 * 2 - 1;
}
double grain(double x, double y) {
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const double fx = smooth(x - ix), fy = smooth(y - iy);
    return std::lerp(std::lerp(noise(ix, iy), noise(ix + 1, iy), fx),
                     std::lerp(noise(ix, iy + 1), noise(ix + 1, iy + 1), fx), fy);
}
} // namespace
bool creativeColorActive(const EditDocument &e) {
    const auto &v = e.creative.values;
    return (e.enabled(Correction::pointColor) && (v[9] != 0 || v[10] != 0 || v[11] != 0)) ||
           (e.enabled(Correction::grading) &&
            (v[13] != 0 || v[14] != 0 || v[16] != 0 || v[17] != 0 || v[19] != 0 || v[20] != 0 ||
             v[22] != 0 || v[23] != 0));
}
double pointColorWeight(const std::array<float, 3> &rgb, const CreativeSettings &s) {
    const auto hsl = encodedHSL(rgb);
    const auto &v = s.values;
    double dh = std::abs(hsl.hue - v[3]);
    dh = std::min(dh, 360 - dh);
    const double h = 1 - smooth(dh / v[6]);
    const double saturation = 1 - smooth(std::abs(hsl.saturation * 100 - v[4]) / v[7]);
    const double lightness = 1 - smooth(std::abs(hsl.luminance * 100 - v[5]) / v[8]);
    // Hue is undefined for achromatic pixels; only sample them for a neutral target.
    return (v[4] < 1 ? 1 : h * smooth(hsl.saturation / .02)) * saturation * lightness;
}
std::array<float, 3> applyCreativeColor(std::array<float, 3> rgb, const EditDocument &e) {
    const auto &v = e.creative.values;
    if (e.enabled(Correction::pointColor) && (v[9] != 0 || v[10] != 0 || v[11] != 0)) {
        const double w = pointColorWeight(rgb, e.creative);
        if (w > 0) {
            const auto original = encodedHSL(rgb);
            auto adjusted = original;
            adjusted.hue = std::fmod(original.hue + v[9] * w + 360, 360.0);
            adjusted.saturation = std::clamp(original.saturation + v[10] / 100 * w, 0.0, 1.0);
            adjusted.luminance = std::clamp(original.luminance + v[11] / 100 * w, 0.0, 1.0);
            const auto a = fromHSL(original), b = fromHSL(adjusted);
            // Add the bounded color delta, retaining original negative/HDR residuals.
            for (std::size_t c = 0; c < 3; ++c)
                rgb[c] += static_cast<float>(decodeSRGB(b[c]) - decodeSRGB(a[c]));
        }
    }
    if (e.enabled(Correction::grading)) {
        const double luminance = std::max(0.0, .2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2]);
        const double tone = std::clamp(encodeSRGB(luminance) - v[25] / 250, 0.0, 1.0);
        const double exponent = 4 - v[24] * .035;
        std::array<double, 4> weights{std::pow(1 - tone, exponent),
                                      std::pow(std::max(0.0, 1 - std::abs(2 * tone - 1)), exponent),
                                      std::pow(tone, exponent), 1};
        const double sum = weights[0] + weights[1] + weights[2];
        for (int i = 0; i < 3; ++i)
            weights[i] /= sum;
        double exposure = 0;
        std::array<double, 3> tint{};
        for (int i = 0; i < 4; ++i) {
            const auto base = 12 + i * 3;
            const double weight = weights[i];
            exposure += weight * v[base + 2] / 100;
            const auto color = fromHSL({v[base], 1, .5});
            const double y = .2126 * color[0] + .7152 * color[1] + .0722 * color[2];
            for (std::size_t c = 0; c < 3; ++c)
                tint[c] += weight * v[base + 1] / 100 * (color[c] - y);
        }
        const double envelope = std::min(.25, std::max(0.0, luminance)) * .65;
        for (std::size_t c = 0; c < 3; ++c)
            rgb[c] = static_cast<float>((rgb[c] + envelope * tint[c]) * std::exp2(exposure));
    }
    return rgb;
}
std::array<float, 3> applyOutputEffects(std::array<float, 3> rgb, const EditDocument &e, int x,
                                        int y, int width, int height, double scale) {
    const auto &v = e.creative.values;
    if (e.enabled(Correction::vignette) && v[26] != 0) {
        const double nx = std::abs(2 * (x + .5) / width - 1),
                     ny = std::abs(2 * (y + .5) / height - 1);
        const double power = std::exp2(-v[28] / 75) * 2;
        const double radius = std::pow((std::pow(nx, power) + std::pow(ny, power)) / 2, 1 / power);
        const double start = v[27] / 125;
        double edge = smooth((radius - start) / std::max(.01, v[29] / 100 * (1 - start)));
        if (v[26] < 0)
            edge *= 1 - v[30] / 100 * smooth(std::max({rgb[0], rgb[1], rgb[2]}) / .9);
        const double gain = std::exp2(v[26] / 50 * edge);
        for (auto &c : rgb)
            c = static_cast<float>(c * gain);
    }
    if (e.enabled(Correction::grain) && v[31] > 0) {
        scale = std::clamp(scale, 1e-6, 1.0);
        const double size = .8 + v[32] / 20, gx = (x + .5) / (scale * size),
                     gy = (y + .5) / (scale * size);
        const double rough = v[33] / 100;
        const double n = std::lerp(grain(gx, gy), grain(gx * 2.13, gy * 2.13), rough * .6);
        const double amount = v[31] / 100 * .12 * std::sqrt(std::min(1.0, scale * size));
        for (auto &c : rgb) {
            const double encoded = encodeSRGB(c);
            c = static_cast<float>(decodeSRGB(
                encoded + n * amount * std::sqrt(std::clamp(encoded * (1 - encoded), 0.0, .25))));
        }
    }
    return rgb;
}
} // namespace luma
