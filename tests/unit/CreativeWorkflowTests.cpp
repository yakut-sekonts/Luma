#include "core/catalog/Identity.h"
#include "core/edits/EditSnapshot.h"
#include "core/quick/QuickLooks.h"
#include "core/render/AutoAdjust.h"
#include "core/render/CreativeColor.h"
#include "core/render/Enhance.h"
#include "core/render/Presence.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <typename F> void rejects(F operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Invalid input was accepted.");
}
void settingsAndSnapshots() {
    luma::EditDocument edits;
    for (const auto &parameter : luma::creativeParameters) {
        for (double value : {parameter.minimum, parameter.maximum}) {
            edits.setParameter(parameter.name, value);
            require(edits.parameter(parameter.name) == value,
                    "Creative boundary was not retained.");
            require(luma::decodeEditSnapshot(luma::encodeEditSnapshot(edits)) == edits,
                    "Snapshot lost creative or legacy settings.");
        }
        rejects([&] { edits.setParameter(parameter.name, parameter.minimum - 1); });
        rejects([&] { edits.setParameter(parameter.name, parameter.maximum + 1); });
        rejects(
            [&] { edits.setParameter(parameter.name, std::numeric_limits<double>::quiet_NaN()); });
        edits.setParameter(parameter.name, parameter.initial);
    }
    edits.exposure = 1.25;
    const auto legacyFlags = edits.enabledCorrections;
    for (auto correction :
         {luma::Correction::presence, luma::Correction::pointColor, luma::Correction::grading,
          luma::Correction::vignette, luma::Correction::grain}) {
        edits.setEnabled(correction, false);
        require(!edits.enabled(correction), "Creative bypass was not saved.");
        require(edits.enabledCorrections == legacyFlags, "Creative bypass corrupted legacy flags.");
    }
    require(luma::decodeEditSnapshot(luma::encodeEditSnapshot(edits)) == edits,
            "Snapshot lost disabled creative corrections.");
    const auto encoded = luma::encodeCreative(edits.creative);
    rejects([&] { luma::decodeCreative(encoded + "trailing"); });
    auto invalid = edits.creative;
    invalid.enabled |= 1;
    rejects([&] { luma::encodeCreative(invalid); });
}
void colorAndEffects() {
    luma::EditDocument edits;
    const std::array<float, 3> hdr{-0.1F, 0.25F, 2.0F};
    require(luma::applyCreativeColor(hdr, edits) == hdr,
            "Neutral creative color changed HDR pixels.");
    require(luma::applyOutputEffects(hdr, edits, 0, 0, 11, 11, 1) == hdr,
            "Neutral output effects changed HDR pixels.");
    edits.setParameter("point.referenceHue", 360);
    edits.setParameter("point.referenceSaturation", 100);
    edits.setParameter("point.referenceLuminance", 50);
    require(std::abs(luma::pointColorWeight({1, 0, 0}, edits.creative) - 1) < 1e-6,
            "Point Color failed at the hue wrap boundary.");
    edits.setParameter("point.hue", 120);
    const auto green = luma::applyCreativeColor({1, 0, 0}, edits);
    require(green[1] > .99F && std::abs(green[0]) < 1e-6 && std::abs(green[2]) < 1e-6,
            "Point Color did not rotate the selected hue.");
    require(luma::applyCreativeColor({0, 0, 1}, edits) == std::array<float, 3>{0, 0, 1},
            "Point Color altered a color outside its range.");
    edits.setEnabled(luma::Correction::pointColor, false);
    require(luma::applyCreativeColor({1, 0, 0}, edits) == std::array<float, 3>{1, 0, 0},
            "Point Color bypass changed pixels.");
    edits = {};
    edits.setParameter("vignette.amount", -100);
    const std::array<float, 3> gray{.4F, .4F, .4F};
    require(luma::applyOutputEffects(gray, edits, 5, 5, 11, 11, 1) == gray,
            "Vignette changed the image center.");
    require(luma::applyOutputEffects(gray, edits, 0, 0, 11, 11, 1)[0] < gray[0],
            "Vignette did not darken the corners.");
    edits = {};
    edits.setParameter("grain.amount", 65);
    const auto grain = luma::applyOutputEffects(gray, edits, 7, 9, 20, 20, .5);
    require(grain != gray, "Grain produced no visible signal.");
    require(luma::applyOutputEffects(gray, edits, 7, 9, 20, 20, .5) == grain,
            "Grain was not deterministic.");
    edits.setEnabled(luma::Correction::grain, false);
    require(luma::applyOutputEffects(gray, edits, 7, 9, 20, 20, .5) == gray,
            "Disabled grain changed pixels.");
}
void presenceAndAuto() {
    luma::FloatImage image(9, 7);
    std::fill(image.rgb.begin(), image.rgb.end(), .2F);
    image.alpha.assign(63, 1);
    image.alpha[0] = 0;
    luma::EditDocument edits;
    edits.setParameter("presence.texture", 100);
    edits.setParameter("presence.clarity", -100);
    const auto sample = [](int x, int y) {
        return x == 0 && y == 0 ? std::array<float, 3>{1000, 1000, 1000}
                                : std::array<float, 3>{.2F, .2F, .2F};
    };
    const luma::PresencePass pass(image, edits, sample);
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x)
            if (x || y)
                require(std::abs(pass.pixel(sample(x, y), x, y)[0] - .2F) < 1e-6,
                        "Transparent RGB contaminated Presence blur or its image edges.");
    luma::CancellationSource cancelled;
    cancelled.cancel();
    rejects([&] { luma::PresencePass stopped(image, edits, sample, cancelled.token()); });
    luma::FloatImage scene(40, 32);
    for (std::size_t i = 0; i < scene.rgb.size(); ++i)
        scene.rgb[i] = static_cast<float>(.01 + .7 * (i / 3) / (40 * 32 - 1));
    edits = {};
    edits.setParameter("grain.size", 77);
    edits.setEnabled(luma::Correction::grading, false);
    luma::PhotoMetadata metadata;
    metadata.width = scene.width;
    metadata.height = scene.height;
    const luma::HistogramAutoAdjust provider;
    const auto adjusted = provider.estimate(scene, edits, metadata, {});
    adjusted.validate();
    require(adjusted.creative == edits.creative,
            "Auto Adjust changed unrelated creative settings.");
    require(provider.estimate(scene, edits, metadata, {}) == adjusted,
            "Auto Adjust is not deterministic for identical input.");
    const luma::DeterministicEnhance enhance;
    const auto generated = enhance.estimate(scene, edits, metadata, {});
    require(generated == enhance.estimate(scene, edits, metadata, {}),
            "Enhance fallback is not deterministic for identical input.");
    require(generated.provider == "apertone.histogram-enhance" &&
                generated.providerVersion == "1.0" && generated.model.empty() &&
                generated.modelDigest.empty() && generated.deterministicFallback,
            "Enhance fallback provenance is incomplete or ambiguous.");
    auto layered = edits;
    layered.creative.quickLooks.push_back({luma::newID(),
                                           "",
                                           100,
                                           true,
                                           {},
                                           "AI Enhance · CPU",
                                           generated.deltas,
                                           generated.provider,
                                           generated.providerVersion,
                                           generated.model,
                                           generated.modelDigest,
                                           generated.deterministicFallback});
    layered.validate();
    const auto resolved = luma::resolveQuickLooks(layered);
    require(std::abs(resolved.exposure - adjusted.exposure) < 1e-9 &&
                std::abs(resolved.contrast - adjusted.contrast) < 1e-9 &&
                std::abs(resolved.vibrance - adjusted.vibrance) < 1e-9,
            "Editable Enhance layer does not reproduce its estimated parameters.");
    require(luma::decodeCreative(luma::encodeCreative(layered.creative)) == layered.creative,
            "Enhance provenance did not survive serialization.");
    rejects([&] { provider.estimate(scene, edits, metadata, {}, cancelled.token()); });
    std::fill(scene.rgb.begin(), scene.rgb.end(), 0);
    rejects([&] { provider.estimate(scene, edits, metadata, {}); });
}
} // namespace
int main() {
    try {
        settingsAndSnapshots();
        colorAndEffects();
        presenceAndAuto();
        std::cout << "Creative settings, snapshots, rendering and Auto Adjust passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
