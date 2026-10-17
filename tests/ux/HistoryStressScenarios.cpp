#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QSet>
#include <QSignalSpy>
#include <QStyleHints>
#include <vector>

namespace ux {
namespace {
struct KeyboardNavigation {
    Qt::TabFocusBehavior previous = QGuiApplication::styleHints()->tabFocusBehavior();
    KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    }
    ~KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(previous);
    }
};
struct SliderControl {
    QString parameter;
    double minimum = -100;
    double maximum = 100;
    QString band;
};
const std::vector<SliderControl> &basicControls() {
    static const std::vector<SliderControl> controls{
        {"temperature", 2000, 25000, {}}, {"tint", -150, 150, {}},
        {"exposure", -5, 5, {}},          {"contrast", -100, 100, {}},
        {"highlights", -100, 100, {}},    {"shadows", -100, 100, {}},
        {"whites", -100, 100, {}},        {"blacks", -100, 100, {}},
        {"vibrance", -100, 100, {}},      {"saturation", -100, 100, {}}};
    return controls;
}
QString selector(const SliderControl &control) {
    return "develop." + control.parameter + "Slider";
}
void prepare(Context &c, const SliderControl &control) {
    if (!control.band.isEmpty()) {
        const auto name = "hsl.band." + control.band;
        c.reveal(name);
        c.driver->click(name);
        require(c.driver->object("hsl.panel")->property("band").toString() == control.band,
                "HSL stress fixture selected the wrong band");
    }
    c.reveal(selector(control));
    auto *slider = c.driver->item(selector(control));
    require(slider->isVisible() && slider->isEnabled() &&
                slider->property("from").toDouble() == control.minimum &&
                slider->property("to").toDouble() == control.maximum,
            "Stress slider is unavailable or has unexpected limits: " + control.parameter);
}
void focusSlider(Context &c, const SliderControl &control) {
    prepare(c, control);
    const auto before = c.editor.currentSettings();
    const auto name = selector(control);
    auto *slider = c.driver->item(name);
    // macOS Slider uses TabFocus. A pointer click need not grant keyboard focus.
    for (int step = 0; step < 240 && !slider->hasActiveFocus(); ++step)
        c.driver->key(Qt::Key_Tab);
    c.reveal(name);
    require(slider->hasActiveFocus() && !slider->property("pressed").toBool() &&
                c.editor.currentSettings() == before,
            "Native Tab navigation missed the slider or edited its document: " + control.parameter);
}
void assertDocument(Context &c, const luma::EditDocument &expected, const QString &reason) {
    require(c.editor.currentSettings() == expected && c.editor.error().isEmpty(), reason);
}
void assertSaved(Context &c, const luma::EditDocument &expected) {
    c.saved();
    const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
    require(active && active->id == c.editor.photoID().toStdString() && active->edits == expected,
            "Latest native slider input was not the durable active document");
}
QImage expectedPixels(Context &c) {
    return fixtureReference(c);
}
bool pixelsMatch(Context &c, const QImage &expected) {
    auto *preview = c.driver->item("develop.preview");
    return preview->isVisible() && preview->property("status").toInt() == 1 &&
           preview->property("source").toUrl() == c.editor.previewUrl() &&
           c.provider->requestImage({}, nullptr, {}) == expected;
}
void waitPixels(Context &c, const QString &reason) {
    const auto expected = expectedPixels(c);
    c.driver->wait([&] { return pixelsMatch(c, expected); }, reason);
}
struct ReleaseWrites {
    SqliteFault &fault;
    ~ReleaseWrites() {
        fault.holdWrites(false);
    }
};
void retainedHistory(Context &c, const QString &id) {
    auto &d = *c.driver;
    c.reveal("curves.mode");
    d.choose("curves.mode", 1);
    require(d.object("curves.mode")->property("currentIndex").toInt() == 1,
            "Curve history fixture did not enter Parametric mode");
    const std::vector<SliderControl> controls{{"curve.shadows", -100, 100, {}},
                                              {"curve.darks", -100, 100, {}},
                                              {"curve.lights", -100, 100, {}},
                                              {"curve.highlights", -100, 100, {}},
                                              {"hsl.red.hue", -100, 100, "red"},
                                              {"hsl.orange.saturation", -100, 100, "orange"},
                                              {"hsl.yellow.luminance", -100, 100, "yellow"},
                                              {"hsl.blue.saturation", -100, 100, "blue"}};
    constexpr int edits = 500, capacity = 200, retainedBoundary = edits - capacity;
    require(!c.editor.canUndo() && !c.editor.canRedo(),
            "History-cap fixture must start with an empty session history");
    std::vector<luma::EditDocument> snapshots{c.editor.currentSettings()};
    snapshots.reserve(edits + 1);
    for (int index = 0; index < edits; ++index) {
        const auto &control = controls[static_cast<std::size_t>(index / 63)];
        if (index % 63 == 0)
            focusSlider(c, control);
        require(d.object(selector(control))->property("stepSize").toDouble() == 1,
                "Curve/HSL stress fixture requires the documented one-unit keyboard step");
        const auto next = snapshots.back().parameter(control.parameter.toStdString()) + 1;
        require(next < control.maximum,
                "History fixture exhausted a control range before 500 distinct edits");
        auto expected = snapshots.back();
        expected.setParameter(control.parameter.toStdString(), next);
        d.key(Qt::Key_Right);
        require(!d.object(selector(control))->property("pressed").toBool(),
                "History stress key left an unfinished gesture");
        assertDocument(c, expected, "One Curve/HSL keyboard step changed the wrong document");
        require(c.editor.currentSettings() != snapshots.back(),
                "History-cap fixture generated a no-op at edit " + QString::number(index + 1));
        snapshots.push_back(c.editor.currentSettings());
        d.log({{"action", "history_stress_edit"},
               {"edit", index + 1},
               {"parameter", control.parameter},
               {"value", next}});
    }
    const auto final = snapshots.back();
    assertSaved(c, final);
    waitPixels(c, "500-edit final document reached the preview");
    for (int current = edits; current > retainedBoundary; --current) {
        require(c.editor.canUndo() && d.item("main.undoButton")->isEnabled(),
                "History retained fewer than 200 valid edits");
        d.click("main.undoButton");
        assertDocument(c, snapshots[static_cast<std::size_t>(current - 1)],
                       "Retained Undo order diverged at edit " + QString::number(current));
    }
    require(!c.editor.canUndo() && !d.item("main.undoButton")->isEnabled() && c.editor.canRedo() &&
                d.item("main.redoButton")->isEnabled(),
            "History cap did not stop exactly at snapshot 300");
    d.key(Qt::Key_Z, Qt::ControlModifier);
    assertDocument(c, snapshots[retainedBoundary], "Undo crossed the retained history boundary");
    waitPixels(c, "Retained boundary document reached the preview");
    for (int next = retainedBoundary + 1; next <= edits; ++next) {
        require(c.editor.canRedo() && d.item("main.redoButton")->isEnabled(),
                "History retained fewer than 200 Redo records");
        d.click("main.redoButton");
        assertDocument(c, snapshots[static_cast<std::size_t>(next)],
                       "Retained Redo order diverged at edit " + QString::number(next));
    }
    require(!c.editor.canRedo() && !d.item("main.redoButton")->isEnabled() && c.editor.canUndo(),
            "Full Redo left an unexpected branch");
    assertSaved(c, final);
    waitPixels(c, "Full Redo restored the final preview document");
    writeJson(c.root + "/measurements.json",
              {{"scenario", id},
               {"verification", "exact EditDocument equality at every retained Undo/Redo"},
               {"native_edits", edits},
               {"documented_history_capacity", capacity},
               {"retained_boundary_edit", retainedBoundary},
               {"undo_steps", capacity},
               {"redo_steps", capacity}});
}
} // namespace

