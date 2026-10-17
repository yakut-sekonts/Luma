#include "apps/shared/PhotoDecoder.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/DisplayProfile.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxDistribution.h"
#include <QColorSpace>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPointer>
#include <QSet>
#include <QTest>
#include <cstdlib>
#include <functional>

namespace ux {
namespace {
using Predicate = std::function<bool()>;
QJsonObject summary(const Distribution &values) {
    return {{"samples", static_cast<qint64>(values.count())},
            {"p50_ms", values.percentile(.5)},
            {"p95_ms", values.percentile(.95)},
            {"p99_ms", values.percentile(.99)},
            {"maximum_ms", values.maximum()}};
}
QJsonObject metadata(const QString &id, const QString &scope) {
    return {{"scenario", id},
            {"mode", "MEASURE_ONLY"},
            {"scope", scope},
            {"image_processing", "current CPU pipeline; no phase 0.6 GPU-processing claim"},
            {"fixture", "generated opaque 256x192 public PNG"},
            {"acknowledgment", "QML Image Ready/current source, exact provider pixels where "
                               "applicable, post-input polish and scene-region readback within one "
                               "RGBA8 quantization level"},
            {"readback_included", true},
            {"display_scanout_measured", false},
            {"scene_rgba8_channel_tolerance", 1},
            {"provider_pixels_tolerance", 0},
            {"quantile_resolution_ms", 1},
            {"poll_interval_ms", 10},
            {"quantiles", "conservative 1 ms Distribution bins"},
            {"precondition", "native Cocoa, idle host; no concurrent build/tidy/CTest"},
            {"performance_thresholds_applied", false}};
}
QImage referenceFor(Context &c, const QString &path, const luma::EditDocument &document) {
    const auto decoded = luma::PhotoDecoder().decode(path.toStdString());
    require(decoded.image.width == 256 && decoded.image.height == 192 &&
                decoded.image.alpha.empty(),
            "Current performance oracle requires the small opaque generated PNG fixture");
    const auto profile = luma::macos::displayProfile(c.window->winId());
    const auto pixels =
        luma::renderCPU(decoded.image, document, decoded.metadata.orientation, profile, {},
                        decoded.calibration, luma::DetailDiagnostic::none, 1, decoded.metadata);
    return QImage(pixels.rgba.data(), pixels.width, pixels.height,
                  static_cast<qsizetype>(pixels.width) * 4, QImage::Format_RGBA8888)
        .copy();
}
bool previewReady(Context &c, const QImage &pixels) {
    auto *image = c.driver->item("develop.preview");
    return !c.window->property("libraryMode").toBool() && !c.editor.busy() && image->isVisible() &&
           image->property("status").toInt() == 1 &&
           image->property("source").toUrl().toString() == c.editor.previewUrl() &&
           c.provider->requestImage({}, nullptr, {}) == pixels;
}
QImage sceneRegion(Context &c, QQuickItem *item) {
    if (!item || !item->isVisible() || item->width() <= 0 || item->height() <= 0)
        throw std::runtime_error("Performance readback region is hidden or empty");
    // Capture geometry before readback can process scene events. The caller owns
    // the liveness guard; do not dereference the raw item after grabWindow.
    const auto box = item->mapRectToScene(item->boundingRect());
    const auto frame = c.window->grabWindow();
    require(!frame.isNull(), "Native performance scene readback failed");
    const double ratio = static_cast<double>(frame.width()) / c.window->width();
    const auto rectangle =
        QRectF(box.x() * ratio, box.y() * ratio, box.width() * ratio, box.height() * ratio)
            .toAlignedRect();
    require(frame.rect().contains(rectangle), "Performance readback region escaped the window");
    auto pixels = frame.copy(rectangle).convertToFormat(QImage::Format_RGBA8888);
    pixels.setDevicePixelRatio(1);
    pixels.setColorSpace({});
    return pixels;
}
bool sceneMatches(const QImage &actual, const QImage &expected) {
    if (actual.size() != expected.size() || actual.format() != QImage::Format_RGBA8888 ||
        expected.format() != QImage::Format_RGBA8888)
        return false;
    if (actual == expected)
        return true;
    // Metal's scaled scene readback can differ by one quantization level between
    // otherwise identical frames. Provider pixels and EditDocument remain exact.
    for (int y = 0; y < actual.height(); ++y) {
        const auto *left = actual.constScanLine(y);
        const auto *right = expected.constScanLine(y);
        for (qsizetype channel = 0; channel < static_cast<qsizetype>(actual.width()) * 4;
             ++channel) {
            if (std::abs(static_cast<int>(left[channel]) - right[channel]) > 1)
                return false;
        }
    }
    return true;
}
void waitPreview(Context &c, const QImage &expected) {
    c.driver->wait([&] { return previewReady(c, expected); },
                   "performance fixture final pixels ready outside measurement");
    c.driver->frame();
}
QQuickItem *thumbnail(QQuickItem *card) {
    for (auto *child : card->childItems()) {
        if (child->objectName() == "photoThumbnail")
            return child;
        if (auto *found = thumbnail(child))
            return found;
    }
    return nullptr;
}
bool thumbnailReady(QQuickItem *card, QQuickItem *image) {
    return card && image && card->isVisible() && image->isVisible() &&
           image->property("status").toInt() == 1 &&
           image->property("source").toUrl().toString() ==
               card->property("thumbnailURL").toString();
}
void foreground(Context &c) {
    c.window->raise();
    c.window->requestActivate();
    c.driver->wait([&] { return c.window->isActive() && c.window->isExposed(); },
                   "native performance fixture foreground", 5000);
    c.driver->frame();
}
QPoint prepareClick(Context &c, const QString &name) {
    foreground(c);
    auto *target = c.driver->item(name);
    const auto point = c.driver->point(name);
    require(target->isVisible() && target->isEnabled() &&
                c.window->contentItem()->contains(point) && c.window->isActive(),
            "Performance input target is unavailable: " + name);
    c.driver->log({{"action", "performance_pointer_input_prepared"}, {"id", name}});
    return point;
}
double measured(Context &c, const QString &operation, const std::function<void()> &input,
                const Predicate &stateReady, QQuickItem *region, const QImage &expectedScene) {
    const QPointer<QQuickItem> readbackRegion(region);
    c.driver->log({{"action", "performance_input_begin"}, {"operation", operation}});
    require(c.window->isActive() && c.window->isExposed(),
            "Performance input cannot start in a background or unexposed window");
    const auto before = c.metrics.polishes();
    QElapsedTimer timer;
    timer.start();
    input();
    try {
        c.driver->wait(
            [&] {
                return stateReady() && readbackRegion && c.metrics.polishes() > before &&
                       sceneMatches(sceneRegion(c, readbackRegion.data()), expectedScene);
            },
            "native input acknowledged by current QML state and rendered scene: " + operation);
    } catch (...) {
        c.driver->log({{"action", "performance_ack_failure"},
                       {"operation", operation},
                       {"state_ready", stateReady()},
                       {"active", c.window->isActive()},
                       {"exposed", c.window->isExposed()},
                       {"polishes_after_input", static_cast<qint64>(c.metrics.polishes() - before)},
                       {"preview_source", c.editor.previewUrl()}});
        expectedScene.save(c.root + "/expected-scene.png");
        if (readbackRegion && readbackRegion->isVisible())
            sceneRegion(c, readbackRegion.data()).save(c.root + "/observed-scene.png");
        throw;
    }
    const double elapsed = static_cast<double>(timer.nsecsElapsed()) / 1000000.;
    require(c.window->isActive() && c.window->isExposed(),
            "Performance window lost foreground during the measured input");
    // File I/O and metric aggregation occur after the measured acknowledgment.
    c.driver->log({{"action", "performance_visual_ack"},
                   {"operation", operation},
                   {"input_to_verified_scene_readback_ms", elapsed},
                   {"preview_source", c.editor.previewUrl()}});
    return elapsed;
}
double measuredClick(Context &c, const QString &name, const Predicate &stateReady,
                     QQuickItem *region, const QImage &expectedScene) {
    const auto point = prepareClick(c, name);
    return measured(
        c, name, [&] { QTest::mouseClick(c.window, Qt::LeftButton, Qt::NoModifier, point); },
        stateReady, region, expectedScene);
}
void navigationLatency(Context &c, const QString &id) {
    auto &d = *c.driver;
    const auto source = c.editor.sourcePath();
    const auto document = c.editor.currentSettings();
    const auto pixels = referenceFor(c, source, document);
    waitPreview(c, pixels);
    d.click("main.libraryButton");
    const QPointer<QQuickItem> card(d.item(c.card(0)));
    const QPointer<QQuickItem> image(thumbnail(card.data()));
    require(image, "Loaded-photo navigation fixture has no thumbnail image");
    d.wait([&] { return thumbnailReady(card, image); }, "warmed Library thumbnail ready");
    d.frame();
    const auto libraryScene = sceneRegion(c, image);
    require(libraryScene.pixelColor(libraryScene.width() / 2, libraryScene.height() / 2) !=
                libraryScene.pixelColor(libraryScene.width() / 4, libraryScene.height() / 4),
            "Library acknowledgment fixture has no distinct image content");
    d.click("main.developButton");
    waitPreview(c, pixels);
    auto *preview = d.item("develop.preview");
    const auto developScene = sceneRegion(c, preview);
    Distribution toLibrary, toDevelop;
    constexpr int cycles = 100;
    for (int cycle = 0; cycle < cycles; ++cycle) {
        toLibrary.add(measuredClick(
            c, "main.libraryButton",
            [&] {
                return c.window->property("libraryMode").toBool() &&
                       d.item("library.grid")->isVisible() && thumbnailReady(card, image);
            },
            image, libraryScene));
        toDevelop.add(measuredClick(
            c, "main.developButton", [&] { return previewReady(c, pixels); }, preview,
            developScene));
        require(c.editor.sourcePath() == source && c.editor.currentSettings() == document,
                "Measured Library/Develop navigation changed the active document");
    }
    auto result = metadata(id, "100 warmed Library/Develop round trips with one loaded photo");
    result["develop_to_library"] = summary(toLibrary);
    result["library_to_develop"] = summary(toDevelop);
    result["library_acknowledgment"] =
        "visible grid, warmed thumbnail Image.Ready/current model "
        "URL, thumbnail scene readback with the declared RGBA8 tolerance";
    writeJson(c.root + "/measurements.json", result);
}
void originalLatency(Context &c, const QString &id) {
    auto &d = *c.driver;
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
    c.saved();
    const auto document = c.editor.currentSettings();
    const auto processed = referenceFor(c, c.editor.sourcePath(), document);
    const auto original = referenceFor(c, c.editor.sourcePath(), {});
    require(original != processed, "Original timing fixture has no distinct processed effect");
    waitPreview(c, processed);
    auto *preview = d.item("develop.preview");
    const auto processedScene = sceneRegion(c, preview);
    d.click("develop.originalButton");
    waitPreview(c, original);
    const auto originalScene = sceneRegion(c, preview);
    require(!sceneMatches(originalScene, processedScene),
            "Original and Processed scene readbacks are identical");
    d.click("develop.processedButton");
    waitPreview(c, processed);
    Distribution toOriginal, toProcessed;
    constexpr int cycles = 100;
    for (int cycle = 0; cycle < cycles; ++cycle) {
        for (const bool showOriginal : {true, false}) {
            foreground(c);
            const auto previousSource = c.editor.previewUrl();
            const auto &pixels = showOriginal ? original : processed;
            const auto &scene = showOriginal ? originalScene : processedScene;
            const auto elapsed = measured(
                c,
                showOriginal ? "processed_to_original_shortcut" : "original_to_processed_shortcut",
                [&] { QTest::keyClick(c.window, Qt::Key_Backslash, Qt::NoModifier, 0); },
                [&] {
                    return c.editor.original() == showOriginal &&
                           c.editor.previewUrl() != previousSource && previewReady(c, pixels);
                },
                preview, scene);
            (showOriginal ? toOriginal : toProcessed).add(elapsed);
            require(c.editor.currentSettings() == document,
                    "Measured Original toggle mutated the edit document");
        }
    }
    auto result =
        metadata(id, "100 Original/Processed shortcut round trips on a loaded edited photo");
    result["processed_to_original"] = summary(toOriginal);
    result["original_to_processed"] = summary(toProcessed);
    result["input"] = "real Backslash key through the production Shortcut";
    writeJson(c.root + "/measurements.json", result);
}
void chooseTarget(Context &c, bool hsl) {
    if (hsl) {
        c.reveal("hsl.targetMode");
        c.driver->choose("hsl.targetMode", 1);
    }
    const auto selector = hsl ? QString("hsl.target") : QString("curves.target");
    const auto expected = hsl ? QString("hsl.saturation") : QString("curve.rgb");
    c.reveal(selector);
    if (c.editor.targetedTool() != expected)
        c.driver->click(selector);
    require(c.editor.targetedTool() == expected,
            "Latency fixture selected the wrong targeted tool");
}
void targetedLatency(Context &c, const QString &id) {
    auto &d = *c.driver;
    QJsonObject tools;
    for (const bool hsl : {false, true}) {
        chooseTarget(c, hsl);
        c.saved();
        const auto before = c.editor.currentSettings();
        const auto beforePixels = referenceFor(c, c.editor.sourcePath(), before);
        waitPreview(c, beforePixels);
        d.drag("develop.targetedArea", {.5, .5}, {.5, .35});
        const auto after = c.editor.currentSettings();
        auto expected = before;
        if (hsl)
            expected.hsl = after.hsl;
        else
            expected.curves[0] = after.curves[0];
        require(after != before && after == expected,
                "Targeted latency calibration changed an unrelated group or was a no-op");
        const auto afterPixels = referenceFor(c, c.editor.sourcePath(), after);
        require(afterPixels != beforePixels,
                "Targeted latency fixture has no distinct image effect");
        waitPreview(c, afterPixels);
        auto *preview = d.item("develop.preview");
        const auto afterScene = sceneRegion(c, preview);
        d.click("main.undoButton");
        require(c.editor.currentSettings() == before, "Targeted calibration Undo changed its base");
        c.saved();
        waitPreview(c, beforePixels);
        const auto beforeScene = sceneRegion(c, preview);
        require(!sceneMatches(beforeScene, afterScene),
                "Targeted calibration scene has no distinct visual effect");
        Distribution movement;
        constexpr int samples = 50;
        for (int sample = 0; sample < samples; ++sample) {
            foreground(c);
            require(c.editor.currentSettings() == before && previewReady(c, beforePixels),
                    "Targeted latency sample did not start from its calibrated base");
            d.press("develop.targetedArea", {.5, .5});
            require(d.object("develop.targetedArea")->property("adjusting").toBool(),
                    "Actual targeted pointer press was refused");
            require(c.editor.currentSettings() == before && previewReady(c, beforePixels),
                    "Targeted press changed the calibrated base before the measured move");
            const auto endpoint = d.point("develop.targetedArea", {.5, .35});
            const auto previousSource = c.editor.previewUrl();
            const auto elapsed = measured(
                c, hsl ? "held_pointer_hsl_move" : "held_pointer_curve_move",
                [&] { QTest::mouseMove(c.window, endpoint, 0); },
                [&] {
                    return c.editor.currentSettings() == after &&
                           c.editor.previewUrl() != previousSource && previewReady(c, afterPixels);
                },
                preview, afterScene);
            movement.add(elapsed);
            d.release("develop.targetedArea", {.5, .35});
            require(!d.object("develop.targetedArea")->property("adjusting").toBool(),
                    "Targeted performance pointer release left a live gesture");
            d.click("main.undoButton");
            require(c.editor.currentSettings() == before,
                    "Measured targeted drag was not one Undo");
            c.saved();
            waitPreview(c, beforePixels);
            d.wait([&] { return sceneMatches(sceneRegion(c, preview), beforeScene); },
                   "targeted base scene restored outside measurement");
        }
        tools[hsl ? "hsl_saturation" : "curve_rgb"] = summary(movement);
    }
    auto result = metadata(id, "50 single pointer-move acknowledgments per targeted tool");
    result["input"] = "one real move inside an accepted held-pointer gesture; press, release, "
                      "Undo, save and fixture calibration are outside each timing interval";
    result["continuous_drag_throughput_measured"] = false;
    result["tools"] = tools;
    writeJson(c.root + "/measurements.json", result);
}
} // namespace

bool currentPerformanceScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-NAV-002", "UX-PRV-002", "UX-ADV-012"};
    if (!supported.contains(id))
        return false;
    require(QGuiApplication::platformName() == "cocoa" &&
                qEnvironmentVariable("GITHUB_ACTIONS") != "true",
            "PERF_LOCAL measurements require native Cocoa on an idle physical Mac");
    c.openFirst();
    c.saved();
    foreground(c);
    if (id == "UX-NAV-002")
        navigationLatency(c, id);
    else if (id == "UX-PRV-002")
        originalLatency(c, id);
    else
        targetedLatency(c, id);
    return true;
}
} // namespace ux
