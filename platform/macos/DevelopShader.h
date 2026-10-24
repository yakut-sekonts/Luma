#pragma once
namespace luma::macos {
inline constexpr const char *developShader = R"metal(
#include <metal_stdlib>
using namespace metal;

float encodeColor(float value) {
    const float x = abs(value);
    return copysign(x <= 0.0031308f ? 12.92f * x : 1.055f * pow(x, 1.0f / 2.4f) - 0.055f, value);
}
float decodeColor(float value) {
    const float x = abs(value);
    return copysign(x <= 0.04045f ? x / 12.92f : pow((x + 0.055f) / 1.055f, 2.4f), value);
}
float pointCurve(float value, uint channel, constant float *p) {
    const uint start = 58 + channel * 4097;
    if (value <= 0)
        return p[start] + value * p[50 + channel * 2];
    if (value >= 1)
        return p[start + 4096] + (value - 1) * p[51 + channel * 2];
    const float position = value * 4096;
    const uint index = min(uint(position), 4095u);
    return mix(p[start + index], p[start + index + 1], position - float(index));
}
float parametric(float x, constant float *p) {
    if (x <= 0)
        return x * exp2(p[22] * 0.005f);
    if (x >= 1)
        return 1 + (x - 1) * exp2(-p[25] * 0.005f);
    const float position = clamp((x - 0.125f) * 4, 0.0f, 3.0f);
    const uint index = min(uint(position), 2u);
    const float gain = exp2(mix(p[22 + index], p[23 + index],
                                position - float(index)) * 0.005f);
    return x * gain / (1 - x + x * gain);
}
float wrapHue(float hue) {
    const float result = fmod(hue, 360.0f);
    return result < 0 ? result + 360 : result;
}
float3 applyHsl(float3 rgb, constant float *p) {
    const float offset = min(0.0f, min(rgb.r, min(rgb.g, rgb.b)));
    const float scale = max(1.0f, max(rgb.r, max(rgb.g, rgb.b)) - offset);
    rgb = (rgb - offset) / scale;
    const float high = max(rgb.r, max(rgb.g, rgb.b));
    const float low = min(rgb.r, min(rgb.g, rgb.b));
    const float delta = high - low;
    float light = (high + low) * 0.5f;
    float hue = 0, saturation = 0;
    if (delta > 1e-12f) {
        // Avoid subtracting nearly equal values at black/white. In particular,
        // HDR normalization makes high == 1, where this denominator equals delta.
        saturation = delta / max(1e-12f, min(high + low, (1 - high) + (1 - low)));
        hue = high == rgb.r ? (rgb.g - rgb.b) / delta
            : high == rgb.g ? (rgb.b - rgb.r) / delta + 2
                            : (rgb.r - rgb.g) / delta + 4;
        hue = wrapHue(hue * 60);
    }
    constexpr float centers[9] = {0, 30, 60, 120, 180, 240, 270, 300, 360};
    uint band = 0;
    while (band < 7 && hue >= centers[band + 1])
        ++band;
    const float fraction = (hue - centers[band]) / (centers[band + 1] - centers[band]);
    const uint next = (band + 1) % 8;
    const float3 a(p[26 + band * 3], p[27 + band * 3], p[28 + band * 3]);
    const float3 b(p[26 + next * 3], p[27 + next * 3], p[28 + next * 3]);
    const float3 adjustment = mix(a, b, fraction);
    const float weight = min(1.0f, saturation / 0.1f);
    hue = wrapHue(hue + adjustment.x * weight * 0.3f);
    saturation = clamp(saturation * (1 + adjustment.y * weight / 100), 0.0f, 1.0f);
    light += adjustment.z * weight * 0.005f * (adjustment.z >= 0 ? 1 - light : light);
    const float h = hue / 60;
    const float c = (2 * min(light, 1 - light)) * saturation;
    const float x = c * (1 - abs(fmod(h, 2.0f) - 1));
    const float m = light - c * 0.5f;
    switch (min(uint(h), 5u)) {
    case 0: rgb = float3(c, x, 0); break;
    case 1: rgb = float3(x, c, 0); break;
    case 2: rgb = float3(0, c, x); break;
    case 3: rgb = float3(0, x, c); break;
    case 4: rgb = float3(x, 0, c); break;
    default: rgb = float3(c, 0, x); break;
    }
    return (rgb + m) * scale + offset;
}

kernel void develop(device const float *input [[buffer(0)]],
                         device float *output [[buffer(1)]],
                         constant float *p [[buffer(2)]],
                         constant uint &count [[buffer(3)]],
                         device atomic_uint &invalid [[buffer(4)]],
                         uint i [[thread_position_in_grid]]) {
    if (i >= count)
        return;
    float3 camera = float3(input[i * 3], input[i * 3 + 1], input[i * 3 + 2]);
    if (!all(isfinite(camera))) {
        atomic_store_explicit(&invalid, 1u, memory_order_relaxed);
        return;
    }
    camera *= float3(p[9], p[10], p[11]);
    float3 rgb;
    rgb.r = (p[0] * camera.r + p[1] * camera.g + p[2] * camera.b) * p[12];
    rgb.g = (p[3] * camera.r + p[4] * camera.g + p[5] * camera.b) * p[12];
    rgb.b = (p[6] * camera.r + p[7] * camera.g + p[8] * camera.b) * p[12];
    const float3 weights(0.2126f, 0.7152f, 0.0722f);
    const float luminance = dot(weights, rgb);
    if (luminance > 1e-8f) {
        const float l = log2(luminance / 0.18f);
        const float shadow = 1.0f / (1.0f + exp2(l * 0.5f));
        const float highlight = 1.0f - shadow;
        const float delta = p[13] * 0.5f * tanh(l * 0.5f) +
                            p[14] * shadow * 2.0f + p[15] * highlight * 2.0f +
                            p[16] * shadow * shadow + p[17] * highlight * highlight;
        rgb *= exp2(delta);
    }
    const float gray = dot(weights, rgb);
    const float minimum = min(rgb.r, min(rgb.g, rgb.b));
    const float maximum = max(rgb.r, max(rgb.g, rgb.b));
    const float chroma = clamp((maximum - minimum) / max(1e-6f, abs(maximum)), 0.0f, 1.0f);
    const float gain = max(0.0f, (1.0f + p[18]) * (1.0f + p[19] * (1.0f - chroma)));
    rgb = gray + (rgb - gray) * gain;
    if (p[20] != 0 || p[21] != 0) {
        for (uint channel = 0; channel < 3; ++channel) {
            rgb[channel] = encodeColor(rgb[channel]);
            if (p[20] != 0)
                rgb[channel] = pointCurve(pointCurve(parametric(rgb[channel], p), 0, p),
                                          channel + 1, p);
        }
        if (p[21] != 0)
            rgb = applyHsl(rgb, p);
        for (uint channel = 0; channel < 3; ++channel)
            rgb[channel] = decodeColor(rgb[channel]);
    }
    if (!all(isfinite(rgb))) {
        atomic_store_explicit(&invalid, 1u, memory_order_relaxed);
        return;
    }
    output[i * 3] = rgb.r;
    output[i * 3 + 1] = rgb.g;
    output[i * 3 + 2] = rgb.b;
}
)metal";
} // namespace luma::macos
