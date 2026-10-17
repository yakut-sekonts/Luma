#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QSet>

namespace ux {
namespace {
void pixelsReady(Context &c) {
    const auto expected = fixtureReference(c);
    c.driver->wait(
        [&] {
            const auto *preview = c.driver->item("develop.preview");
            return preview->isVisible() && preview->property("status").toInt() == 1 &&
                   preview->property("source").toUrl().toString() == c.editor.previewUrl() &&
                   c.provider->requestImage({}, nullptr, {}) == expected;
        },
        "preset document pixels ready");
}
void undoRedo(Context &c, const luma::EditDocument &before, const luma::EditDocument &after) {
    c.driver->click("main.undoButton");
    require(c.editor.currentSettings() == before, "Preset application was not one Undo");
    pixelsReady(c);
    c.driver->click("main.redoButton");
    require(c.editor.currentSettings() == after, "Preset Redo changed the applied document");
    pixelsReady(c);
}
} // namespace
bool presetExtraScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-PRS-007", "UX-PRS-014", "UX-PRS-015", "UX-PRS-016",
                                         "UX-PRV-004"};
    if (!supported.contains(id))
        return false;
    auto &d = *c.driver;
    c.openFirst();
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
    if (id == "UX-PRS-015") {
        for (const auto *selector : {"develop.saturationSlider", "sharpen.amountSlider",
                                     "develop.transform.rotateSlider"}) {
            c.reveal(selector);
            d.drag(selector, d.sliderThumb(selector), {.6, .5});
        }
        c.addMask(0);
        c.reveal("localPanelToggle");
        d.click("localPanelToggle");
        require(!c.editor.localMode(), "Preset fixture retained its colored mask overlay");
    }
    c.saved();
    pixelsReady(c);
    const auto before = c.editor.currentSettings();
    if (id == "UX-PRV-004") {
        const auto processed = c.provider->requestImage({}, nullptr, {});
        d.click("develop.originalButton");
        d.wait([&] { return c.provider->requestImage({}, nullptr, {}) != processed; },
               "distinct Original pixels prepared");
        const auto original = c.provider->requestImage({}, nullptr, {});
        d.click("develop.processedButton");
        pixelsReady(c);
        d.move("filter_soft-portrait", {.5, .5});
        d.wait(
            [&] {
                return c.editor.presetPreview() &&
                       c.provider->requestImage({}, nullptr, {}) != processed;
            },
            "actual preset hover visible before Original");
        d.key(Qt::Key_Backslash);
        d.wait(
            [&] {
                return c.editor.original() && c.provider->requestImage({}, nullptr, {}) == original;
            },
            "Original replaces hover render");
        d.move("develop.preview", {.5, .5});
        d.wait([&] { return !c.editor.presetPreview(); }, "pointer leave cleared hover");
        d.click("develop.processedButton");
        d.wait([&] { return c.provider->requestImage({}, nullptr, {}) == processed; },
               "exact pre-hover Processed pixels restored");
        require(c.editor.currentSettings() == before, "Hover/Original sequence changed edits");
        d.click("main.undoButton");
        require(c.editor.currentSettings() == luma::EditDocument{} && !c.editor.canUndo(),
                "Hover/Original sequence polluted history");
        return true;
    }
    std::vector<luma::AssetRecord> assets;
    if (id == "UX-PRS-016") {
        const luma::Catalog catalog(c.catalogFile.toStdString());
        for (const auto &assetID : catalog.libraryIDs({})) {
            const auto asset = catalog.assetByID(assetID);
            require(asset.has_value(), "Missing preset isolation fixture asset");
            assets.push_back(*asset);
        }
    }
    const auto active = c.editor.photoID().toStdString();
    d.move("filter_soft-portrait", {.5, .5});
    d.wait([&] { return c.editor.presetPreview(); }, "filter hovered");
    d.click("filter_soft-portrait");
    const auto applied = c.editor.currentSettings();
    const auto preset = c.presets.snapshot("builtin:soft-portrait");
    require(applied != before && applied == luma::mergeSettings(before, preset.settings,
                                                                preset.groups, preset.parameters),
            "Filter did not apply its declared subset");
    d.move("develop.preview", {.5, .5});
    pixelsReady(c);
    if (id == "UX-PRS-014") {
        for (int repeat = 0; repeat < 5; ++repeat) {
            d.click("filter_soft-portrait");
            d.move("develop.preview", {.5, .5});
            pixelsReady(c);
            require(c.editor.currentSettings() == applied,
                    "Repeated built-in filter application accumulated settings");
        }
    } else if (id == "UX-PRS-015") {
        const auto natural = c.presets.snapshot("builtin:natural");
        d.click("filter_natural");
        d.move("develop.preview", {.5, .5});
        const auto restored = c.editor.currentSettings();
        auto expected = applied;
        expected.curves = natural.settings.curves;
        expected.curveShadows = natural.settings.curveShadows;
        expected.curveDarks = natural.settings.curveDarks;
        expected.curveLights = natural.settings.curveLights;
        expected.curveHighlights = natural.settings.curveHighlights;
        expected.hsl = natural.settings.hsl;
        expected.setEnabled(luma::Correction::curves, true);
        expected.setEnabled(luma::Correction::hsl, true);
        require(natural.groups == (4 | 8) && restored != applied && restored == expected,
                "Natural changed fields outside its declared Curve/HSL groups");
        undoRedo(c, applied, restored);
        return true;
    } else if (id == "UX-PRS-016") {
        c.saved();
        const luma::Catalog catalog(c.catalogFile.toStdString());
        for (const auto &asset : assets) {
            const auto current = catalog.assetByID(asset.id);
            require(current && current->edits == (asset.id == active ? applied : asset.edits),
                    "Single-photo filter altered another catalog asset");
        }
    }
    undoRedo(c, before, applied);
    return true;
}
} // namespace ux
