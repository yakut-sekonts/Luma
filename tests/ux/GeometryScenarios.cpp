#include "tests/ux/UxContext.h"
#include <cmath>

namespace ux {
bool geometryScenario(Context &c, const QString &id) {
    if (id == "UX-DSC-007") {
        c.openFirst();
        auto &d = *c.driver;
        auto *pane = d.item("develop.adjustmentsPane");
        auto *viewport = d.item("develop.adjustmentsScroll");
        for (const int width : {250, 400, 250}) {
            const double initial = pane->width();
            d.dragBetween("develop.adjustmentsPane", {-1 / initial, .5}, "develop.adjustmentsPane",
                          {(initial - width - 1) / initial, .5});
            d.wait([&] { return std::abs(pane->width() - width) <= 1; },
                   "SplitView width reaches requested bound");
            for (const auto *selector : {"develop.exposureSlider", "develop.saturationSlider",
                                         "cropAngleSlider", "develop.transform.horizontalSlider",
                                         "sharpen.amountSlider", "noise.colorSmoothnessSlider"}) {
                c.reveal(selector);
                auto *control = d.item(selector);
                const auto left = control->mapToItem(viewport, QPointF()).x();
                const auto right = control->mapToItem(viewport, QPointF(control->width(), 0)).x();
                d.log({{"action", "narrow_panel_control"},
                       {"id", selector},
                       {"pane_width", pane->width()},
                       {"viewport_width", viewport->width()},
                       {"left", left},
                       {"right", right}});
                require(left >= -.5 && right <= viewport->width() + .5,
                        "Adjustment control is clipped horizontally: " + QString(selector));
            }
        }
        return true;
    }
    if (id != "UX-CRP-001" && id != "UX-CRP-002" && id != "UX-UND-006")
        return false;
    auto &d = *c.driver;
    c.openFirst();
    c.reveal("cropTool");
    d.click("cropTool");
    d.wait([&] { return c.editor.cropMode() && c.editor.cropCanvasReady(); }, "Crop canvas ready");
    const QPointF grips[]{{0, 0}, {.5, 0}, {1, 0}, {0, .5}, {1, .5}, {0, 1}, {.5, 1}, {1, 1}};
    const int count = id == "UX-CRP-001" ? 8 : 1;
    for (int i = 0; i < count; ++i) {
        // An inset frame is a precondition; the acceptance input is a real drag.
        c.editor.setCropRect(.2, .2, .6, .6);
        c.saved();
        d.wait([&] { return c.editor.cropCanvasReady(); }, "inset Crop fixture ready");
        d.frame();
        const auto before = c.editor.currentSettings();
        const bool move = id == "UX-CRP-002";
        const auto start =
            move ? QPointF(.5, .5) : QPointF(.2 + .6 * grips[i].x(), .2 + .6 * grips[i].y());
        const auto finish =
            move ? QPointF(.6, .6)
                 : start + QPointF((.5 - grips[i].x()) * .2, (.5 - grips[i].y()) * .2);
        d.press("cropDragArea", start);
        for (int step = 1; step <= 12; ++step) {
            d.move("cropDragArea", start + (finish - start) * (static_cast<double>(step) / 12));
            d.frame();
        }
        d.release("cropDragArea", finish);
        d.wait([&] { return c.editor.cropCanvasReady(); }, "Crop drag final render");
        const auto after = c.editor.currentSettings();
        auto expected = before.crop;
        if (move) {
            expected.x += .1;
            expected.y += .1;
        } else {
            if (grips[i].x() == 0) {
                expected.x += .1;
                expected.width -= .1;
            }
            if (grips[i].x() == 1)
                expected.width -= .1;
            if (grips[i].y() == 0) {
                expected.y += .1;
                expected.height -= .1;
            }
            if (grips[i].y() == 1)
                expected.height -= .1;
        }
        d.log({{"action", "crop_drag_oracle"},
               {"grip", i},
               {"actual_x", after.crop.x},
               {"expected_x", expected.x},
               {"actual_y", after.crop.y},
               {"expected_y", expected.y},
               {"actual_width", after.crop.width},
               {"expected_width", expected.width}});
        require(std::abs(after.crop.x - expected.x) < .005 &&
                    std::abs(after.crop.y - expected.y) < .005 &&
                    std::abs(after.crop.width - expected.width) < .005 &&
                    std::abs(after.crop.height - expected.height) < .005,
                "Crop frame did not follow the complete pointer drag");
        d.click("main.undoButton");
        require(c.editor.currentSettings() == before, "Crop drag was not exactly one Undo");
        d.click("main.redoButton");
        require(c.editor.currentSettings() == after, "Crop Redo changed final geometry");
    }
    return true;
}
} // namespace ux
