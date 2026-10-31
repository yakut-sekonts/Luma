#include "core/catalog/Identity.h"
#include "core/edits/CreativeSettings.h"
#include "core/edits/LocalMasks.h"
#include "core/image/Bayer.h"
#include "core/quick/MatchLook.h"
#include "core/quick/QuickLooks.h"
#include "core/render/CpuRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <typename Operation> void rejects(Operation operation, const char *message) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}
void catalogChecks() {
    const auto recipes = luma::quickLookRecipes();
    require(recipes.size() >= 70, "Quick Look catalog is incomplete.");
    std::set<std::string_view> ids, categories;
    bool subject = false, background = false, sky = false, blur = false;
    for (const auto &recipe : recipes) {
        require(ids.insert(recipe.id).second, "Duplicate Quick Look recipe ID.");
        categories.insert(recipe.category);
        subject |= recipe.target == luma::QuickLookTarget::subject;
        background |= recipe.target == luma::QuickLookTarget::background;
        sky |= recipe.target == luma::QuickLookTarget::sky;
        blur |= recipe.blur > 0;
    }
    require(categories.size() >= 8 && subject && background && sky && blur,
            "Quick Look category or adaptive coverage is incomplete.");
}
void stackChecks() {
    luma::EditDocument source;
    source.creative.quickLooks = {
        {luma::newID(), "wide-cinema", 75, true, {}, {}, {}},
        {luma::newID(), "scene-travel", 125, true, {}, {}, {}},
        {luma::newID(), "pure-mono", 0, true, {}, {}, {}},
    };
    source.validate();
    const auto resolved = luma::resolveQuickLooks(source);
    require(resolved.creative.quickLooks.empty(), "Resolved document retained Quick Look layers.");
    require(resolved.contrast != source.contrast && resolved.vibrance != source.vibrance,
            "Stacked Quick Looks did not affect the document.");
    const auto encoded = luma::encodeCreative(source.creative);
    require(luma::decodeCreative(encoded) == source.creative,
            "Quick Look stack did not survive the creative codec.");
    std::ostringstream version2;
    version2.imbue(std::locale::classic());
    version2 << "LUMA_CREATIVE 2 " << source.creative.enabled
             << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto value : source.creative.values)
        version2 << ' ' << value;
    version2 << ' ' << source.creative.quickLooks.size() << '\n';
    for (const auto &layer : source.creative.quickLooks)
        version2 << std::quoted(layer.id) << ' ' << std::quoted(layer.recipe) << ' ' << layer.amount
                 << ' ' << layer.enabled << ' ' << std::quoted(layer.mask) << '\n';
    require(luma::decodeCreative(version2.str()) == source.creative,
            "Creative codec stopped reading version 2 Quick Look stacks.");
    std::ostringstream version3;
    version3.imbue(std::locale::classic());
    version3 << "LUMA_CREATIVE 3 " << source.creative.enabled
             << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto value : source.creative.values)
        version3 << ' ' << value;
    version3 << ' ' << source.creative.quickLooks.size() << '\n';
    for (const auto &layer : source.creative.quickLooks) {
        version3 << std::quoted(layer.id) << ' ' << std::quoted(layer.recipe) << ' ' << layer.amount
                 << ' ' << layer.enabled << ' ' << std::quoted(layer.mask) << ' '
                 << std::quoted(layer.name) << ' ' << layer.deltas.size();
        for (const auto &delta : layer.deltas)
            version3 << ' ' << std::quoted(delta.parameter) << ' ' << delta.value;
        version3 << '\n';
    }
    require(luma::decodeCreative(version3.str()) == source.creative,
            "Creative codec stopped reading version 3 Quick Look stacks.");
    const auto legacy = luma::encodeCreative(luma::CreativeSettings{});
    require(legacy.starts_with("LUMA_CREATIVE 4 "), "Creative codec was not upgraded.");
}
void matchLookChecks() {
    luma::EditDocument target;
    target.exposure = 1.25;
    target.temperature = 5100;
    target.contrast = 5;
    target.hsl[0].saturation = -10;
    target.creative.setParameter("presence.clarity", 4);
    target.creative.setParameter("grading.shadows.hue", 350);

    auto first = target;
    first.exposure = -2;
    first.temperature = 7200;
    first.contrast = 25;
    first.hsl[0].saturation = 20;
    first.creative.setParameter("presence.clarity", 14);
    first.creative.setParameter("grading.shadows.hue", 10);
    auto second = first;
    second.contrast = 45;
    second.hsl[0].saturation = 40;
    second.creative.setParameter("presence.clarity", 34);
    second.creative.setParameter("grading.shadows.hue", 30);

    const std::array references{first, second};
    const auto deltas = luma::matchLookDeltas(target, references);
    require(std::none_of(deltas.begin(), deltas.end(),
                         [](const auto &delta) {
                             return delta.parameter == "exposure" ||
                                    delta.parameter == "temperature" || delta.parameter == "tint";
                         }),
            "Default Match Look copied Exposure or white balance.");
    auto matched = target;
    matched.creative.quickLooks.push_back(
        {luma::newID(), "", 100, true, {}, "Match Look · 2 references", deltas});
    matched.validate();
    const auto resolved = luma::resolveQuickLooks(matched);
    require(std::abs(resolved.exposure - target.exposure) < 1e-9 &&
                std::abs(resolved.temperature - target.temperature) < 1e-9,
            "Match Look changed preserved Exposure or white balance.");
    require(std::abs(resolved.contrast - 35) < 1e-9 &&
                std::abs(resolved.hsl[0].saturation - 30) < 1e-9 &&
                std::abs(resolved.creative.parameter("presence.clarity") - 24) < 1e-9 &&
                std::abs(resolved.creative.parameter("grading.shadows.hue") - 20) < 1e-9,
            "Match Look did not average editable reference style.");

    matched.creative.quickLooks.front().amount = 50;
    const auto half = luma::resolveQuickLooks(matched);
    require(std::abs(half.contrast - 20) < 1e-9 &&
                std::abs(half.creative.parameter("grading.shadows.hue") - 5) < 1e-9,
            "Match Look Amount did not scale embedded deltas.");
    require(luma::decodeCreative(luma::encodeCreative(matched.creative)) == matched.creative,
            "Match Look layer did not survive the creative codec.");
    matched.creative.quickLooks.front().provider = "apertone.histogram-enhance";
    matched.creative.quickLooks.front().providerVersion = "1.0";
    matched.creative.quickLooks.front().deterministicFallback = true;
    matched.creative.quickLooks.front().name = "AI Enhance · CPU";
    matched.validate();
    require(luma::decodeCreative(luma::encodeCreative(matched.creative)) == matched.creative,
            "Generated-layer provenance did not survive the creative codec.");
    auto invalidProvenance = matched;
    invalidProvenance.creative.quickLooks.front().model = "Unpinned model";
    rejects([&] { invalidProvenance.validate(); },
            "Generated layer accepted a model without its SHA-256 digest.");
    invalidProvenance.creative.quickLooks.front().modelDigest = std::string(64, 'a');
    rejects([&] { invalidProvenance.validate(); },
            "Generated layer marked a pinned model as a deterministic fallback.");
    auto invalid = matched;
    invalid.creative.quickLooks.front().deltas.push_back({"crop.x", 1});
    rejects([&] { invalid.validate(); },
            "Match Look accepted geometry outside the transferable parameter set.");
    const std::array identical{target};
    rejects([&] { (void)luma::matchLookDeltas(target, identical); },
            "Match Look accepted a reference without a distinct editable style.");

    const auto complete = luma::matchLookDeltas(
        target, references, {.includeExposure = true, .includeWhiteBalance = true});
    require(std::any_of(complete.begin(), complete.end(),
                        [](const auto &delta) { return delta.parameter == "exposure"; }) &&
                std::any_of(complete.begin(), complete.end(),
                            [](const auto &delta) { return delta.parameter == "temperature"; }),
            "Match Look inclusion options did not expose Light and white balance.");
}
void localBlurChecks() {
    luma::FloatImage image(48, 24);
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x) {
            const float value = ((x / 2) % 2) ? 1.F : 0.F;
            const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 3;
            image.rgb[offset] = image.rgb[offset + 1] = image.rgb[offset + 2] = value;
        }
    luma::EditDocument edit;
    luma::LocalMask mask;
    mask.id = luma::newID();
    mask.name = "Background blur";
    mask.shape = luma::MaskShape::bitmap;
    mask.bitmap = luma::BitmapMask(2, 1, {0, 255});
    mask.blur = 100;
    edit.localMasks.push_back(mask);
    edit.validate();
    const auto decoded = luma::decodeLocalMasks(luma::encodeLocalMasks(edit.localMasks));
    require(decoded == edit.localMasks && decoded.front().blur == 100,
            "Local blur did not survive the mask codec.");
    const auto plain = luma::renderCPU(image, {}), blurred = luma::renderCPU(image, edit);
    require(plain.rgba != blurred.rgba, "Local blur did not affect rendered pixels.");
    require(std::equal(plain.rgba.begin(), plain.rgba.begin() + 4 * 4, blurred.rgba.begin()),
            "Local blur changed pixels outside the selection.");
}
} // namespace
int main() {
    try {
        catalogChecks();
        stackChecks();
        matchLookChecks();
        localBlurChecks();
        std::cout << "Quick Look catalog, Match Look codec and adaptive blur passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
