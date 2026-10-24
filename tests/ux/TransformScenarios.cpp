#include "tests/ux/AutoTransformProbe.h"
#include "tests/ux/UxContext.h"
#include <QSet>
#include <QSignalSpy>
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
    // Calibrated, bounded source fixture; Auto and Cancel still use real UI input.
    c.editor.openFile(QUrl::fromLocalFile(c.fixture + "/stress/Building.DNG"));
    c.loaded();
    c.saved();
    c.reveal("transformAuto");
    const auto before = c.editor.currentSettings();
    const bool undo = c.editor.canUndo(), redo = c.editor.canRedo();
    // An arbitrary QObject MetaCall can be a preview/save callback. Hold the
    // actual successful Auto result before its original GUI delivery instead.
    AutoTransformResultGate gate(c.editor);
    QSignalSpy cancels(d.object("transformAutoCancel"), SIGNAL(clicked()));
    require(cancels.isValid(), "Auto has no observable Cancel button");
    d.click("transformAuto");
    d.wait([&] { return gate.snapshot().held.has_value() || !c.editor.transformAutoBusy(); },
           "Calibrated Auto result reached the worker boundary", 15000);
    const auto held = gate.snapshot();
    require(held.held && held.successfulArrivals == 1 && held.deliveries == 0 && !held.released &&
                !held.timedOut && !held.hookFailed &&
                held.held->operation.controller == &c.editor && held.held->operation.job &&
                held.held->operation.assetID == c.editor.photoID().toStdString() &&
                held.held->operation.sourcePath == c.editor.sourcePath().toStdString() &&
                c.editor.transformAutoBusy(),
            "Auto did not hold exactly its real successful result");
    const auto operation = held.held->operation;
    auto wouldApply = before;
    wouldApply.transform = held.held->solved;
    wouldApply.setEnabled(luma::Correction::transform, true);
    require(wouldApply != before && wouldApply.transform.guided,
            "Auto cancellation fixture would not produce a meaningful edit");
    c.reveal("transformAutoCancel");
    d.click("transformAutoCancel");
    require(cancels.count() == 1 && gate.snapshot().deliveries == 0 &&
                !c.editor.transformAutoBusy() && c.editor.currentSettings() == before &&
                c.editor.canUndo() == undo && c.editor.canRedo() == redo,
            "Cancel committed Auto or left it busy");
    gate.release();
    d.wait([&] { return gate.snapshot().deliveries > 0; }, "Original Auto callback delivered");
    d.wait([&] { return operation.job->state.load() == luma::JobState::cancelled; },
           "Cancelled Auto worker finished");
    d.frame();
    const auto delivered = gate.snapshot();
    writeJson(c.root + "/auto-cancel-delivery.json",
              {{"fixture", "calibrated 320x240 Building.DNG"},
               {"successful_arrivals", static_cast<int>(delivered.successfulArrivals)},
               {"deliveries", static_cast<int>(delivered.deliveries)},
               {"rejections", static_cast<int>(delivered.rejections)},
               {"cancel_clicks", cancels.count()},
               {"released", delivered.released},
               {"left_hold", delivered.leftHold},
               {"timed_out", delivered.timedOut},
               {"hook_failed", delivered.hookFailed}});
    require(delivered.successfulArrivals == 1 && delivered.deliveries == 1 &&
                delivered.rejections == 1 && delivered.lastDelivered && delivered.rejection &&
                delivered.lastDelivered->sameAs(operation) &&
                delivered.rejection->operation.sameAs(operation) &&
                delivered.rejection->controllerPresent &&
                delivered.rejection->currentRevision != operation.revision && delivered.released &&
                delivered.leftHold && !delivered.timedOut && !delivered.hookFailed &&
                !delivered.deliveryBeforeRelease && delivered.mismatchedDeliveries == 0 &&
                c.editor.currentSettings() == before && c.editor.error().isEmpty() &&
                !c.editor.transformAutoBusy() && c.editor.canUndo() == undo &&
                c.editor.canRedo() == redo,
            "Original late Auto success was not rejected exactly once after Cancel");
    return true;
}
} // namespace ux
