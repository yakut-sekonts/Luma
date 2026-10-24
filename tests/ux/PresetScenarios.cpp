#include "tests/ux/UxContext.h"
#include "tests/ux/UxTestIds.h"

namespace ux {
bool presetScenario(Context &c, const QString &id) {
    if (id != "UX-PRS-001" && id != "UX-PRS-004" && id != "UX-PRS-005" && id != "UX-UND-008" &&
        id != "UX-PRS-008")
        return false;
    auto &d = *c.driver;
    c.openFirst();
    c.reveal(ids::exposure);
    const auto initial = c.editor.currentSettings();
    d.drag(ids::exposure, d.sliderThumb(ids::exposure), {.65, .5});
    c.saved();
    const auto document = c.editor.currentSettings();
    if (id == "UX-PRS-001" || id == "UX-UND-008" || id == "UX-PRS-008") {
        d.click("preset.tabs.presets");
        d.click("presetCreate");
        d.wait([&] { return d.object("presetEditor")->property("opened").toBool(); },
               "subset preset editor opened");
        d.type("presetName", "UX subset 🌅");
        d.type("presetFolder", "UX folder");
        c.selectPresetGroups(static_cast<std::uint32_t>(luma::SettingsGroup::light));
        d.click("presetSave");
        d.wait(
            [&] {
                return !d.object("presetEditor")->property("visible").toBool() &&
                       c.presets.items().size() == 1;
            },
            "subset preset saved");
        const auto saved = c.presets.items()[0].toMap()["id"].toString();
        const auto preset = c.presets.snapshot(saved);
        require(preset.groups == 1 && preset.settings.exposure == document.exposure,
                "Preset did not preserve its selected Light subset");
        c.reveal(ids::exposure);
        d.doubleClick(ids::exposure);
        c.reveal("develop.saturationSlider");
        d.drag("develop.saturationSlider", d.sliderThumb("develop.saturationSlider"), {.65, .5});
        const auto before = c.editor.currentSettings();
        const auto expected =
            luma::mergeSettings(before, preset.settings, preset.groups, preset.parameters);
        d.wait([&] { return d.find("preset." + saved) != nullptr; }, "saved preset delegate ready");
        d.click("preset." + saved);
        require(c.editor.currentSettings() == expected && expected.saturation == before.saturation,
                "Light preset altered unselected Color settings");
        d.click(ids::undo);
        require(c.editor.currentSettings() == before, "Subset preset was not one Undo");
        return true;
    }
    const auto image = c.provider->requestImage({}, nullptr, {});
    d.move("filter_soft-portrait", {.5, .5});
    d.wait(
        [&] {
            return c.editor.presetPreview() && c.provider->requestImage({}, nullptr, {}) != image;
        },
        "actual preset hover pixels");
    require(c.editor.currentSettings() == document && !c.editor.saving() &&
                luma::Catalog(c.catalogFile.toStdString()).activeAsset().value().edits == document,
            "Hover saved/applied a preset instead of transient preview");
    d.move("develop.preview", {.5, .5});
    d.wait(
        [&] {
            return !c.editor.presetPreview() && c.provider->requestImage({}, nullptr, {}) == image;
        },
        "pointer leave restored exact pre-hover pixels");
    require(c.editor.currentSettings() == document, "Pointer leave lost pre-hover document");
    d.click(ids::undo);
    require(c.editor.currentSettings() == initial && !c.editor.canUndo(),
            "Hover polluted session history");
    return true;
}
} // namespace ux
