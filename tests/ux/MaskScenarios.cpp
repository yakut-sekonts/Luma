#include "core/render/CropGeometry.h"
#include "core/render/MaskCoverage.h"
#include "tests/ux/UxContext.h"
#include <QEvent>
#include <QGuiApplication>
#include <QImage>
#include <QSet>
#include <algorithm>
#include <cmath>
namespace ux {
namespace {
void ready(Context &c) {
    c.driver->wait([&] { return c.editor.localCanvasReady() && !c.editor.localSampling(); },
                   "mask canvas ready");
    require(c.editor.error().isEmpty(), c.editor.error());
}
void undoRedo(Context &c, const luma::EditDocument &before, const luma::EditDocument &after) {
    c.driver->click("main.undoButton");
    require(c.editor.currentSettings() == before, "Mask interaction was not exactly one Undo");
    c.driver->click("main.redoButton");
    require(c.editor.currentSettings() == after, "Mask Redo lost document data");
}
void stroke(Context &c, QPointF from = {.2, .3}, QPointF to = {.8, .7}) {
    ready(c);
    c.driver->drag("localMaskGestureArea", from, to);
    ready(c);
    require(!c.editor.localGestureActive(), "Brush gesture remained active after release");
}
} // namespace
bool maskScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{
        "UX-MSK-001", "UX-MSK-002", "UX-MSK-003", "UX-MSK-004", "UX-MSK-006", "UX-MSK-007",
        "UX-MSK-009", "UX-MSK-010", "UX-MSK-011", "UX-MSK-013", "UX-MSK-014", "UX-MSK-015",
        "UX-MSK-016", "UX-MSK-017", "UX-MSK-019", "UX-MSK-020", "UX-MSK-021", "UX-MSK-022",
        "UX-MSK-024", "UX-MSK-026", "UX-MSK-027", "UX-MSK-028", "UX-MSK-029", "UX-MSK-031",
        "UX-MSK-032", "UX-MSK-033", "UX-MSK-034", "UX-MSK-035", "UX-MSK-036", "UX-MSK-037",
        "UX-MSK-038"};
    if (!supported.contains(id))
        return false;
    auto &d = *c.driver;
    c.openFirst();
    QString baseline;
    if (id == "UX-MSK-033")
        baseline = c.exportPNG("baseline");
    if (id == "UX-MSK-020") {
        // Geometry is a fixture precondition here; brush interaction itself uses real input.
        c.editor.setCropRect(.1, .08, .8, .84);
        c.editor.setParameter("transform.rotate", 9);
        c.editor.setParameter("transform.horizontal", 12);
        c.saved();
        d.log({{"action", "fixture_geometry"}, {"crop", true}, {"transform", true}});
    }
    const int kind = id == "UX-MSK-021" || id == "UX-MSK-024"   ? 0
                     : id == "UX-MSK-022"                       ? 1
                     : id == "UX-MSK-026" || id == "UX-MSK-029" ? 3
                     : id == "UX-MSK-027" || id == "UX-MSK-028" ? 4
                                                                : 2;
    c.addMask(kind);
    const auto created = c.editor.currentSettings();
    require(created.localMasks.size() == 1 &&
                d.object("localMaskChoice")->property("currentIndex").toInt() == 0 &&
                d.item("localPhotoOverlay")->isVisible(),
            "New mask missing from list or photo editor");
    if (id == "UX-MSK-001")
        return true;
    if (id == "UX-MSK-002" || id == "UX-MSK-035") {
        if (id == "UX-MSK-035") {
            c.reveal("local_exposure");
            d.drag("local_exposure", {.5, .5}, {.65, .5});
        }
        const auto first = c.editor.currentSettings().localMasks[0];
        c.addMask(1);
        require(c.editor.localState()["id"].toString().toStdString() ==
                    c.editor.currentSettings().localMasks.back().id,
                "New mask did not become selected");
        const auto both = c.editor.currentSettings();
        c.reveal("localMaskChoice");
        d.choose("localMaskChoice", 0);
        require(c.editor.localState()["id"].toString().toStdString() == first.id &&
                    c.editor.localState()["exposure"].toDouble() == first.adjustments[0],
                "Mask selection retained another mask's controls");
        require(d.object("local_exposure")->property("value").toDouble() == first.adjustments[0] &&
                    c.editor.currentSettings() == both,
                "Selection changed edits or left stale slider binding");
        return true;
    }
    if (id == "UX-MSK-003") {
        c.reveal("localMaskName");
        d.type("localMaskName", "Маска 🌅");
        d.key(Qt::Key_Return);
        c.saved();
        require(c.editor.currentSettings().localMasks[0].name == "Маска 🌅",
                "Mask rename did not persist Unicode input");
        return true;
    }
    if (id == "UX-MSK-004" || id == "UX-MSK-006") {
        const QString control = id == "UX-MSK-004" ? "localMaskDelete" : "localMaskInvert";
        c.reveal(control);
        d.click(control);
        const auto after = c.editor.currentSettings();
        require(id == "UX-MSK-004" ? after.localMasks.empty() : after.localMasks[0].inverted,
                "Mask delete/invert click lost");
        undoRedo(c, created, after);
        return true;
    }
    if (id == "UX-MSK-007") {
        for (int operation = 0; operation < 3; ++operation)
            c.addComponent(operation, operation);
        const auto doc = c.editor.currentSettings();
        for (int i = 0; i < 3; ++i)
            require(doc.localMasks[0].components[static_cast<std::size_t>(i)].operation ==
                        static_cast<luma::MaskOperation>(i),
                    "Boolean component hierarchy lost order");
        require(luma::decodeLocalMasks(luma::encodeLocalMasks(doc.localMasks)) == doc.localMasks,
                "Boolean hierarchy did not round-trip");
        c.reveal("localComponentUp");
        d.click("localComponentUp");
        require(c.editor.currentSettings().localMasks[0].components[1].id ==
                    doc.localMasks[0].components[2].id,
                "Component reordering did not move selected operand");
        undoRedo(c, doc, c.editor.currentSettings());
        return true;
    }
    if (id == "UX-MSK-009") {
        stroke(c);
        const auto source = c.editor.sourcePath();
        const auto other = c.card(1, true);
        d.click(other);
        d.wait(
            [&] {
                return !c.editor.busy() && c.editor.sourcePath() != source &&
                       !c.editor.previewUrl().isEmpty();
            },
            "second asset active");
        require(c.editor.currentSettings().localMasks.empty() && !c.editor.localMode() &&
                    !d.item("localPhotoOverlay")->isVisible(),
                "Previous asset mask overlay leaked into new photo");
        return true;
    }
    if (id == "UX-MSK-010") {
        stroke(c);
        const auto before = c.editor.currentSettings();
        d.click("develop.originalButton");
        require(c.editor.original() && !d.item("localPhotoOverlay")->isVisible() &&
                    c.editor.currentSettings() == before,
                "Original mutated or displayed mask overlay");
        d.click("develop.processedButton");
        require(c.editor.currentSettings() == before, "Original transition lost mask data");
        return true;
    }
    if (id == "UX-MSK-014" || id == "UX-MSK-015" || id == "UX-MSK-019") {
        if (id == "UX-MSK-019") {
            d.press("localMaskGestureArea", {.5, -.03});
            d.release("localMaskGestureArea", {.5, -.03});
            require(c.editor.currentSettings() == created, "Outside-image brush changed edits");
        } else {
            d.press("localMaskGestureArea", {.4, .4});
            d.move("localMaskGestureArea", {.6, .5});
            if (id == "UX-MSK-015") {
                QEvent event(QEvent::ApplicationDeactivate);
                d.log({{"action", "application_deactivate_event"}});
                QCoreApplication::sendEvent(QGuiApplication::instance(), &event);
                require(!c.editor.localGestureActive(),
                        "Application deactivation left active brush gesture");
                d.release("localMaskGestureArea", {.6, .5});
            } else {
                d.move("localMaskGestureArea", {1.03, .5});
                d.release("localMaskGestureArea", {1.03, .5});
            }
            require(!c.editor.localGestureActive() &&
                        c.editor.currentSettings().localMasks[0].strokes.size() == 1,
                    "Outside release/deactivation lost or left brush active");
            undoRedo(c, created, c.editor.currentSettings());
        }
        return true;
    }
    if (id == "UX-MSK-016") {
        c.reveal("localBrushSize");
        d.drag("localBrushSize", {.08, .5}, {.2, .5});
        c.reveal("localBrushOpacity");
        d.drag("localBrushOpacity", {1, .5}, {.5, .5});
        c.reveal("local_feather");
        d.drag("local_feather", {.75, .5}, {.4, .5});
        const auto before = c.editor.currentSettings();
        stroke(c);
        const auto mask = c.editor.currentSettings().localMasks[0];
        require(std::abs(mask.strokes[0].radius - c.editor.localBrushSize() / 200) < 1e-9 &&
                    mask.strokes[0].opacity == c.editor.localBrushOpacity() &&
                    mask.feather == before.localMasks[0].feather,
                "Brush control changes not captured by stroke");
        return true;
    }
    if (id == "UX-MSK-017") {
        d.press("localMaskGestureArea", {.2, .2});
        for (int i = 0; i < 10; ++i)
            d.move("localMaskGestureArea", {i % 2 ? .2 : .8, .2 + i * .06});
        d.release("localMaskGestureArea", {.2, .8});
        ready(c);
        const auto mask = c.editor.currentSettings().localMasks[0];
        require(mask.strokes.size() == 1 && mask.strokes[0].points.size() >= 10,
                "Fast zigzag missed pointer segments");
        const auto &a = mask.strokes[0].points[0], &b = mask.strokes[0].points[1];
        require(luma::MaskCoverage(mask, 256.0 / 192).coverage((a.x + b.x) / 2, (a.y + b.y) / 2) >
                    .9,
                "Continuous fast brush has a gap between pointer samples");
        undoRedo(c, created, c.editor.currentSettings());
        return true;
    }
    if (id == "UX-MSK-020") {
        d.frame();
        auto *area = d.item("localMaskGestureArea");
        const auto eventPoint = area->mapFromScene(d.point("localMaskGestureArea", {.3, .35}));
        const double nx = eventPoint.x() / area->width(), ny = eventPoint.y() / area->height();
        const auto asset = luma::Catalog((c.root + "/catalog.sqlite").toStdString()).activeAsset();
        require(asset && asset->metadata.orientation == 0, "Unexpected generated PNG orientation");
        const luma::CropGeometry geometry(256, 192, 0, c.editor.currentSettings());
        const auto expected =
            geometry.source(std::clamp(nx * geometry.width() - .5, 0.0, geometry.width() - 1.0),
                            std::clamp(ny * geometry.height() - .5, 0.0, geometry.height() - 1.0));
        stroke(c, {.3, .35}, {.7, .65});
        const auto actual = c.editor.currentSettings().localMasks[0].strokes[0].points[0];
        d.log({{"action", "brush_mapping"},
               {"actual_x", actual.x},
               {"actual_y", actual.y},
               {"expected_x", (expected[0] + .5) / 256},
               {"expected_y", (expected[1] + .5) / 192}});
        require(std::abs(actual.x - (expected[0] + .5) / 256) < 1e-6 &&
                    std::abs(actual.y - (expected[1] + .5) / 192) < 1e-6,
                "Brush coordinate mapping differs from inverse Crop/Transform");
        return true;
    }
    if (id == "UX-MSK-021" || id == "UX-MSK-022" || id == "UX-MSK-024") {
        d.drag("localMaskGestureArea", {.2, .2}, {.8, .65});
        ready(c);
        const auto after = c.editor.currentSettings();
        require(after != created && after.localMasks[0].x1 < .3 && after.localMasks[0].y1 < .3,
                "Gradient pointer redraw failed");
        undoRedo(c, created, after);
        ready(c);
        const auto handle = c.editor.localHandles()[0].toMap();
        const QPointF from(handle["x"].toDouble(), handle["y"].toDouble());
        const QPointF to(.3, .3);
        d.press("localMaskGestureArea", from);
        d.log({{"action", "gradient_handle_press_state"},
               {"local_canvas_ready", c.editor.localCanvasReady()},
               {"gesture_active", c.editor.localGestureActive()},
               {"busy", c.editor.busy()},
               {"area_enabled", d.item("localMaskGestureArea")->isEnabled()}});
        require(c.editor.localGestureActive(), "Ready gradient handle press was not accepted");
        for (int step = 1; step <= 12; ++step)
            d.move("localMaskGestureArea", from + (to - from) * (static_cast<double>(step) / 12));
        d.release("localMaskGestureArea", to);
        ready(c);
        require(c.editor.currentSettings() != after, "Gradient handle did not move");
        if (id == "UX-MSK-022") {
            c.reveal("local_angle");
            const auto beforeRotation = c.editor.currentSettings();
            d.drag("local_angle", {.5, .5}, {.65, .5});
            require(c.editor.currentSettings().localMasks[0].angle > 0,
                    "Radial rotation slider did not rotate ellipse");
            undoRedo(c, beforeRotation, c.editor.currentSettings());
        }
        return true;
    }
    if (id == "UX-MSK-026" || id == "UX-MSK-027" || id == "UX-MSK-028" || id == "UX-MSK-029") {
        if (id == "UX-MSK-028") {
            d.press("localMaskGestureArea", {.5, -.03});
            d.release("localMaskGestureArea", {.5, -.03});
            require(c.editor.currentSettings() == created && c.editor.error().isEmpty(),
                    "Outside range sample changed state or reported an error");
        } else if (id == "UX-MSK-029") {
            c.reveal("local_rangeLow");
            d.drag("local_rangeLow", {.33, .5}, {.5, .5});
            require(c.editor.currentSettings() != created, "Range threshold drag was lost");
            undoRedo(c, created, c.editor.currentSettings());
        } else {
            d.click("localMaskGestureArea");
            d.wait(
                [&] { return !c.editor.localSampling() && c.editor.currentSettings() != created; },
                "range sample accepted");
            require(c.editor.error().isEmpty(), c.editor.error());
            undoRedo(c, created, c.editor.currentSettings());
        }
        return true;
    }
    stroke(c);
    const auto painted = c.editor.currentSettings();
    require(painted.localMasks[0].strokes.size() == 1 &&
                painted.localMasks[0].strokes[0].points.size() >= 2,
            "Real brush pointer input did not paint continuous stroke");
    if (id == "UX-MSK-011" || id == "UX-MSK-013") {
        undoRedo(c, created, painted);
        return true;
    }
    if (id == "UX-MSK-031") {
        const auto withOverlay = c.provider->requestImage({}, nullptr, {});
        c.reveal("localOverlayToggle");
        d.click("localOverlayToggle");
        ready(c);
        const auto withoutOverlay = c.provider->requestImage({}, nullptr, {});
        require(c.editor.currentSettings() == painted && withOverlay != withoutOverlay,
                "Overlay toggle persisted edits or made no visible change");
        return true;
    }
    if (id == "UX-MSK-032" || id == "UX-MSK-037") {
        const auto first = c.exportPNG("overlay-a");
        c.reveal("localEditOnPhoto");
        if (!c.editor.localMode())
            d.click("localEditOnPhoto");
        ready(c);
        c.reveal("localOverlayColor.#00cbea");
        d.click("localOverlayColor.#00cbea");
        c.reveal("localOverlayOpacity");
        d.drag("localOverlayOpacity", {.35, .5}, {.8, .5});
        ready(c);
        require(c.editor.currentSettings() == painted,
                "Overlay color/opacity changed mask document");
        const auto second = c.exportPNG("overlay-b");
        QImage a(first), b(second);
        require(!a.isNull() && a == b, "Export contains transient mask overlay");
        return true;
    }
    if (id == "UX-MSK-033" || id == "UX-MSK-034" || id == "UX-MSK-036") {
        c.reveal("local_exposure");
        d.drag("local_exposure", {.5, .5}, {.65, .5});
        const auto adjusted = c.editor.currentSettings();
        require(adjusted.localMasks[0].adjustments[0] > 0,
                "Local Exposure slider did not change adjustment");
        undoRedo(c, painted, adjusted);
        if (id == "UX-MSK-036") {
            c.reveal("localMaskEnabled");
            d.click("localMaskEnabled");
            const auto disabled = c.editor.currentSettings();
            auto retained = disabled.localMasks[0];
            retained.enabled = true;
            require(!disabled.localMasks[0].enabled && retained == adjusted.localMasks[0],
                    "Disabling mask discarded saved settings");
        } else if (id == "UX-MSK-033") {
            const QImage before(baseline), after(c.exportPNG("masked"));
            require(!before.isNull() && before.size() == after.size(),
                    "Masked export has invalid dimensions");
            const luma::MaskCoverage coverage(adjusted.localMasks[0], 256.0 / 192);
            int changed = 0;
            for (int y = 0; y < before.height(); ++y)
                for (int x = 0; x < before.width(); ++x) {
                    if (before.pixel(x, y) == after.pixel(x, y))
                        continue;
                    ++changed;
                    require(coverage.coverage((x + .5) / before.width(),
                                              (y + .5) / before.height()) > 0,
                            "Local Exposure changed an unmasked pixel");
                }
            require(changed > 0 && changed < before.width() * before.height() / 2,
                    "Local adjustment did not stay within masked region");
        }
        return true;
    }
    if (id == "UX-MSK-038") {
        d.click("develop.originalButton");
        require(!c.editor.localMode(), "Original did not deactivate mask editor");
        d.click("develop.processedButton");
        c.reveal("localEditOnPhoto");
        d.click("localEditOnPhoto");
        ready(c);
        c.exportPNG("transition");
        require(!c.editor.localMode() && c.editor.currentSettings() == painted,
                "Export transition retained overlay editing or mutated masks");
        return true;
    }
    throw std::runtime_error("Supported mask scenario has no assertions: " + id.toStdString());
}
} // namespace ux
