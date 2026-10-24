#include "core/render/CropGeometry.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QSet>
#include <cmath>

namespace ux {
namespace {
void ready(Context &c) {
    c.driver->wait([&] { return c.editor.cropMode() && c.editor.cropCanvasReady(); },
                   "Crop canvas ready");
    require(c.editor.error().isEmpty(), c.editor.error());
}
void enter(Context &c) {
    c.reveal("cropTool");
    c.driver->click("cropTool");
    ready(c);
}
void valid(Context &c) {
    const auto document = c.editor.currentSettings();
    document.validate();
    const auto frame = c.editor.cropState();
    const double x = frame["x"].toDouble(), y = frame["y"].toDouble();
    const double width = frame["width"].toDouble(), height = frame["height"].toDouble();
    require(std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height) &&
                x >= 0 && y >= 0 && width > 0 && height > 0 && x + width <= 1 + 1e-9 &&
                y + height <= 1 + 1e-9,
            "Crop produced invalid visible frame bounds");
    require(frame["outputWidth"].toInt() >= 1 && frame["outputHeight"].toInt() >= 1,
            "Crop produced a zero-pixel output");
}
void undoRedo(Context &c, const luma::EditDocument &before, const luma::EditDocument &after) {
    c.driver->click("main.undoButton");
    require(c.editor.currentSettings() == before, "Crop control was not exactly one Undo");
    c.driver->click("main.redoButton");
    require(c.editor.currentSettings() == after, "Crop Redo changed final geometry");
    ready(c);
}
void inset(Context &c) {
    c.driver->drag("cropDragArea", {.001, .001}, {.15, .15});
    ready(c);
    require(c.editor.currentSettings().crop.width < 1 && c.editor.currentSettings().crop.height < 1,
            "Real crop fixture drag did not create an inset frame");
}
} // namespace
bool cropScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-CRP-003", "UX-CRP-004", "UX-CRP-005", "UX-CRP-006",
                                         "UX-CRP-007", "UX-CRP-008", "UX-CRP-009", "UX-CRP-011",
                                         "UX-CRP-012", "UX-CRP-013", "UX-CRP-014", "UX-CRP-017"};
    if (!supported.contains(id))
        return false;
    c.openFirst();
    auto &d = *c.driver;
    enter(c);
    if (id == "UX-CRP-003") {
        for (const auto target :
             {QPointF(1.2, 1.2), QPointF(-.2, -.2), QPointF(.5, .5), QPointF(.999, .001)}) {
            // Inset precondition allows crossing an opposite edge with actual input.
            c.editor.setCropRect(.2, .2, .6, .6);
            ready(c);
            d.drag("cropDragArea", {.2, .2}, target);
            valid(c);
            require(d.object("cropOverlay")->property("dragMode").toString().isEmpty(),
                    "Crossed-edge Crop retained pointer ownership");
        }
        return true;
    }
    if (id == "UX-CRP-004" || id == "UX-CRP-006") {
        const double ratios[]{0, -1, 1, 4. / 3, 3. / 2, 16. / 9};
        for (int index = 0; index < 6; ++index) {
            c.reveal("cropAspect");
            const auto before = c.editor.currentSettings();
            d.choose("cropAspect", index);
            ready(c);
            const auto after = c.editor.currentSettings();
            auto expected = before;
            expected.crop.aspect = ratios[index];
            require(after == expected, "Aspect selection changed unrelated edit settings");
            const auto frame = c.editor.cropState();
            const double ratio = ratios[index] == -1 ? 256. / 192 : ratios[index];
            if (ratio > 0) {
                auto *overlay = d.item("cropOverlay");
                const double scale = overlay->width() / frame["canvasWidth"].toDouble();
                const double width = frame["width"].toDouble() * overlay->width();
                const double height = frame["height"].toDouble() * overlay->height();
                // Each output dimension rounds by at most half a source pixel.
                const double allowance = (.5 + .5 * ratio) * scale;
                d.log({{"action", "crop_ratio_oracle"},
                       {"requested", ratio},
                       {"visible_width", width},
                       {"visible_height", height},
                       {"rounding_allowance", allowance}});
                require(std::abs(width - height * ratio) <= allowance + 1e-6,
                        "Displayed locked Crop exceeds the pixel-rounding ratio budget");
            }
            valid(c);
            if (after != before)
                undoRedo(c, before, after);
        }
        if (id == "UX-CRP-006") {
            c.reveal("cropAspectSwap");
            const auto before = c.editor.currentSettings();
            d.click("cropAspectSwap");
            auto expected = before;
            expected.crop.aspect = 1 / before.crop.aspect;
            require(c.editor.currentSettings() == expected,
                    "Aspect swap did not invert only the selected ratio");
            undoRedo(c, before, expected);
        }
        return true;
    }
    if (id == "UX-CRP-005") {
        for (const auto &ratio :
             {std::pair{QString("1"), QString("100")}, std::pair{QString("100"), QString("1")}}) {
            c.reveal("cropReset");
            d.click("cropReset");
            ready(c);
            c.reveal("cropRatioWidth");
            d.type("cropRatioWidth", ratio.first);
            c.reveal("cropRatioHeight");
            d.type("cropRatioHeight", ratio.second);
            c.reveal("cropCustomApply");
            d.click("cropCustomApply");
            ready(c);
            require(c.editor.currentSettings().crop.aspect ==
                        ratio.first.toDouble() / ratio.second.toDouble(),
                    "Extreme custom Crop ratio was not applied");
            const auto frame = c.editor.cropState();
            const auto before = c.editor.currentSettings();
            const auto *area = d.item("cropDragArea");
            // Window input is rounded to integer scene coordinates. Stay one
            // logical pixel inside the item while still hitting its corner grip.
            const QPointF start(std::max(frame["x"].toDouble(), 1. / area->width()),
                                std::max(frame["y"].toDouble(), 1. / area->height()));
            const bool wide = before.crop.aspect > 1;
            const QPointF finish = start + (wide ? QPointF(.2, 0) : QPointF(0, .2));
            d.press("cropDragArea", start);
            require(d.object("cropDragArea")->property("pressed").toBool() &&
                        d.object("cropOverlay")->property("dragMode").toString() == "lt",
                    "Extreme Crop input did not acquire its actual top-left grip");
            d.log({{"action", "extreme_crop_corner_press"},
                   {"aspect", before.crop.aspect},
                   {"frame", QJsonObject::fromVariantMap(frame)},
                   {"pressed", d.object("cropDragArea")->property("pressed").toBool()},
                   {"drag_mode", d.object("cropOverlay")->property("dragMode").toString()}});
            for (int step = 1; step <= 12; ++step)
                d.move("cropDragArea", start + (finish - start) * (step / 12.));
            d.release("cropDragArea", finish);
            valid(c);
            const auto moved = c.editor.cropState();
            const auto dimension = wide ? "outputWidth" : "outputHeight";
            require(c.editor.currentSettings() != before &&
                        moved[dimension].toInt() < frame[dimension].toInt(),
                    "Extreme custom Crop frame cannot be manipulated along its long dimension");
        }
        return true;
    }
    inset(c);
    if (id == "UX-CRP-007" || id == "UX-CRP-008") {
        for (const auto &selector : id == "UX-CRP-007"
                                        ? QStringList{"cropRotateRight", "cropRotateLeft"}
                                        : QStringList{"cropFlipHorizontal", "cropFlipVertical"}) {
            c.reveal(selector);
            const auto before = c.editor.currentSettings();
            d.click(selector);
            auto expected = before;
            if (selector == QString("cropRotateRight"))
                expected.crop.quarterTurns = (before.crop.quarterTurns + 1) % 4;
            else if (selector == QString("cropRotateLeft"))
                expected.crop.quarterTurns = (before.crop.quarterTurns + 3) % 4;
            else if (selector == QString("cropFlipHorizontal"))
                expected.crop.flipHorizontal = !before.crop.flipHorizontal;
            else
                expected.crop.flipVertical = !before.crop.flipVertical;
            require(c.editor.currentSettings() == expected,
                    "Rotate/flip changed unrelated Crop fields");
            undoRedo(c, before, expected);
            valid(c);
        }
        return true;
    }
    if (id == "UX-CRP-009") {
        c.reveal("cropAngleSlider");
        const auto before = c.editor.currentSettings();
        d.drag("cropAngleSlider", d.sliderThumb("cropAngleSlider"), {.65, .5});
        const auto after = c.editor.currentSettings();
        auto expected = before;
        expected.crop.angle = after.crop.angle;
        require(after != before && after == expected,
                "Straighten changed fields outside its angle");
        undoRedo(c, before, after);
        return true;
    }
    if (id == "UX-CRP-012") {
        const auto source = c.editor.sourcePath();
        const auto photo = c.editor.photoID();
        const auto cropped = c.editor.currentSettings();
        d.click(c.card(1, true));
        d.wait([&] { return c.editor.sourcePath() != source && !c.editor.busy(); },
               "Crop photo switch complete");
        c.loaded();
        require(!c.editor.cropMode() && !d.item("cropOverlay")->isVisible() &&
                    d.object("cropOverlay")->property("dragMode").toString().isEmpty() &&
                    c.editor.currentSettings() == luma::EditDocument{} && !c.editor.canUndo(),
                "Crop transient state/history crossed the photo boundary");
        const auto saved =
            luma::Catalog(c.catalogFile.toStdString()).assetByID(photo.toStdString());
        require(saved && saved->edits == cropped, "Photo switch lost the previous saved Crop");
        return true;
    }
    c.saved();
    const auto document = c.editor.currentSettings();
    if (id == "UX-CRP-011") {
        d.key(Qt::Key_Backslash);
        require(c.editor.original() && !c.editor.cropMode() &&
                    !d.item("cropOverlay")->isVisible() && c.editor.currentSettings() == document &&
                    d.object("cropOverlay")->property("dragMode").toString().isEmpty(),
                "Original left a Crop overlay/gesture or changed geometry");
        d.click("develop.processedButton");
        require(c.editor.currentSettings() == document, "Processed comparison lost saved Crop");
        return true;
    }
    if (id == "UX-CRP-013") {
        d.move("filter_soft-portrait", {.5, .5});
        d.wait([&] { return c.editor.presetPreview(); }, "Crop preset hover entered");
        // Documented preset preview closes transient tools; geometry must remain intact.
        require(!c.editor.cropMode() && !d.item("cropOverlay")->isVisible() &&
                    c.editor.currentSettings() == document,
                "Preset hover corrupted saved Crop/transient tool state");
        d.move("develop.preview", {.5, .5});
        const auto reference = fixtureReference(c);
        d.wait(
            [&] {
                return !c.editor.presetPreview() &&
                       c.provider->requestImage({}, nullptr, {}) == reference;
            },
            "Crop preview restored after hover");
        enter(c);
        require(c.editor.currentSettings() == document, "Reopening Crop after hover lost geometry");
        return true;
    }
    if (id == "UX-CRP-014") {
        const auto image = c.provider->requestImage({}, nullptr, {});
        const auto url = c.editor.previewUrl();
        const auto before = luma::Catalog(c.catalogFile.toStdString())
                                .assetSnapshot(c.editor.photoID().toStdString());
        require(before.has_value(), "Crop guide fixture has no persisted asset");
        const QStringList guides{"thirds", "grid", "diagonal", "golden"};
        for (int index = 0; index < guides.size(); ++index) {
            c.reveal("cropGuide");
            d.choose("cropGuide", index);
            require(c.editor.cropOverlay() == guides[index] &&
                        d.object("cropOverlay")->property("guide").toString() == guides[index] &&
                        c.editor.currentSettings() == document && !c.editor.saving() &&
                        c.editor.previewUrl() == url &&
                        c.provider->requestImage({}, nullptr, {}) == image,
                    "Display-only Crop guide changed edits/render/autosave");
        }
        const auto after = luma::Catalog(c.catalogFile.toStdString())
                               .assetSnapshot(c.editor.photoID().toStdString());
        require(after && before->revision == after->revision &&
                    before->asset.edits == after->asset.edits,
                "Crop guides changed persisted revision");
        return true;
    }
    require(id == "UX-CRP-017", "Unhandled Crop scenario");
    c.reveal("cropAngleSlider");
    d.drag("cropAngleSlider", d.sliderThumb("cropAngleSlider"), {.6, .5});
    const auto final = c.editor.currentSettings();
    c.reveal("cropTool");
    d.click("cropTool");
    require(!c.editor.cropMode(), "Done did not close Crop");
    const auto immediate = c.exportPNG("immediate-crop");
    c.saved();
    const auto settled = c.exportPNG("settled-crop");
    const luma::CropGeometry geometry(256, 192, 0, final);
    require(c.editor.currentSettings() == final && QImage(immediate) == QImage(settled) &&
                QImage(immediate).size() == QSize(geometry.width(), geometry.height()),
            "Immediate export did not use final saved Crop geometry");
    return true;
}
} // namespace ux
