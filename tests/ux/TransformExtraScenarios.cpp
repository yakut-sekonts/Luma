#include "core/render/TransformMapping.h"
#include "tests/ux/QueuedCallGate.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QSet>
#include <QTest>
#include <cmath>

namespace ux {
namespace {
void enterGuided(Context &c) {
    c.reveal("transformGuided");
    c.driver->click("transformGuided");
    c.driver->wait([&] { return c.editor.guidedMode() && c.editor.guidedCanvasReady(); },
                   "extra Guided fixture ready");
    c.driver->frame();
    require(c.driver->item("transformOverlay")->width() > 0 &&
                c.driver->item("transformOverlay")->height() > 0,
            "Guided fixture has no canvas");
}
void rendered(Context &c) {
    const auto expected = fixtureReference(c);
    c.driver->wait(
        [&] {
            return c.driver->object("develop.preview")->property("status").toInt() == 1 &&
                   c.driver->object("develop.preview")->property("source").toUrl() ==
                       c.editor.previewUrl() &&
                   c.provider->requestImage({}, nullptr, {}) == expected;
        },
        "Transform preview matches final edit document");
}
void reset(Context &c) {
    c.reveal("transformReset");
    c.driver->click("transformReset");
    c.saved();
}
void cancelAuto(Context &c) {
    auto &d = *c.driver;
    const auto before = c.editor.currentSettings();
    const bool undo = c.editor.canUndo();
    QueuedCallGate gate(
        c.editor,
        [&] { return c.editor.transformAutoBusy() && !c.editor.busy() && !c.editor.saving(); },
        [&] {
            d.frame();
            d.click("transformAutoCancel");
        });
    d.click("transformAuto");
    d.wait([&] { return gate.caught; }, "repeated Auto reached queued completion");
    if (gate.failure)
        std::rethrow_exception(gate.failure);
    d.frame();
    require(!c.editor.transformAutoBusy() && c.editor.currentSettings() == before &&
                c.editor.canUndo() == undo && c.editor.error().isEmpty(),
            "Repeated Auto/Cancel changed history, settings or busy state");
}
} // namespace
bool transformExtraScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-TRN-002", "UX-TRN-005", "UX-TRN-009", "UX-TRN-010",
                                         "UX-TRN-011", "UX-TRN-012", "UX-TRN-013", "UX-TRN-014"};
    if (!supported.contains(id))
        return false;
    auto &d = *c.driver;
    if (id == "UX-TRN-005") {
        const auto flat = c.root + "/flat";
        require(QDir().mkpath(flat), "Cannot create uncertain Auto fixture");
        QImage image(256, 192, QImage::Format_RGBA8888);
        image.fill(QColor(128, 128, 128));
        require(image.save(flat + "/Flat.png"), "Cannot save flat public Auto fixture");
        c.fixture = flat;
        c.import(1);
        d.doubleClick(c.card(0));
        c.loaded();
    } else
        c.openFirst();
    c.saved();
    if (id == "UX-TRN-002") {
        for (const auto *control :
             {"rotate", "horizontal", "vertical", "aspect", "scale", "xOffset", "yOffset"}) {
            for (const bool maximum : {false, true}) {
                reset(c);
                const QString selector = "develop.transform." + QString(control) + "Slider";
                const std::string parameter = "transform." + std::string(control);
                c.reveal(selector);
                const auto value = d.object(selector)->property(maximum ? "to" : "from").toDouble();
                d.drag(selector, d.sliderThumb(selector), {maximum ? 1. : 0., .5});
                const auto document = c.editor.currentSettings();
                require(document.parameter(parameter) == value && c.editor.error().isEmpty(),
                        "Transform did not reach a valid finite UI boundary: " + QString(control));
                const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
                require(active.has_value(), "Transform boundary fixture lost active asset");
                const auto &meta = active->metadata;
                const luma::TransformMapping mapping(meta.width, meta.height, meta.orientation,
                                                     document.transform, true);
                require(std::isfinite(mapping.extraZoom()) && mapping.extraZoom() > 0,
                        "Extreme Transform has invalid effective scale");
                for (const double x : {0., .5, 1.})
                    for (const double y : {0., .5, 1.}) {
                        const auto point = mapping.sourceFromSensorCanvas(x * (meta.width - 1),
                                                                          y * (meta.height - 1));
                        require(std::isfinite(point[0]) && std::isfinite(point[1]),
                                "Extreme Transform mapping produced non-finite coordinates");
                    }
                rendered(c);
                c.saved();
            }
        }
        return true;
    }
    if (id == "UX-TRN-005" || id == "UX-TRN-013") {
        c.reveal("transformAuto");
        const auto before = c.editor.currentSettings();
        const bool undo = c.editor.canUndo();
        c.metrics.reset();
        QElapsedTimer timer;
        timer.start();
        d.click("transformAuto");
        d.wait([&] { return !c.editor.transformAutoBusy(); },
               "Auto completed or refused uncertain fixture");
        if (id == "UX-TRN-005") {
            require(!c.editor.error().isEmpty() && c.editor.currentSettings() == before &&
                        c.editor.canUndo() == undo,
                    "Uncertain flat Auto created an edit/history entry instead of refusing");
        } else {
            writeJson(c.root + "/measurements.json",
                      {{"operation", "Transform Auto"},
                       {"performance_mode", "MEASURE_ONLY"},
                       {"elapsed_ms", timer.elapsed()},
                       {"main_loop_stall_max_ms", c.metrics.json()["main_loop_stall_max_ms"]},
                       {"target_stall_ms", 250},
                       {"refused", !c.editor.error().isEmpty()}});
        }
        reset(c);
        require(c.editor.error().isEmpty(), "Reset did not clear Auto refusal");
        return true;
    }
    if (id == "UX-TRN-014") {
        c.reveal("transformAuto");
        for (int cycle = 0; cycle < 100; ++cycle) {
            cancelAuto(c);
            d.log({{"action", "auto_cancel_cycle"}, {"cycle", cycle + 1}});
        }
        return true;
    }
    if (id == "UX-TRN-011") {
        c.reveal("develop.transform.horizontalSlider");
        d.drag("develop.transform.horizontalSlider",
               d.sliderThumb("develop.transform.horizontalSlider"), {.75, .5});
        rendered(c);
        const auto constrained = c.editor.currentSettings();
        for (int cycle = 0; cycle < 10; ++cycle) {
            c.reveal("transformConstrain");
            d.click("transformConstrain");
            auto expected = constrained;
            expected.transform.constrainCrop = false;
            require(c.editor.currentSettings() == expected, "Constrain changed unrelated geometry");
            rendered(c);
            d.click("transformConstrain");
            require(c.editor.currentSettings() == constrained,
                    "Constrain did not restore exact geometry");
            rendered(c);
        }
        return true;
    }
    if (id == "UX-TRN-010") {
        const auto before = c.editor.currentSettings();
        enterGuided(c);
        c.reveal("cropTool");
        d.click("cropTool");
        d.wait([&] { return c.editor.cropMode() && c.editor.cropCanvasReady(); },
               "Crop owns its canvas");
        require(!c.editor.guidedMode() && !d.item("transformOverlay")->isVisible(),
                "Crop and Guided share pointer ownership");
        enterGuided(c);
        require(!c.editor.cropMode() && !d.item("cropOverlay")->isVisible() &&
                    c.editor.currentSettings() == before,
                "Guided and Crop share pointer ownership or mode switching changed edits");
        return true;
    }
    if (id == "UX-TRN-009") {
        enterGuided(c);
        for (int layout = 0; layout < 3; ++layout) {
            c.reveal("transformGuideLayout");
            d.choose("transformGuideLayout", layout);
            d.frame();
            const bool undo = c.editor.canUndo();
            const auto before = c.editor.currentSettings();
            const auto guides = c.editor.transformGuides();
            const auto start = d.point("transformGuidePoint0_0");
            const auto end = d.point("transformOverlay", {.4, .3});
            d.log({{"action", "guided_leave_while_pointer_held"}, {"layout", layout}});
            QTest::mousePress(c.window, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(c.window, end);
            QCoreApplication::processEvents();
            require(c.editor.transformGuides() != guides,
                    "Guided mid-drag fixture did not move an endpoint");
            d.key(Qt::Key_Backslash);
            QTest::mouseRelease(c.window, Qt::LeftButton, Qt::NoModifier, end);
            require(c.editor.original() && !c.editor.guidedMode() &&
                        !d.item("transformOverlay")->isVisible() &&
                        c.editor.currentSettings() == before &&
                        c.editor.transformGuides().isEmpty(),
                    "Leaving Guided left active geometry or persisted draft endpoints");
            d.key(Qt::Key_Backslash);
            enterGuided(c);
            require(c.editor.transformGuides() == guides && c.editor.canUndo() == undo &&
                        d.object("transformGuideLayout")->property("currentIndex").toInt() ==
                            layout,
                    "Leaving Guided retained unapplied transient endpoints");
        }
        return true;
    }
    const auto source = c.editor.sourcePath();
    c.reveal("transformAuto");
    QueuedCallGate gate(
        c.editor,
        [&] { return c.editor.transformAutoBusy() && !c.editor.busy() && !c.editor.saving(); },
        [&] {
            d.click(c.card(1, true));
            d.wait([&] { return c.editor.sourcePath() != source && !c.editor.busy(); },
                   "Auto photo-switch fixture loaded");
            c.loaded();
            require(!c.editor.transformAutoBusy() && !c.editor.guidedMode(),
                    "Photo switch retained Auto/Guided state");
        });
    d.click("transformAuto");
    d.wait([&] { return gate.caught; }, "old Auto completion held across photo switch");
    if (gate.failure)
        std::rethrow_exception(gate.failure);
    d.frame();
    require(c.editor.currentSettings() == luma::EditDocument{} && c.editor.error().isEmpty(),
            "Old Auto callback changed the new photo");
    enterGuided(c);
    d.dragBetween("transformGuidePoint0_0", {.5, .5}, "transformOverlay", {.4, .3});
    d.click(c.card(2, true));
    d.wait([&] { return !c.editor.busy() && !c.editor.guidedMode(); },
           "Guided photo switch completed");
    require(c.editor.transformGuides().isEmpty() &&
                c.editor.currentSettings() == luma::EditDocument{},
            "Guided draft endpoints leaked across photo switch");
    return true;
}
} // namespace ux
