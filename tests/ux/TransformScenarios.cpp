#include "tests/ux/QueuedCallGate.h"
#include "tests/ux/UxContext.h"
#include <QSet>
#include <cmath>

namespace ux {
namespace {
const QStringList controls{"rotate", "horizontal", "vertical", "aspect",
                           "scale",  "xOffset",    "yOffset"};
void undoRedo(Context &c, const luma::EditDocument &before, const luma::EditDocument &after) {
    c.driver->click("main.undoButton");
    require(c.editor.currentSettings() == before, "Transform gesture was not one Undo");
    c.driver->click("main.redoButton");
    require(c.editor.currentSettings() == after, "Transform Redo changed geometry");
}
void guided(Context &c, bool horizontal) {
    auto &d = *c.driver;
    c.reveal("transformGuided");
    d.click("transformGuided");
    d.wait([&] { return c.editor.guidedMode() && c.editor.guidedCanvasReady(); },
           "Guided canvas ready");
    c.reveal("transformGuideLayout");
    d.choose("transformGuideLayout", horizontal ? 1 : 0);
    d.wait([&] { return c.editor.transformGuides().size() == 2; }, "Guided pair created");
    const auto before = c.editor.currentSettings();
    for (int line = 0; line < 2; ++line) {
        const double position = line == 0 ? .2 : .8;
        const QPointF expected = horizontal ? QPointF(.15, position) : QPointF(position, .15);
        d.dragBetween(QString("transformGuidePoint%1_0").arg(line), {.5, .5}, "transformOverlay",
                      expected);
        const auto guide = c.editor.transformGuides()[line].toMap();
        d.log({{"action", "guide_endpoint_oracle"},
               {"line", line},
               {"x", guide["x1"].toDouble()},
               {"y", guide["y1"].toDouble()}});
        require(std::abs(guide["x1"].toDouble() - expected.x()) < .005 &&
                    std::abs(guide["y1"].toDouble() - expected.y()) < .005,
                "Guided endpoint did not follow the complete drag");
    }
    require(c.editor.currentSettings() == before,
            "Transient guides committed an edit before Apply");
    c.reveal("transformGuideApply");
    d.click("transformGuideApply");
    const auto after = c.editor.currentSettings();
    auto expected = before;
    expected.transform = after.transform;
    expected.setEnabled(luma::Correction::transform, true);
    require(after.transform.guided && after != before && after == expected &&
                !c.editor.guidedMode(),
            "Apply guides failed or changed settings outside Transform");
    undoRedo(c, before, after);
}
} // namespace
void editTransformControls(Context &c) {
    guided(c, false);
    for (const auto &control : controls) {
        const auto selector = "develop.transform." + control + "Slider";
        c.reveal(selector);
        const auto before = c.editor.currentSettings();
        c.driver->drag(selector, c.driver->sliderThumb(selector), {.6, .5});
        require(c.editor.currentSettings().parameter(("transform." + control).toStdString()) !=
                    before.parameter(("transform." + control).toStdString()),
                "Transform restart fixture did not edit: " + control);
    }
}
bool transformScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-TRN-001", "UX-TRN-003", "UX-TRN-004", "UX-TRN-006",
                                         "UX-TRN-007", "UX-TRN-008", "UX-UND-007"};
    if (!supported.contains(id))
        return false;
    c.openFirst();
    auto &d = *c.driver;
    if (id == "UX-UND-007") {
        guided(c, false);
        c.saved();
        // Fixture selection only; application, completion and history use real controls.
        c.editor.openFile(QUrl::fromLocalFile(c.fixture + "/stress/Building.DNG"));
        c.loaded();
        const auto before = c.editor.currentSettings();
        c.reveal("transformAuto");
        d.click("transformAuto");
        d.wait([&] { return !c.editor.transformAutoBusy(); }, "calibrated Auto completed");
        const auto after = c.editor.currentSettings();
        auto expected = before;
        expected.transform = after.transform;
        expected.setEnabled(luma::Correction::transform, true);
        require(after != before && after.transform.guided && after == expected &&
                    c.editor.error().isEmpty(),
                "Auto history fixture did not successfully solve the public building image");
        undoRedo(c, before, after);
        return true;
    }
    if (id == "UX-TRN-001") {
        for (const auto &control : controls) {
            const auto selector = "develop.transform." + control + "Slider";
            const auto parameter = "transform." + control;
            c.reveal(selector);
            const auto before = c.editor.currentSettings();
            d.drag(selector, d.sliderThumb(selector), {.6, .5});
            const auto after = c.editor.currentSettings();
            auto expected = before;
            expected.setParameter(parameter.toStdString(),
                                  after.parameter(parameter.toStdString()));
            require(after != before && after == expected,
                    "Manual Transform changed the wrong fields: " + control);
            undoRedo(c, before, after);
        }
        return true;
    }
    if (id == "UX-TRN-006" || id == "UX-TRN-007") {
        guided(c, id == "UX-TRN-007");
        return true;
    }
    if (id == "UX-TRN-008") {
        c.reveal("transformGuided");
        d.click("transformGuided");
        d.wait([&] { return c.editor.guidedCanvasReady(); }, "degenerate Guided fixture ready");
        const auto before = c.editor.currentSettings();
        const bool undo = c.editor.canUndo();
        for (int endpoint = 0; endpoint < 2; ++endpoint)
            d.dragBetween(QString("transformGuidePoint0_%1").arg(endpoint), {.5, .5},
                          "transformOverlay", {.25, .5});
        c.reveal("transformGuideApply");
        d.click("transformGuideApply");
        require(!c.editor.error().isEmpty() && c.editor.currentSettings() == before &&
                    c.editor.canUndo() == undo,
                "Degenerate guides were not refused safely");
        c.reveal("transformReset");
        d.click("transformReset");
        require(c.editor.error().isEmpty() && !c.editor.guidedMode(),
                "Reset did not clear Guided refusal");
        return true;
    }
    c.saved();
    c.reveal("transformAuto");
    const auto before = c.editor.currentSettings();
    const bool undo = c.editor.canUndo();
    QueuedCallGate gate(
        c.editor,
        [&] { return c.editor.transformAutoBusy() && !c.editor.busy() && !c.editor.saving(); },
        [&] {
            d.log({{"action", "auto_completion_callback_held"}});
            d.frame();
            require(d.item("transformAutoCancel")->isVisible() &&
                        d.item("transformAutoCancel")->isEnabled(),
                    "Auto has no usable Cancel");
            d.click("transformAutoCancel");
            require(!c.editor.transformAutoBusy() && c.editor.currentSettings() == before &&
                        c.editor.canUndo() == undo,
                    "Cancel committed Auto or left it busy");
        });
    d.click("transformAuto");
    d.wait([&] { return gate.caught; }, "Auto queued completion reached");
    if (gate.failure)
        std::rethrow_exception(gate.failure);
    d.frame();
    require(c.editor.currentSettings() == before && c.editor.error().isEmpty() &&
                !c.editor.transformAutoBusy() && c.editor.canUndo() == undo,
            "Late Auto completion changed state after Cancel");
    return true;
}
} // namespace ux