bool historyStressScenario(Context &c, const QString &id) {
    if (id != "UX-UND-016" && id != "UX-ADV-013")
        return false;
    const KeyboardNavigation navigation;
    c.openFirst();
    c.saved();
    retainedHistory(c, id);
    return true;
}

bool sliderExtraScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-SLD-012", "UX-SLD-013", "UX-SLD-014", "UX-SLD-018"};
    if (!supported.contains(id))
        return false;
    const KeyboardNavigation navigation;
    c.openFirst();
    c.saved();
    auto &d = *c.driver;
    const SliderControl exposure{"exposure", -5, 5, {}};
    if (id == "UX-SLD-012") {
        const auto before = c.editor.currentSettings();
        prepare(c, exposure);
        const ReleaseWrites release{c.storageFault};
        c.storageFault.holdWrites(true);
        d.drag(selector(exposure), d.sliderThumb(selector(exposure)), {.65, .5});
        require(c.editor.currentSettings() != before && c.editor.saving(),
                "Concurrent-drag fixture did not schedule the first save");
        d.wait([&] { return c.storageFault.writeBlocked(); }, "Actual autosave WAL write held");
        require(c.editor.saving() && !c.editor.busy(),
                "Blocked autosave did not leave the current photo editable");
        d.drag(selector(exposure), d.sliderThumb(selector(exposure)), {1.2, .5});
        auto expected = before;
        expected.exposure = 5;
        assertDocument(c, expected, "Drag during a blocked autosave lost its final value");
        c.storageFault.holdWrites(false);
        assertSaved(c, expected);
        waitPixels(c, "Drag during autosave reached its final preview");
        return true;
    }
    if (id == "UX-SLD-013") {
        prepare(c, exposure);
        const auto initial = c.editor.currentSettings();
        for (int index = 0; index < 1000; ++index) {
            d.drag(selector(exposure), d.sliderThumb(selector(exposure)),
                   {index % 2 == 1 ? 1.2 : -.2, .5}, 2);
            auto expected = initial;
            expected.exposure = index % 2 == 1 ? 5 : -5;
            assertDocument(c, expected, "Rapid Exposure input affected an unrelated field");
        }
        auto expected = initial;
        expected.exposure = 5;
        assertSaved(c, expected);
        waitPixels(c, "1000-input Exposure storm drained to its last document");
        writeJson(c.root + "/measurements.json",
                  {{"scenario", id}, {"native_pointer_gestures", 1000}, {"final_exposure", 5}});
        return true;
    }
    if (id == "UX-SLD-014") {
        // The public fixture is small. This checks bounded stale-work drainage, not
        // a 20 MP render-performance target or interaction FPS.
        auto expected = c.editor.currentSettings();
        const auto initial = expected;
        QImage beforeFinalControl;
        QElapsedTimer finalDrain;
        constexpr int movesPerControl = 100;
        for (std::size_t index = 0; index < basicControls().size(); ++index) {
            const auto &control = basicControls()[index];
            const bool finalControl = index + 1 == basicControls().size();
            if (finalControl)
                beforeFinalControl = expectedPixels(c);
            prepare(c, control);
            const auto name = selector(control);
            d.press(name, d.sliderThumb(name));
            for (int move = 0; move < movesPerControl; ++move)
                d.move(name, {move % 2 == 0 ? .25 : .75, .5});
            finalDrain.start();
            d.move(name, {1.2, .5});
            d.release(name, {1.2, .5});
            expected.setParameter(control.parameter.toStdString(), control.maximum);
            assertDocument(c, expected,
                           "Basic slider storm did not preserve the final input: " +
                               control.parameter);
            if (!finalControl) {
                // Avoid an all-white clipped final image, which could equal an
                // obsolete render. Production double-tap resets each earlier
                // parameter while the last Saturation gesture remains visible.
                QObject *tapHandler = nullptr;
                for (auto *child : d.object(name)->children()) {
                    if (QByteArray(child->metaObject()->className()).contains("TapHandler"))
                        tapHandler = child;
                }
                require(tapHandler, "Basic slider has no double-tap handler");
                const auto *meta = tapHandler->metaObject();
                int doubleSignal = -1;
                for (int method = 0; method < meta->methodCount(); ++method) {
                    if (meta->method(method).name() == "doubleTapped")
                        doubleSignal = method;
                }
                require(doubleSignal >= 0, "TapHandler doubleTapped signal is absent");
                const QSignalSpy doubleTaps(tapHandler, meta->method(doubleSignal));
                d.doubleClick(name);
                require(doubleTaps.count() == 1 && !d.object(name)->property("pressed").toBool() &&
                            !d.object(name)->property("resetRequested").toBool(),
                        "Double-tap reset was not handled exactly once or left a pending gesture");
                expected.setParameter(control.parameter.toStdString(),
                                      initial.parameter(control.parameter.toStdString()));
                d.log({{"action", "basic_storm_reset"},
                       {"parameter", control.parameter},
                       {"actual_value",
                        c.editor.currentSettings().parameter(control.parameter.toStdString())},
                       {"expected_value", expected.parameter(control.parameter.toStdString())},
                       {"actual_custom_wb", c.editor.currentSettings().customWhiteBalance},
                       {"expected_custom_wb", expected.customWhiteBalance},
                       {"pressed", d.object(name)->property("pressed").toBool()}});
                assertDocument(c, expected,
                               "Basic storm reset changed unrelated fields: " + control.parameter);
            }
        }
        const auto image = expectedPixels(c);
        require(!beforeFinalControl.isNull() && image != beforeFinalControl,
                "Storm fixture has no distinct visual effect for the last input");
        d.wait([&] { return pixelsMatch(c, image); }, "Basic storm preview drained", 2000);
        const auto elapsed = finalDrain.elapsed();
        require(elapsed <= 2000, "Basic controls left a multi-second stale preview backlog");
        assertSaved(c, expected);
        writeJson(c.root + "/measurements.json",
                  {{"scenario", id},
                   {"verification", "functional small-public-fixture stale-work drainage"},
                   {"basic_controls", static_cast<int>(basicControls().size())},
                   {"pointer_moves_per_control", movesPerControl + 1},
                   {"final_preview_drain_ms", elapsed}});
        return true;
    }
    require(id == "UX-SLD-018", "Unexpected slider adapter ID");
    for (const auto &control : basicControls()) {
        prepare(c, control);
        auto previous = c.editor.currentSettings();
        for (const bool maximum : {false, true}) {
            const auto name = selector(control);
            d.drag(name, d.sliderThumb(name), {maximum ? 1.2 : -.2, .5});
            auto expected = previous;
            expected.setParameter(control.parameter.toStdString(),
                                  maximum ? control.maximum : control.minimum);
            require(expected != previous, "Clamp fixture generated a no-op");
            assertDocument(c, expected, "Pointer outside slider did not clamp to its endpoint");
            require(d.object(name)->property("value").toDouble() ==
                        (maximum ? control.maximum : control.minimum),
                    "Slider thumb value disagrees with the clamped document");
            // Additional input beyond the same limit must remain a history no-op.
            focusSlider(c, control);
            d.key(maximum ? Qt::Key_Right : Qt::Key_Left);
            d.key(maximum ? Qt::Key_Right : Qt::Key_Left);
            assertDocument(c, expected, "Keyboard input crossed a valid parameter boundary");
            d.click("main.undoButton");
            assertDocument(c, previous, "Boundary no-op polluted the single-gesture Undo");
            d.click("main.redoButton");
            assertDocument(c, expected, "Clamped endpoint Redo changed its settings");
            previous = expected;
        }
    }
    assertSaved(c, c.editor.currentSettings());
    waitPixels(c, "All Basic boundary controls reached the final preview");
    return true;
}
} // namespace ux
