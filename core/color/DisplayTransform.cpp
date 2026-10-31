#include "core/color/DisplayTransform.h"
#include <algorithm>
#include <cmath>
#include <lcms2.h>
#include <limits>
#include <stdexcept>

namespace luma {
DisplayTransform::DisplayTransform(std::span<const std::uint8_t> profile) {
    cmsCIExyY white{0.3127, 0.3290, 1};
    cmsCIExyYTRIPLE primaries{{0.64, 0.33, 1}, {0.30, 0.60, 1}, {0.15, 0.06, 1}};
    cmsToneCurve *gamma = cmsBuildGamma(nullptr, 1);
    if (!gamma) {
        throw std::runtime_error("Cannot allocate a linear ICC curve.");
    }
    cmsToneCurve *curves[]{gamma, gamma, gamma};
    auto input = cmsCreateRGBProfile(&white, &primaries, curves);
    cmsFreeToneCurve(gamma);
    auto output =
        profile.empty()
            ? cmsCreate_sRGBProfile()
            : cmsOpenProfileFromMem(profile.data(), static_cast<cmsUInt32Number>(profile.size()));
    if (input && output) {
        transform_ =
            cmsCreateTransform(input, TYPE_RGB_FLT, output, TYPE_RGBA_8,
                               INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_BLACKPOINTCOMPENSATION);
    }
    if (input) {
        cmsCloseProfile(input);
    }
    if (output) {
        cmsCloseProfile(output);
    }
    if (!transform_) {
        throw std::runtime_error("Cannot create the ICC display transform.");
    }
}
DisplayTransform::~DisplayTransform() {
    if (transform_) {
        cmsDeleteTransform(transform_);
    }
}
void DisplayTransform::convert(std::span<const float> rgb, std::span<std::uint8_t> rgba,
                               DisplayMapping mapping) {
    if (rgb.size() % 3 != 0 || rgba.size() != rgb.size() / 3 * 4 ||
        rgb.size() / 3 > std::numeric_limits<cmsUInt32Number>::max() ||
        (mapping != DisplayMapping::clip && mapping != DisplayMapping::adaptiveHDR)) {
        throw std::runtime_error("Invalid display transform inputs.");
    }
    if (mapping == DisplayMapping::adaptiveHDR) {
        constexpr float shoulder = .7F, range = 1 - shoulder;
        mapped_.assign(rgb.begin(), rgb.end());
        for (std::size_t index = 0; index < mapped_.size(); index += 3) {
            const float peak =
                std::max({0.F, mapped_[index], mapped_[index + 1], mapped_[index + 2]});
            if (peak <= shoulder)
                continue;
            const float compressed = shoulder + range * (1 - std::exp(-(peak - shoulder) / range));
            const float scale = compressed / peak;
            for (std::size_t channel = 0; channel < 3; ++channel)
                mapped_[index + channel] *= scale;
        }
        rgb = mapped_;
    }
    cmsDoTransform(transform_, rgb.data(), rgba.data(),
                   static_cast<cmsUInt32Number>(rgb.size() / 3));
    for (std::size_t index = 3; index < rgba.size(); index += 4) {
        rgba[index] = 255;
    }
}
} // namespace luma
