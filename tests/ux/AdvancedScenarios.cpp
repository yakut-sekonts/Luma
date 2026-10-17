#include "tests/ux/UxContext.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>

namespace ux {
namespace {
void undoRedo(Context &c, const luma::EditDocument &before, const luma::EditDocument &after) {
    c.driver->click("main.undoButton");
    require(c.editor.currentSettings() == before, "Advanced gesture was not one Undo");
    c.driver->click("main.redoButton");
    require(c.editor.currentSettings() == after, "Advanced Redo changed document");
}
void target(Context &c, bool hsl) {
    if (hsl) {
        c.reveal("hsl.targetMode");
        c.driver->choose("hsl.targetMode", 1);
    }
    const QString name = hsl ? "hsl.target" : "curves.target";
    c.reveal(name);
    c.driver->click(name);
    require(c.editor.targetedTool() == (hsl ? "hsl.saturation" : "curve.rgb"),
            "Targeted UI did not select expected tool");
}
} // namespace
bool advancedScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-ADV-001", "UX-ADV-002", "UX-ADV-004", "UX-ADV-005",
                                         "UX-ADV-006", "UX-ADV-007", "UX-ADV-008", "UX-ADV-010",
                                         "UX-ADV-011", "UX-ADV-014", "UX-UND-009"};
    if (!supported.contains(id))
        return false;
    auto &d = *c.driver;
    c.openFirst();
    if (id == "UX-UND-009") {
        for (const bool hsl : {false, true}) {
            target(c, hsl);
            const auto before = c.editor.currentSettings();
            d.drag("develop.targetedArea", {.5, .5}, {.5, .35});
            const auto after = c.editor.currentSettings();
            auto expected = before;
            if (hsl)
                expected.hsl = after.hsl;
            else
                expected.curves[0] = after.curves[0];
            require(after != before && after == expected,
                    "Targeted history fixture changed an unexpected group");
            undoRedo(c, before, after);
        }
        return true;
    }
    if (id == "UX-ADV-001" || id == "UX-ADV-002" || id == "UX-ADV-014") {
        c.reveal("curves.graph");
        const auto before = c.editor.currentSettings();
        auto *flickable = qvariant_cast<QObject *>(
            d.object("develop.adjustmentsScroll")->property("contentItem"));
        const auto scrollBefore = flickable->property("contentY").toDouble();
        d.press("curves.gesture", {.45, .55});
        for (int i = 1; i <= 12; ++i) {
            d.move("curves.gesture",
                   QPointF(.45, .55) + QPointF(.2, -.3) * (static_cast<double>(i) / 12));
            d.frame();
            if (i == 6) {
                const auto *grabber = c.window->mouseGrabberItem();
                d.log({{"action", "curve_pointer_owner"},
                       {"grabber", grabber ? QString::fromUtf8(grabber->metaObject()->className())
                                           : QString{}},
                       {"dragging", d.object("curves.gesture")->property("dragging").toBool()},
                       {"point_count", d.object("curves.gesture")->property("pointCount").toInt()},
                       {"scroll_before", scrollBefore},
                       {"scroll_now", flickable->property("contentY").toDouble()}});
            }
        }
        d.release("curves.gesture", {.65, .25});
        d.log({{"action", "curve_final_points"},
               {"points", QJsonArray::fromVariantList(c.editor.curvePoints()["rgb"].toList())},
               {"selected", d.object("curves.panel")->property("selected").toInt()}});
        const auto after = c.editor.currentSettings();
        const auto &points = after.curves[0].points;
        require(points.size() == 3 && std::abs(points[1].x - .65) < .005 &&
                    std::abs(points[1].y - .75) < .005,
                "Curve point did not follow complete pointer gesture");
        undoRedo(c, before, after);
        if (id == "UX-ADV-014") {
            require(d.object("curves.panel")->property("selected").toInt() == 1,
                    "Curve selection fixture is not selected");
            c.saved();
            const auto source = c.editor.sourcePath();
            d.click(c.card(1, true));
            d.wait([&] { return c.editor.sourcePath() != source && !c.editor.busy(); },
                   "curve asset switch");
            c.loaded();
            require(d.object("curves.panel")->property("selected").toInt() == -1,
                    "Selected curve point leaked into another photo");
            return true;
        }
        c.reveal("curves.graph");
        if (id == "UX-ADV-002") {
            d.drag("curves.gesture", {.65, .25}, {-.2, 1.2});
            const auto clamped = c.editor.currentSettings();
            clamped.validate();
            require(clamped.curves[0].points[1].x <= .002 && clamped.curves[0].points[1].y == 0,
                    "Outside curve drag was not clamped");
        } else {
            d.press("curves.gesture", {.65, .25});
            d.release("curves.gesture", {.65, .25});
            d.key(Qt::Key_Delete);
            require(c.editor.currentSettings() == before,
                    "Delete curve point did not remove only the selected point");
            d.click("main.undoButton");
            require(c.editor.currentSettings() == after, "Deleting curve point was not one Undo");
        }
        return true;
    }
    if (id == "UX-ADV-004") {
        c.reveal("curves.mode");
        d.choose("curves.mode", 1);
        for (const auto *parameter : {"highlights", "lights", "darks", "shadows"}) {
            const auto selector = "develop.curve." + QString(parameter) + "Slider";
            c.reveal(selector);
            const auto before = c.editor.currentSettings();
            d.drag(selector, d.sliderThumb(selector), {.65, .5});
            const auto after = c.editor.currentSettings();
            require(after != before, "Parametric curve drag lost");
            undoRedo(c, before, after);
        }
        return true;
    }
    if (id == "UX-ADV-005") {
        for (std::size_t i = 0; i < luma::hslBands.size(); ++i) {
            const auto band = QString::fromUtf8(luma::hslBands[i].data(),
                                                static_cast<qsizetype>(luma::hslBands[i].size()));
            c.reveal("hsl.band." + band);
            d.click("hsl.band." + band);
            const auto selector = "develop.hsl." + band + ".hueSlider";
            c.reveal(selector);
            const auto before = c.editor.currentSettings();
            d.drag(selector, d.sliderThumb(selector), {.65, .5});
            const auto after = c.editor.currentSettings();
            auto expected = before;
            expected.hsl[i].hue = after.hsl[i].hue;
            require(after != before && after == expected,
                    "HSL band change edited another band/parameter");
            undoRedo(c, before, after);
        }
        return true;
    }
    target(c, id == "UX-ADV-007");
    if (id == "UX-ADV-010") {
        c.reveal("cropTool");
        d.click("cropTool");
        require(c.editor.cropMode() && c.editor.targetedTool() == "none" && !c.editor.guidedMode(),
                "Crop and targeted tools both own the pointer");
        target(c, false);
        require(!c.editor.cropMode(), "Targeted tool retained Crop capture");
        c.reveal("transformGuided");
        d.click("transformGuided");
        require(c.editor.guidedMode() && c.editor.targetedTool() == "none" && !c.editor.cropMode(),
                "Guided and targeted tools both own the pointer");
        return true;
    }
    const auto before = c.editor.currentSettings();
    if (id == "UX-ADV-008") {
        auto *image = d.item("develop.preview");
        d.frame();
        const double gapX = image->width() - image->property("paintedWidth").toDouble();
        const double gapY = image->height() - image->property("paintedHeight").toDouble();
        require(gapX > 4 || gapY > 4, "Outside-image fixture has no letterbox");
        const auto outside = gapX > 4 ? QPointF(gapX / image->width() / 4, .5)
                                      : QPointF(.5, gapY / image->height() / 4);
        d.press("develop.targetedArea", outside);
        d.release("develop.targetedArea", outside);
        require(c.editor.currentSettings() == before && !c.editor.canUndo(),
                "Targeted gesture outside painted image changed edits");
        return true;
    }
    if (id == "UX-ADV-011") {
        d.press("develop.targetedArea", {.5, .5});
        d.move("develop.targetedArea", {.5, .4});
        const auto partial = c.editor.currentSettings();
        require(partial != before, "Targeted Original fixture did not start");
        d.key(Qt::Key_Backslash);
        require(c.editor.original(), "Original shortcut did not activate during targeted gesture");
        d.log(
            {{"action", "targeted_original_boundary"},
             {"points_before", QJsonArray::fromVariantList(c.editor.curvePoints()["rgb"].toList())},
             {"adjusting", d.object("develop.targetedArea")->property("adjusting").toBool()}});
        d.move("develop.targetedArea", {.5, .2});
        d.release("develop.targetedArea", {.5, .2});
        d.log(
            {{"action", "targeted_original_after_pointer"},
             {"points_after", QJsonArray::fromVariantList(c.editor.curvePoints()["rgb"].toList())},
             {"original", c.editor.original()}});
        require(c.editor.currentSettings() == partial, "Original allowed targeted mutation");
        d.click("develop.processedButton");
        d.click("main.undoButton");
        require(c.editor.currentSettings() == before,
                "Original transition broke targeted gesture history");
        return true;
    }
    d.drag("develop.targetedArea", {.5, .5}, {.5, .35});
    const auto after = c.editor.currentSettings();
    auto expected = before;
    if (id == "UX-ADV-007")
        expected.hsl = after.hsl;
    else
        expected.curves[0] = after.curves[0];
    require(after != before && after == expected,
            "Targeted drag did not change exactly its declared group");
    undoRedo(c, before, after);
    return true;
}
} // namespace ux
