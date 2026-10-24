#include "apps/shared/PhotoDecoder.h"
#include "core/render/AdvancedColor.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/DisplayProfile.h"
#include "tests/ux/LoadProbe.h"
#include "tests/ux/MemoryPressureProbe.h"
#include "tests/ux/PreviewResultProbe.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QQuickItemGrabResult>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <optional>
#include <thread>

namespace ux {
namespace {
void observeZoomAvailability(Context &c, QObject &owner) {
    const auto record = [&c](const QString &reason) {
        c.driver->log({{"action", "zoom_availability_observation"},
                       {"reason", reason},
                       {"pressure", int(MemoryPressureProbe::current(c.editor))},
                       {"available", c.editor.zoomAvailable()},
                       {"has_photo", c.editor.hasPhoto()},
                       {"busy", c.editor.busy()},
                       {"preview_url", c.editor.previewUrl()}});
    };
    record("initial");
    QObject::connect(&c.editor, &EditorController::stateChanged, &owner,
                     [&c, record, pressure = MemoryPressureProbe::current(c.editor),
                      available = c.editor.zoomAvailable()]() mutable {
                         const auto nextPressure = MemoryPressureProbe::current(c.editor);
                         const auto nextAvailable = c.editor.zoomAvailable();
                         if (nextPressure != pressure || nextAvailable != available) {
                             record("state_changed");
                             pressure = nextPressure;
                             available = nextAvailable;
                         }
                     });
}
void awaitZoomControls(Context &c) {
    // Real OS pressure can arrive during the independent large-image oracle.
    // A disabled zoom control is the intentional memory fallback. Wait for the
    // actual normal/recovered state; never inject pressure or suppress callbacks.
    QElapsedTimer elapsed;
    elapsed.start();
    c.driver->wait(
        [&] {
            return c.editor.zoomAvailable() &&
                   c.driver->item("develop.actualPixels")->isEnabled() &&
                   c.driver->item("develop.zoomPercent")->isEnabled();
        },
        "Zoom controls recovered from actual OS memory pressure");
    c.driver->log({{"action", "zoom_controls_available"},
                   {"wait_ms", elapsed.elapsed()},
                   {"pressure", int(MemoryPressureProbe::current(c.editor))}});
}
QByteArray sourceHash(const QString &path) {
    QFile source(path);
    require(source.open(QIODevice::ReadOnly), "Cannot read own zoom fixture");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&source), "Cannot hash complete zoom fixture");
    return hash.result().toHex();
}
QString chart(Context &c, bool pair, QSize size = {3072, 2048}) {
    const auto directory = c.root + "/zoom-originals";
    require(QDir().mkpath(directory), "Cannot create zoom fixture directory");
    QImage image(size, QImage::Format_RGBA8888);
    require(!image.isNull(), "Cannot allocate bounded zoom chart");
    // Single-pixel detail in both axes, plus non-periodic coarse coordinates.
    // A 2560-wide preview stretched to 3072 cannot satisfy the scene oracle.
    for (int y = 0; y < image.height(); ++y) {
        auto *row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            row[x * 4] = static_cast<uchar>(32 + (x % 2) * 144 + (y / 17) % 48);
            row[x * 4 + 1] = static_cast<uchar>(32 + (y % 2) * 144 + (x / 19) % 48);
            row[x * 4 + 2] = static_cast<uchar>(32 + (x / 11 + y / 13) % 192);
            row[x * 4 + 3] = 255;
        }
    }
    const auto path = directory + "/Zoom.png";
    require(image.save(path), "Cannot save zoom chart");
    if (pair) {
        image.invertPixels(QImage::InvertRgb);
        require(image.save(directory + "/Zoom2.png"), "Cannot save second zoom chart");
    }
    return path;
}
QImage reference(Context &c, const luma::DecodedImage &decoded, const luma::EditDocument &document,
                 int maximumDimension, bool displayProfile = true) {
    const auto profile = displayProfile && QGuiApplication::platformName() == "cocoa"
                             ? luma::macos::displayProfile(c.window->winId())
                             : std::vector<std::uint8_t>{};
    // A full CPU oracle can exceed the GUI watchdog budget on hosted runners.
    // Only immutable test data enters this worker; the real GUI timer and the
    // original application callbacks keep running while we await its result.
    const auto guiThread = std::this_thread::get_id();
    luma::CancellationSource cancellation;
    QElapsedTimer elapsed;
    elapsed.start();
    c.driver->log(
        {{"action", "zoom_cpu_reference_started"}, {"maximum_dimension", maximumDimension}});
    auto work = std::async(std::launch::async, [&, document, profile, guiThread,
                                                stop = cancellation.token()] {
        require(std::this_thread::get_id() != guiThread,
                "Independent zoom CPU reference must not run on the GUI thread");
        const auto reduced =
            maximumDimension == 0
                ? std::nullopt
                : std::optional(luma::standardPreview(decoded.image, maximumDimension, stop));
        const auto &input = reduced ? *reduced : decoded.image;
        const double scale = std::min(double(input.width) / decoded.image.width,
                                      double(input.height) / decoded.image.height);
        const auto result = luma::renderCPU(input, document, decoded.metadata.orientation, profile,
                                            stop, decoded.calibration, luma::DetailDiagnostic::none,
                                            scale, decoded.metadata);
        return QImage(result.rgba.data(), result.width, result.height, result.width * 4,
                      QImage::Format_RGBA8888)
            .copy();
    });
    try {
        c.driver->wait(
            [&] { return work.wait_for(std::chrono::seconds(0)) == std::future_status::ready; },
            "Independent zoom CPU reference worker completed");
    } catch (...) {
        cancellation.cancel();
        work.wait(); // Join before the borrowed immutable decoded fixture leaves scope.
        throw;
    }
    const auto result = work.get();
    c.driver->log({{"action", "zoom_cpu_reference_completed"},
                   {"maximum_dimension", maximumDimension},
                   {"width", result.width()},
                   {"height", result.height()},
                   {"elapsed_ms", elapsed.elapsed()},
                   {"worker_thread", true}});
    return result;
}
bool agrees(const QImage &a, const QImage &b, int rgbBudget);
void ready(Context &c, const QImage &pixels, const QString &previous = {}, int rgbBudget = 0) {
    try {
        c.driver->wait(
            [&] {
                const auto *image = c.driver->item("develop.preview");
                return !c.editor.busy() && c.editor.error().isEmpty() &&
                       c.editor.previewUrl() != previous && image->isVisible() &&
                       image->property("status").toInt() == 1 &&
                       image->property("source").toUrl().toString() == c.editor.previewUrl() &&
                       agrees(c.provider->requestImage({}, nullptr, {}), pixels, rgbBudget);
            },
            "Zoom Ready image agrees with independent full/reduced CPU reference");
    } catch (...) {
        recordPreviewDifference(
            c, pixels, QString("Zoom CPU reference, RGB8 limit %1, exact alpha").arg(rgbBudget));
        throw;
    }
    c.driver->frame();
}
bool agrees(const QImage &a, const QImage &b, int rgbBudget) {
    if (a.isNull() || b.isNull() || a.size() != b.size())
        return false;
    const auto first = a.convertToFormat(QImage::Format_RGBA8888);
    const auto second = b.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width() * 4; ++x)
            if (std::abs(int(first.constScanLine(y)[x]) - int(second.constScanLine(y)[x])) >
                (x % 4 == 3 ? 0 : rgbBudget))
                return false;
    return true;
}
void mapping(Context &c, const QImage &expected) {
    auto &d = *c.driver;
    auto *image = d.item("develop.preview"), *view = d.item("develop.viewport");
    const auto scale = c.window->devicePixelRatio();
    const auto origin = image->mapToScene({}) * scale;
    const auto center = image->mapFromItem(view, {view->width() / 2, view->height() / 2}) * scale;
    d.log({{"action", "zoom_pixel_geometry"},
           {"device_pixel_ratio", scale},
           {"image_width", image->width()},
           {"image_height", image->height()},
           {"origin_x_px", origin.x()},
           {"origin_y_px", origin.y()},
           {"source_center_x", center.x()},
           {"source_center_y", center.y()},
           {"content_x", view->property("contentX").toDouble()},
           {"content_y", view->property("contentY").toDouble()}});
    require(c.editor.oneToOne() && view->property("pixelMode").toBool() &&
                std::abs(image->width() * scale - expected.width()) < 1e-6 &&
                std::abs(image->height() * scale - expected.height()) < 1e-6 &&
                std::abs(image->property("paintedWidth").toDouble() - image->width()) < 1e-6 &&
                std::abs(image->property("paintedHeight").toDouble() - image->height()) < 1e-6,
            "100% does not map one output pixel to one scene backing pixel");
    require(std::abs(origin.x() - std::round(origin.x())) < 1e-6 &&
                std::abs(origin.y() - std::round(origin.y())) < 1e-6,
            "100% image is between backing pixels and resamples single-pixel detail");
    require(std::abs(center.x() - expected.width() / 2.) <= .5 &&
                std::abs(center.y() - expected.height() / 2.) <= .5,
            "Fit to 100% lost the image center");
    const auto actual = c.window->grabWindow().convertToFormat(QImage::Format_RGBA8888);
    require(!actual.isNull() && actual.width() == qRound(c.window->width() * scale) &&
                actual.height() == qRound(c.window->height() * scale),
            "Zoom scene readback dimensions disagree with the real window DPR");
    const auto sceneCenter = view->mapToScene({view->width() / 2, view->height() / 2}) * scale;
    const QRect area(qRound(sceneCenter.x()) - 128, qRound(sceneCenter.y()) - 64, 256, 128);
    const QRect sourceArea(area.topLeft() - origin.toPoint(), area.size());
    require(actual.rect().contains(area) && expected.rect().contains(sourceArea),
            "Zoom sample is outside the scene/source");
    const auto sample = actual.copy(area), target = expected.copy(sourceArea);
    require(sample.save(c.root + "/zoom-scene-sample.png") &&
                target.save(c.root + "/zoom-reference-sample.png"),
            "Cannot retain zoom samples");
    const int budget = QGuiApplication::platformName() == "cocoa" ? 1 : 0;
    auto changed = target.copy();
    changed.scanLine(0)[0] ^= 128;
    require(!agrees(changed, target, budget) &&
                !agrees(expected.copy(sourceArea.translated(1, 0)), target, budget),
            "Zoom scene oracle accepted changed or shifted single-pixel detail");
    require(agrees(sample, target, budget),
            "Actual 100% scene does not preserve full-resolution pixel detail");
    d.log({{"action", "zoom_scene_pixels"},
           {"sample_width", area.width()},
           {"sample_height", area.height()},
           {"rgb_budget", budget},
           {"alpha_budget", 0},
           {"scope", "actual Qt scene readback; hardware scanout not measured"}});
}
struct ChartSource {
    QString path;
    QByteArray hash, secondHash;
};
ChartSource openChart(Context &c, bool pair = false, QSize size = {3072, 2048},
                      bool cpuOnly = true) {
    auto &d = *c.driver;
    require(c.editor.metalPreview(), "Fresh chart fixture must use the default Metal preference");
    if (cpuOnly) {
        // This contract isolates scaling and full-resolution decode from GPU arithmetic.
        d.click("settingsButton");
        d.wait([&] { return d.object("preferencesPanel")->property("opened").toBool(); },
               "Zoom CPU preference opened");
        d.click("settings.metalPreview");
        d.key(Qt::Key_Escape);
        d.wait([&] { return !d.object("preferencesPanel")->property("visible").toBool(); },
               "Zoom preferences closed");
        require(!c.editor.metalPreview(), "Zoom CPU preference was not applied");
    }
    const auto path = chart(c, pair, size);
    const auto hash = sourceHash(path);
    const auto secondHash =
        pair ? sourceHash(QFileInfo(path).absolutePath() + "/Zoom2.png") : QByteArray{};
    d.log({{"action", "zoom_own_original_created"},
           {"source", path},
           {"sha256", QString::fromLatin1(hash)},
           {"second_sha256", QString::fromLatin1(secondHash)}});
    const auto originalFixture = c.fixture;
    c.fixture = QFileInfo(path).absolutePath();
    c.import(pair ? 2 : 1);
    c.fixture = originalFixture;
    d.doubleClick(c.card(0));
    c.loaded();
    require(c.editor.sourcePath() == path, "Zoom import did not open the first owned chart");
    return {path, hash, secondHash};
}
void fitGeometry(Context &c) {
    const auto *view = c.driver->item("develop.viewport");
    require(!c.editor.oneToOne() && !view->property("pixelMode").toBool() &&
                view->property("contentX").toDouble() == 0 &&
                view->property("contentY").toDouble() == 0,
            "Photo switch did not reset zoom and both pan offsets to Fit");
}
luma::AssetSnapshot savedSnapshot(Context &c) {
    const auto result =
        luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(c.editor.photoID().toStdString());
    require(result.has_value(), "Current chart has no catalog snapshot");
    return *result;
}
void sameSaved(Context &c, const luma::AssetSnapshot &before) {
    const auto after = luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(before.asset.id);
    require(after && after->revision == before.revision &&
                after->asset.edits == before.asset.edits &&
                after->asset.path == before.asset.path &&
                after->asset.checksum == before.asset.checksum,
            "Preview delivery/zoom changed saved document, revision or original identity");
}
void filmstrip(Context &c, int row, const QString &path, const luma::EditDocument &document,
               const QImage &expected) {
    QSignalSpy opens(&c.library, &LibraryModel::openRequested);
    QSignalSpy loads(&c.editor, &EditorController::photoLoaded);
    require(opens.isValid() && loads.isValid(), "Cannot observe photo switch");
    c.driver->click(c.card(row, true));
    c.driver->wait([&] { return loads.count() == 1 && !c.editor.busy(); },
                   "Actual filmstrip photo switch");
    ready(c, expected);
    require(opens.count() == 1 && opens.at(0).at(0).toUrl().toLocalFile() == path &&
                loads.at(0).at(0).toString() == c.editor.photoID() &&
                c.editor.sourcePath() == path && c.editor.currentSettings() == document &&
                !c.editor.canUndo() && !c.editor.canRedo(),
            "Filmstrip switch lost asset identity/document or reset-history policy");
    fitGeometry(c);
}

PreviewResultSnapshot heldFinal(Context &c, PreviewResultGate &gate, const QImage &expected,
                                const QString &phase) {
    c.driver->wait(
        [&] {
            const auto state = gate.snapshot();
            return state.held && state.held->job->state.load() == luma::JobState::completed;
        },
        "Actual transient final result held: " + phase);
    const auto state = gate.snapshot();
    require(state.held && state.held->controller == &c.editor && state.held->resolution == 2560 &&
                state.held->assetID == c.editor.photoID().toStdString() && !state.hookFailed &&
                !state.released && state.deliveries == 0 && expected.width() == 2560 &&
                state.pixels == expected,
            "Held transient result differs from the exact independent CPU reference");
    c.driver->log({{"action", "progressive_transient_final_held"},
                   {"phase", phase},
                   {"exact_cpu_pixels", state.pixels == expected},
                   {"original", c.editor.original()},
                   {"preset_preview", c.editor.presetPreview()}});
    return state;
}
void rejectTransient(Context &c, PreviewResultGate &gate, const QImage &expected,
                     const QString &phase) {
    const auto url = c.editor.previewUrl();
    const auto histogram = c.editor.histogram();
    const auto document = c.editor.currentSettings();
    const auto photo = c.editor.photoID();
    const auto selection = c.library.selectionIDs();
    const bool original = c.editor.original(), hover = c.editor.presetPreview();
    const bool undo = c.editor.canUndo(), redo = c.editor.canRedo();
    require(!c.editor.histogramPending() && !c.editor.busy() && !c.editor.saving(),
            "Replacement preview is not settled before delayed transient delivery");
    QSignalSpy changes(&c.editor, &EditorController::stateChanged);
    QSignalSpy histograms(&c.editor, &EditorController::histogramChanged);
    require(changes.isValid() && histograms.isValid(), "Cannot observe transient side effects");
    gate.release();
    c.driver->wait([&] { return gate.snapshot().deliveries == 1; },
                   "Actual transient success callback delivered: " + phase);
    c.driver->frame();
    const auto result = gate.snapshot();
    require(result.held && result.delivered && result.held->sameAs(*result.delivered) &&
                result.released && !result.hookFailed && !result.deliveryBeforeRelease &&
                result.deliveries == 1 && result.rejections == 1 &&
                result.rejectionRevision > result.held->revision &&
                result.held->job->cancellation.token().isCancelled(),
            "Existing revision guard failed to reject the exact obsolete transient result");
    require(c.editor.previewUrl() == url && c.provider->requestImage({}, nullptr, {}) == expected &&
                c.editor.histogram() == histogram && !c.editor.histogramPending() &&
                c.editor.currentSettings() == document && c.editor.photoID() == photo &&
                c.library.selectionIDs() == selection && c.editor.original() == original &&
                c.editor.presetPreview() == hover && c.editor.canUndo() == undo &&
                c.editor.canRedo() == redo && changes.isEmpty() && histograms.isEmpty() &&
                !c.editor.busy() && !c.editor.saving() && c.editor.error().isEmpty(),
            "Delayed transient result changed the replacement image or durable/UI state");
    c.driver->log(
        {{"action", "progressive_transient_rejected"},
         {"phase", phase},
         {"exact_job_identity",
          result.held && result.delivered && result.held->sameAs(*result.delivered)},
         {"deliveries", int(result.deliveries)},
         {"guard_rejections", int(result.rejections)},
         {"held_revision", QString::number(result.held.value().revision)},
         {"current_revision", QString::number(result.rejectionRevision)},
         {"current_pixels_exact", c.provider->requestImage({}, nullptr, {}) == expected}});
}
void transientHistory(Context &c, const luma::EditDocument &document, const QImage &neutral,
                      const QImage &edited) {
    c.driver->click("main.undoButton");
    ready(c, neutral);
    require(c.editor.currentSettings() == luma::EditDocument{} && !c.editor.canUndo() &&
                c.editor.canRedo(),
            "Transient preview polluted the single-gesture Undo history");
    c.driver->click("main.redoButton");
    ready(c, edited);
    c.saved();
    require(c.editor.currentSettings() == document && c.editor.canUndo() && !c.editor.canRedo(),
            "Transient preview polluted Redo");
}

} // namespace
bool previewCacheBudgetScenario(Context &c, const QString &id) {
    if (id != "UX-P06-LRU-001")
        return false;
    auto &d = *c.driver;
    MemoryPressureProbe::useControlledNotifications(c.editor);
    const auto source = openChart(c, false, {800, 600});
    const auto decoded = luma::PhotoDecoder().decode(source.path.toStdString());
    require(c.editor.previewCacheMiB() == 256, "Cache fixture needs the default 256 MiB budget");
    luma::EditDocument penultimate;
    for (int step = 1; step <= 9; ++step) {
        penultimate = c.editor.currentSettings();
        c.reveal("develop.exposureSlider");
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {step / 10., .5});
        c.saved();
        d.wait([&] { return !c.editor.busy() && !c.editor.histogramPending(); },
               "Each distinct cache fixture render completed");
        require(c.editor.currentSettings() != penultimate,
                "Cache fixture repeated an unchanged edit");
    }
    const auto document = c.editor.currentSettings();
    const auto before = savedSnapshot(c);
    const auto selected = c.library.selectionIDs();
    const auto pixels = reference(c, decoded, document, 2560);
    ready(c, pixels);
    const auto entryBytes =
        static_cast<std::size_t>(pixels.sizeInBytes()) + sizeof(RenderedPreview);
    const auto quickPixels = reference(c, decoded, document, 512);
    const auto quickEntryBytes =
        static_cast<std::size_t>(quickPixels.sizeInBytes()) + sizeof(RenderedPreview);
    require(quickEntryBytes < entryBytes, "Cache fixture needs distinct transient/final sizes");
    // This 800x600 fixture has exactly two possible image sizes: 512x384
    // transient feedback and 800x600 interaction/final frames. Infer their
    // counts from independently computed costs, including RenderedPreview.
    const auto entryCounts = [&](const luma::CacheStatistics &cache) {
        require(cache.entries > 0 && cache.usedBytes <= cache.entries * entryBytes &&
                    cache.usedBytes >= cache.entries * quickEntryBytes,
                "Rendered cache byte count is outside the actual fixture entry costs");
        const auto savedBytes = cache.entries * entryBytes - cache.usedBytes;
        require(savedBytes % (entryBytes - quickEntryBytes) == 0,
                "Rendered cache bytes do not match whole transient/final entries");
        const auto quick = savedBytes / (entryBytes - quickEntryBytes);
        return std::pair{quick, cache.entries - quick};
    };
    const auto warm = c.editor.cacheStatistics();
    const auto [warmQuick, warmFinal] = entryCounts(warm[2]);
    constexpr std::size_t mib = 1024 * 1024;
    require(warm[2].usedBytes > 16 * mib && warm[2].budgetBytes == 64 * mib && warmFinal >= 9,
            "Cache fixture did not retain nine complete renders and exceed the new 16 MiB budget");
    QSignalSpy saves(&c.editor, &EditorController::editsSaved);
    require(saves.isValid(), "Cannot observe unexpected saves while changing the cache budget");
    const auto url = c.editor.previewUrl();
    d.click("settingsButton");
    d.wait([&] { return d.object("preferencesPanel")->property("opened").toBool(); },
           "Cache preferences opened");
    d.type("settings.previewCache", "64");
    d.key(Qt::Key_Return);
    d.wait([&] { return c.editor.previewCacheMiB() == 64; }, "Actual Settings committed 64 MiB");
    const auto reduced = c.editor.cacheStatistics();
    const auto [reducedQuick, reducedFinal] = entryCounts(reduced[2]);
    require(reduced[0].budgetBytes == 16 * mib && reduced[1].budgetBytes == 32 * mib &&
                reduced[2].budgetBytes == 16 * mib && reduced[2].entries < warm[2].entries &&
                reduced[2].usedBytes < warm[2].usedBytes && reducedFinal > 0,
            "Configured cache split or rendered-entry eviction differs from its byte budget");
    for (const auto &cache : reduced)
        require(cache.usedBytes <= cache.budgetBytes,
                "A production preview cache retained more than its configured budget");
    require(c.editor.previewUrl() == url && c.provider->requestImage({}, nullptr, {}) == pixels,
            "Lowering the budget replaced or invalidated the active image");
    d.click("settings.clearPreviewCache");
    for (const auto &cache : c.editor.cacheStatistics())
        require(cache.entries == 0 && cache.usedBytes == 0,
                "Clear preview cache left optional cached data behind");
    require(c.editor.previewUrl() == url && c.provider->requestImage({}, nullptr, {}) == pixels,
            "Explicit cache clear invalidated the active image");
    d.key(Qt::Key_Escape);
    d.wait([&] { return !d.object("preferencesPanel")->property("visible").toBool(); },
           "Cache preferences dismissed");
    sameSaved(c, before);
    require(c.editor.currentSettings() == document && c.library.selectionIDs() == selected &&
                saves.isEmpty() && c.editor.canUndo() && !c.editor.canRedo(),
            "Cache preferences changed document, selection or edit history");
    d.click("main.undoButton");
    ready(c, reference(c, decoded, penultimate, 2560));
    c.saved();
    require(c.editor.currentSettings() == penultimate && c.editor.canRedo(),
            "Undo after eviction did not reconstruct the preceding document");
    d.click("main.redoButton");
    ready(c, pixels);
    c.saved();
    require(c.editor.currentSettings() == document && !c.editor.canRedo() &&
                c.library.selectionIDs() == selected && sourceHash(source.path) == source.hash,
            "Cache regeneration lost edits, selection or original integrity");
    const auto durable = savedSnapshot(c);
    require(durable.asset.id == before.asset.id && durable.asset.path == before.asset.path &&
                durable.asset.checksum == before.asset.checksum &&
                durable.asset.edits == document && durable.revision == before.revision + 2,
            "Cache regeneration did not persist exactly the requested Undo and Redo");
    for (const auto &cache : c.editor.cacheStatistics())
        require(cache.usedBytes <= cache.budgetBytes,
                "Regenerated previews exceeded their retained cache budget");
    d.log(
        {{"action", "preview_cache_budget_eviction"},
         {"warm_rendered_bytes", qint64(warm[2].usedBytes)},
         {"warm_rendered_entries", qint64(warm[2].entries)},
         {"reduced_rendered_bytes", qint64(reduced[2].usedBytes)},
         {"reduced_rendered_entries", qint64(reduced[2].entries)},
         {"entry_bytes", qint64(entryBytes)},
         {"quick_entry_bytes", qint64(quickEntryBytes)},
         {"warm_quick_entries", qint64(warmQuick)},
         {"warm_final_entries", qint64(warmFinal)},
         {"reduced_quick_entries", qint64(reducedQuick)},
         {"reduced_final_entries", qint64(reducedFinal)},
         {"configured_mib", c.editor.previewCacheMiB()},
         {"scope", "actual cache-owned references and UI controls; no RSS/stress qualification"}});
    require(c.warnings.isEmpty(), "Cache budget workflow produced QML warnings");
    return true;
}
bool memoryPressureScenario(Context &c, const QString &id) {
    if (id != "UX-P06-MEM-001" && id != "UX-P06-MEM-002")
        return false;
    auto &d = *c.driver;
    MemoryPressureProbe::useControlledNotifications(c.editor);
    d.log({{"action", "memory_pressure_controlled_notifications"},
           {"os_monitor_stopped", true},
           {"scope", "owned UX controller only; no OS pressure or RSS qualification"}});
    const auto source = openChart(c);
    const auto decoded = luma::PhotoDecoder().decode(source.path.toStdString());
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.6, .5});
    c.saved();
    const auto document = c.editor.currentSettings();
    const auto before = savedSnapshot(c);
    const auto selection = c.library.selectionIDs();
    const auto fit = reference(c, decoded, document, 2560);
    const auto full = reference(c, decoded, document, 0);
    ready(c, fit);
    d.log({{"action", "memory_pressure_initial_state"},
           {"pressure", int(MemoryPressureProbe::current(c.editor))},
           {"controller_zoom_available", c.editor.zoomAvailable()},
           {"actual_pixels_enabled", d.item("develop.actualPixels")->isEnabled()},
           {"has_photo", c.editor.hasPhoto()},
           {"busy", c.editor.busy()},
           {"zoom_mode", c.editor.zoomMode()}});
    d.click("develop.actualPixels");
    ready(c, full);
    d.wait([&] { return !c.editor.histogramPending(); }, "Full preview settled before pressure");
    const auto warm = c.editor.cacheStatistics();
    require(warm[0].entries > 0 && warm[1].entries > 0 && warm[2].entries > 0,
            "Pressure fixture must populate decoded, source and rendered caches");
    require(c.editor.oneToOne() && c.editor.canUndo() && !c.editor.canRedo(),
            "Pressure fixture needs full resolution and one committed edit");
    QSignalSpy saves(&c.editor, &EditorController::editsSaved);
    const auto pressure = id == "UX-P06-MEM-001" ? luma::macos::MemoryPressure::warning
                                                 : luma::macos::MemoryPressure::critical;
    MemoryPressureProbe::deliver(c.editor, pressure);
    ready(c, fit);
    d.wait([&] { return !c.editor.histogramPending(); }, "Pressure Fit preview settled");
    fitGeometry(c);
    require(!c.editor.zoomAvailable() && !d.item("develop.actualPixels")->isEnabled() &&
                !d.item("develop.fill")->isEnabled() && !d.item("develop.zoomPercent")->isEnabled(),
            "Pressure did not disable full-resolution UI controls");
    QJsonArray caches;
    const auto reclaimed = c.editor.cacheStatistics();
    for (std::size_t i = 0; i < reclaimed.size(); ++i) {
        require(reclaimed[i].entries == 0 && reclaimed[i].usedBytes == 0 &&
                    reclaimed[i].budgetBytes == 0,
                "Pressure retained reclaimable cache references or a nonzero budget");
        caches.append(QJsonObject{{"warm_bytes", qint64(warm[i].usedBytes)},
                                  {"warm_entries", qint64(warm[i].entries)},
                                  {"reclaimed_bytes", qint64(reclaimed[i].usedBytes)},
                                  {"reclaimed_entries", qint64(reclaimed[i].entries)}});
    }
    sameSaved(c, before);
    require(c.editor.currentSettings() == document &&
                c.editor.photoID().toStdString() == before.asset.id &&
                c.library.selectionIDs() == selection && c.editor.canUndo() &&
                !c.editor.canRedo() && saves.isEmpty() && sourceHash(source.path) == source.hash,
            "Memory warning damaged the document, history, catalog or original");
    const auto recoveredUrl = c.editor.previewUrl();
    QSignalSpy changes(&c.editor, &EditorController::stateChanged);
    MemoryPressureProbe::deliver(c.editor, luma::macos::MemoryPressure::normal);
    d.frame();
    const auto restored = c.editor.cacheStatistics();
    for (std::size_t i = 0; i < restored.size(); ++i)
        require(restored[i].budgetBytes == warm[i].budgetBytes,
                "Normal pressure did not restore the configured cache budget");
    d.log({{"action", "memory_pressure_recovery"},
           {"pressure", id == "UX-P06-MEM-001" ? "warning" : "critical"},
           {"caches", caches},
           {"state_notifications", changes.count()},
           {"controller_zoom_available", c.editor.zoomAvailable()},
           {"actual_pixels_enabled", d.item("develop.actualPixels")->isEnabled()},
           {"scope", "injected GUI handler boundary; no OS/RSS qualification"}});
    require(c.editor.zoomAvailable() && !changes.isEmpty() &&
                d.item("develop.actualPixels")->isEnabled() &&
                d.item("develop.fill")->isEnabled() && d.item("develop.zoomPercent")->isEnabled(),
            "Normal memory recovery left zoom UI disabled after the controller recovered");
    require(c.editor.previewUrl() == recoveredUrl &&
                c.provider->requestImage({}, nullptr, {}) == fit,
            "Normal pressure unnecessarily replaced the valid Fit preview");
    d.click("develop.actualPixels");
    ready(c, full);
    require(c.editor.oneToOne(), "Recovered 100% control did not restore full-resolution view");
    sameSaved(c, before);
    require(c.editor.currentSettings() == document && c.library.selectionIDs() == selection &&
                c.editor.canUndo() && !c.editor.canRedo() && saves.isEmpty() &&
                sourceHash(source.path) == source.hash && c.warnings.isEmpty(),
            "Pressure recovery changed edits, history, original or QML validity");
    return true;
}
bool zoomScenario(Context &c, const QString &id) {
    if (id != "UX-P06-ZOM-001" && id != "UX-P06-ZOM-002")
        return false;
    auto &d = *c.driver;
    QObject availabilityObserver;
    observeZoomAvailability(c, availabilityObserver);
    const auto source = openChart(c);
    const auto &path = source.path;
    const auto hash = QString::fromLatin1(source.hash);

    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.6, .5});
    c.saved();
    const auto document = c.editor.currentSettings();
    require(c.editor.canUndo() && !c.editor.canRedo() && document.exposure != 0,
            "Zoom fixture needs one committed UI edit");
    // Release the oracle's duplicate full FloatImage before exercising zoom.
    // Both independent pixel references remain immutable for every assertion.
    const auto [fit, full] = [&] {
        const auto decoded = luma::PhotoDecoder().decode(path.toStdString());
        require(decoded.image.width == 3072 && decoded.image.height == 2048,
                "Zoom fixture was not decoded at full resolution");
        return std::pair{reference(c, decoded, document, 2560), reference(c, decoded, document, 0)};
    }();
    require(fit.width() == 2560 && full.size() == QSize(3072, 2048),
            "Zoom fixture must distinguish reduced and full-resolution rendering");
    ready(c, fit);
    const auto photo = c.editor.photoID();
    const auto before =
        luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(photo.toStdString());
    require(before.has_value(), "Zoom fixture has no saved document");
    const auto selected = c.library.selectionIDs();
    QSignalSpy saves(&c.editor, &EditorController::editsSaved);
    require(saves.isValid(), "Cannot observe unintended zoom persistence");
    for (int cycle = 0; cycle < 2; ++cycle) {
        if (cycle == 1)
            d.resize({c.window->width() + 1, c.window->height() + 1});
        awaitZoomControls(c);
        auto previous = c.editor.previewUrl();
        d.click("develop.actualPixels");
        ready(c, full, previous);
        mapping(c, full);
        previous = c.editor.previewUrl();
        d.click("develop.fit");
        ready(c, fit, previous);
        const auto *view = d.item("develop.viewport");
        const auto *image = d.item("develop.preview");
        const auto center = image->mapToItem(view, {image->width() / 2, image->height() / 2});
        require(!c.editor.oneToOne() && !view->property("pixelMode").toBool() &&
                    view->property("contentX").toDouble() == 0 &&
                    view->property("contentY").toDouble() == 0 &&
                    std::abs(center.x() - view->width() / 2) < 1e-6 &&
                    std::abs(center.y() - view->height() / 2) < 1e-6,
                "Returning to Fit lost the center or retained a pan offset");
    }
    const auto after =
        luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(photo.toStdString());
    require(after && after->revision == before.value().revision && after->asset.edits == document &&
                after->asset.checksum == before.value().asset.checksum &&
                after->asset.path == before.value().asset.path &&
                c.editor.currentSettings() == document && c.editor.photoID() == photo &&
                c.library.count() == 1 && c.library.selectionIDs() == selected && saves.isEmpty() &&
                !c.editor.saving() && c.editor.canUndo() && !c.editor.canRedo(),
            "Zoom modified document, saved revision, identity, selection or history");
    require(QString::fromLatin1(sourceHash(path)) == hash, "Zoom changed original bytes");
    d.log({{"action", "zoom_own_original_preserved"},
           {"source", path},
           {"sha256", hash},
           {"width", 3072},
           {"height", 2048}});
    require(c.warnings.isEmpty(), "Zoom produced QML warnings: " + c.warnings.join('\n'));
    return true;
}
bool progressiveFeedbackScenario(Context &c, const QString &id) {
    if (id != "UX-P06-PRG-001")
        return false;
    require(QGuiApplication::platformName() == "cocoa" &&
                qEnvironmentVariable("GITHUB_ACTIONS") != "true",
            "Progressive feedback latency requires the physical Mac's native Cocoa renderer");
    auto &d = *c.driver;
    const auto source = openChart(c, false, {3072, 2048}, false);
    const auto decoded = luma::PhotoDecoder().decode(source.path.toStdString());
    const auto initial = c.editor.currentSettings();
    ready(c, reference(c, decoded, initial, 2560), {}, 1);
    const QString slider = "develop.presence.claritySlider";
    c.reveal(slider);
    std::vector<double> elapsed;
    QJsonArray samples;
    for (int index = 0; index < 10; ++index) {
        const auto before = c.editor.currentSettings();
        const auto previous = c.editor.previewUrl();
        const QPointF fraction{.575 + index * .02, .5};
        const auto point = d.point(slider, fraction);
        d.press(slider, d.sliderThumb(slider));
        require(d.object(slider)->property("pressed").toBool() &&
                    c.editor.currentSettings() == before,
                "Feedback sample must start from an accepted, unchanged held pointer");
        struct Presentation {
            std::chrono::steady_clock::time_point start;
            std::atomic<int> readyWidth{0}, synchronizedWidth{0}, presentedWidth{0};
            std::atomic<std::int64_t> elapsed{0};
        };
        const auto presentation = std::make_shared<Presentation>();
        QObject frameObserver;
        QObject::connect(
            c.window, &QQuickWindow::afterSynchronizing, &frameObserver,
            [presentation] {
                presentation->synchronizedWidth.store(presentation->readyWidth.load());
            },
            Qt::DirectConnection);
        QObject::connect(
            c.window, &QQuickWindow::frameSwapped, &frameObserver,
            [presentation] {
                const int width = presentation->synchronizedWidth.load();
                if (!width || presentation->elapsed.load() != 0)
                    return;
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::steady_clock::now() - presentation->start)
                                         .count();
                presentation->presentedWidth.store(width);
                presentation->elapsed.store(elapsed);
            },
            Qt::DirectConnection);
        const auto elapsedMilliseconds = [&] {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                             presentation->start)
                .count();
        };
        double imagePublished = -1;
        QEventLoop deliveryLoop;
        QTimer observation;
        observation.setTimerType(Qt::PreciseTimer);
        observation.setInterval(1);
        QImage quickPixels, interactionPixels;
        // afterAnimating runs on the GUI thread before scene synchronization.
        // Observe the current Ready Image here so timer scheduling cannot miss
        // its first eligible frame. Render-thread callbacks read atomics only.
        QObject::connect(c.window, &QQuickWindow::afterAnimating, &frameObserver, [&] {
            auto *preview = d.item("develop.preview");
            const auto pixels = c.provider->requestImage({}, nullptr, {});
            const bool current =
                c.editor.previewUrl() != previous && !c.editor.busy() && preview->isVisible() &&
                preview->property("status").toInt() == 1 &&
                preview->property("source").toUrl().toString() == c.editor.previewUrl() &&
                (pixels.width() == 512 || pixels.width() == 1024);
            if (current) {
                if (imagePublished < 0)
                    imagePublished = elapsedMilliseconds();
                (pixels.width() == 512 ? quickPixels : interactionPixels) = pixels;
            }
            presentation->readyWidth.store(current ? pixels.width() : 0);
        });
        QObject::connect(&observation, &QTimer::timeout, &observation, [&] {
            if (presentation->elapsed.load() > 0)
                deliveryLoop.quit();
        });
        presentation->start = std::chrono::steady_clock::now();
        observation.start();
        QTest::mouseMove(c.window, point, 0);
        const double inputReturned = elapsedMilliseconds();
        const auto document = c.editor.currentSettings();
        auto expected = before;
        expected.creative.setParameter("presence.clarity",
                                       document.creative.parameter("presence.clarity"));
        require(document == expected && document != before &&
                    document.creative.parameter("presence.clarity") > 0 &&
                    c.editor.parameters().value("presence.clarity").toDouble() ==
                        document.creative.parameter("presence.clarity"),
                "Heavy feedback input must change only positive Clarity");
        try {
            // A native event loop keeps presentation callbacks running continuously;
            // QTest's 10 ms sleep/poll loop would add artificial frame latency.
            QTimer deadline;
            deadline.setSingleShot(true);
            QObject::connect(&deadline, &QTimer::timeout, &deliveryLoop, &QEventLoop::quit);
            deadline.start(10000);
            if (presentation->elapsed.load() == 0)
                deliveryLoop.exec();
            observation.stop();
            QObject::disconnect(c.window, nullptr, &frameObserver, nullptr);
            const double milliseconds = presentation->elapsed.load() / 1000000.;
            require(milliseconds > 0, "Current interaction frame was not queued for presentation");
            // Readback verifies actual Image pixels after timing the synchronized
            // window frame. A separate offscreen grab must not define UI latency.
            const auto displayed = readyItemGrab(c, *d.item("develop.preview"),
                                                 "Presented interaction Image readback completed");
            const double readbackCompleted = elapsedMilliseconds();
            elapsed.push_back(milliseconds);
            require(!displayed->image().isNull() && d.object(slider)->property("pressed").toBool(),
                    "Heavy interaction did not render while the actual pointer remained held");
            // Preserve the actual observed frame before the refinement can replace it.
            const int presentedWidth = presentation->presentedWidth.load();
            const auto feedbackPixels = presentedWidth == 512 ? quickPixels : interactionPixels;
            const auto interactive = reference(c, decoded, document, presentedWidth);
            require(agrees(feedbackPixels, interactive, 1),
                    "Heavy interaction preview differs from independent CPU pixels");
            if (index == 0 || index == 9)
                require(displayed->image().save(c.root +
                                                QString("/heavy-interaction-%1.png").arg(index)),
                        "Cannot retain measured interaction pixels");
            samples.append(QJsonObject{
                {"sample", index + 1},
                {"clarity", document.creative.parameter("presence.clarity")},
                {"input_to_frame_queued_ms", milliseconds},
                {"input_to_image_readback_ms", readbackCompleted},
                {"input_returned_ms", inputReturned},
                {"image_published_ms", imagePublished},
                {"post_presentation_readback_ms", readbackCompleted - milliseconds},
                {"first_feedback_width", presentedWidth},
                {"held_refinement_width", 1024},
                {"metal_preference", c.editor.metalPreview()},
                {"memory_pressure", static_cast<int>(MemoryPressureProbe::current(c.editor))}});
        } catch (...) {
            observation.stop();
            d.release(slider, fraction);
            throw;
        }
        // A held pointer must still refine to the established 1024 image before release.
        ready(c, reference(c, decoded, document, 1024), {}, 1);
        const auto interactiveURL = c.editor.previewUrl();
        d.release(slider, fraction);
        c.saved();
        ready(c, reference(c, decoded, document, 2560), interactiveURL, 1);
        require(savedSnapshot(c).asset.edits == document && c.editor.canUndo() &&
                    !c.editor.canRedo(),
                "Heavy final-quality render lost its complete saved gesture");
        d.log({{"action", "heavy_progressive_feedback"},
               {"sample", index + 1},
               {"input_to_frame_queued_ms", elapsed.back()},
               {"final_width", 2560}});
    }
    std::sort(elapsed.begin(), elapsed.end());
    const auto p95 = elapsed[static_cast<std::size_t>(std::ceil(.95 * elapsed.size())) - 1];
    writeJson(c.root + "/measurements.json",
              {{"schema", 1},
               {"case", id},
               {"samples", samples},
               {"p95_ms", p95},
               {"budget_ms", 50},
               {"backend", "default Metal preference"},
               {"frame_queued_callback_timing", true},
               {"completion_poll_interval_ms", 1},
               {"readiness_observation", "GUI afterAnimating before scene synchronization"},
               {"rgb8_cpu_agreement_limit", 1},
               {"alpha_agreement_limit", 0},
               {"fixture_width", 3072},
               {"fixture_height", 2048},
               {"readback_included_in_latency", false},
               {"readback_verified_after_presentation", true},
               {"frame_requires_current_image_synchronization", true},
               {"display_scanout_measured", false},
               {"independent_oracle_in_timing", false},
               {"cold_unique_edit_revisions", true}});
    require(sourceHash(source.path) == source.hash && c.library.count() == 1,
            "Heavy progressive feedback changed original bytes or membership");
    require(p95 <= 50,
            "Heavy interaction preview exceeds the 50 ms post-performance-phase feedback budget");
    return true;
}

bool progressiveExportScenario(Context &c, const QString &id) {
    if (id != "UX-P06-PRG-002")
        return false;
    auto &d = *c.driver;
    const auto source = openChart(c);
    const auto decoded = luma::PhotoDecoder().decode(source.path.toStdString());
    const auto original = c.editor.currentSettings();
    ready(c, reference(c, decoded, original, 2560));
    c.reveal("develop.exposureSlider");
    const auto oldPreview = c.editor.previewUrl();
    d.press("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"));
    luma::EditDocument document;
    try {
        d.move("develop.exposureSlider", {.6, .5});
        document = c.editor.currentSettings();
        require(document.exposure != original.exposure &&
                    d.object("develop.exposureSlider")->property("pressed").toBool(),
                "Progressive fixture did not hold an actual changed slider gesture");
        const auto interactive = reference(c, decoded, document, 1024);
        require(interactive.width() == 1024, "Expected a distinct 1024 interaction preview");
        ready(c, interactive, oldPreview);
        d.log({{"action", "progressive_interaction_observed"},
               {"width", interactive.width()},
               {"height", interactive.height()},
               {"preview_url", c.editor.previewUrl()},
               {"pressed", d.object("develop.exposureSlider")->property("pressed").toBool()},
               {"latency_qualification", false}});
    } catch (...) {
        d.release("develop.exposureSlider", {.6, .5});
        throw;
    }
    const auto interactionUrl = c.editor.previewUrl();
    d.release("develop.exposureSlider", {.6, .5});
    c.saved();
    ready(c, reference(c, decoded, document, 2560), interactionUrl);
    const auto photo = c.editor.photoID();
    const auto before =
        luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(photo.toStdString());
    require(before && before.value().asset.edits == document && c.editor.canUndo() &&
                !c.editor.canRedo(),
            "Progressive final stage did not save one coherent gesture");
    const auto selected = c.library.selectionIDs();
    const auto settings = c.exporter.settings();
    for (const auto *key : {"resize", "colorSpace", "outputMedium", "watermarkKind"})
        require(settings.contains(key) && settings.value(key).toInt() == 0,
                "Progressive export requires Original/sRGB/unsharpened/unwatermarked defaults");
    const auto expected = reference(c, decoded, document, 0, false);
    const auto output = c.exportPNG("Progressive-full-resolution");
    const QImage exported(output);
    require(expected.size() == QSize(3072, 2048) && exported.size() == expected.size() &&
                exported.colorSpace() == QColorSpace(QColorSpace::SRgb),
            "Progressive export lost full resolution or its sRGB declaration");
    require(agrees(exported, expected, 0),
            "Exported PNG differs from the independent full-source CPU document");
    const auto after =
        luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(photo.toStdString());
    require(after && after->revision == before.value().revision && after->asset.edits == document &&
                after->asset.path == before.value().asset.path &&
                after->asset.checksum == before.value().asset.checksum &&
                c.editor.currentSettings() == document && c.editor.photoID() == photo &&
                c.library.count() == 1 && c.library.selectionIDs() == selected &&
                sourceHash(source.path) == source.hash && !c.editor.saving(),
            "Progressive/export changed the saved document, source, identity or selection");
    d.click("main.undoButton");
    ready(c, reference(c, decoded, original, 2560));
    require(c.editor.currentSettings() == original && !c.editor.canUndo() && c.editor.canRedo(),
            "Progressive stages or export polluted single-gesture Undo");
    d.click("main.redoButton");
    ready(c, reference(c, decoded, document, 2560));
    c.saved();
    require(c.editor.currentSettings() == document && c.editor.canUndo() && !c.editor.canRedo(),
            "Progressive stages or export polluted Redo");
    d.log({{"action", "progressive_full_export"},
           {"width", exported.width()},
           {"height", exported.height()},
           {"output_sha256", QString::fromLatin1(sourceHash(output))},
           {"original_sha256", QString::fromLatin1(sourceHash(source.path))},
           {"pixel_budget", 0},
           {"display_profile_used_for_export_reference", false}});
    require(c.warnings.isEmpty(),
            "Progressive export produced QML warnings: " + c.warnings.join('\n'));
    return true;
}
bool zoomSwitchScenario(Context &c, const QString &id) {
    if (id != "UX-P06-ZOM-005")
        return false;
    auto &d = *c.driver;
    const auto source = openChart(c, true);
    const auto second = QFileInfo(source.path).absolutePath() + "/Zoom2.png";
    const auto secondHash = source.secondHash;
    const auto decoded = luma::PhotoDecoder().decode(source.path.toStdString());
    const auto nextDecoded = luma::PhotoDecoder().decode(second.toStdString());
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.6, .5});
    c.saved();
    const auto document = c.editor.currentSettings();
    const auto before = savedSnapshot(c);
    const auto fit = reference(c, decoded, document, 2560);
    ready(c, fit);
    d.click("develop.actualPixels");
    ready(c, reference(c, decoded, document, 0));
    mapping(c, reference(c, decoded, document, 0));
    const auto *view = d.item("develop.viewport");
    const auto x = view->property("contentX").toDouble();
    const auto y = view->property("contentY").toDouble();
    d.drag("develop.viewport", {.5, .5}, {.25, .25});
    require(view->property("contentX").toDouble() != x &&
                view->property("contentY").toDouble() != y,
            "Zoom photo switch needs actual non-central pan input");
    const luma::EditDocument neutral;
    filmstrip(c, 1, second, neutral, reference(c, nextDecoded, neutral, 2560));
    const auto nextSaved = savedSnapshot(c);
    filmstrip(c, 0, source.path, document, fit);
    sameSaved(c, before);
    sameSaved(c, nextSaved);
    require(sourceHash(source.path) == source.hash && sourceHash(second) == secondHash,
            "Photo switch changed original bytes");
    d.log({{"action", "zoom_switch_reset_fit"},
           {"first_uuid", QString::fromStdString(before.asset.id)},
           {"second_uuid", QString::fromStdString(nextSaved.asset.id)},
           {"first_sha256", QString::fromLatin1(source.hash)},
           {"second_sha256", QString::fromLatin1(secondHash)},
           {"policy", "reset zoom/pan on each photo load; preserve saved edits; reset history"}});
    require(c.warnings.isEmpty(), "Zoom switch produced QML warnings: " + c.warnings.join('\n'));
    return true;
}
bool progressiveRaceScenario(Context &c, const QString &id) {
    const bool switchAsset = id == "UX-P06-PRG-009";
    if (!switchAsset && id != "UX-P06-PRG-004")
        return false;
    auto &d = *c.driver;
    const auto source = openChart(c, switchAsset);
    const auto second = QFileInfo(source.path).absolutePath() + "/Zoom2.png";
    const auto secondHash = source.secondHash;
    const auto decoded = luma::PhotoDecoder().decode(source.path.toStdString());
    ready(c, reference(c, decoded, c.editor.currentSettings(), 2560));
    c.reveal("develop.exposureSlider");
    PreviewResultGate gate(c.editor, 2560);
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.6, .5});
    c.saved();
    const auto obsolete = c.editor.currentSettings();
    const auto oldExpected = reference(c, decoded, obsolete, 2560);
    d.wait(
        [&] {
            const auto state = gate.snapshot();
            return state.held && state.held->job->state.load() == luma::JobState::completed;
        },
        "Actual old final render finished while its success dispatch is retained");
    const auto held = gate.snapshot();
    require(!held.hookFailed && !held.released && held.deliveries == 0 &&
                held.pixels == oldExpected &&
                held.held->assetID == c.editor.photoID().toStdString(),
            "Race gate did not retain the real old final result");
    const auto firstSaved = savedSnapshot(c);
    QImage newExpected;
    if (switchAsset) {
        const auto nextDecoded = luma::PhotoDecoder().decode(second.toStdString());
        const luma::EditDocument neutral;
        newExpected = reference(c, nextDecoded, neutral, 2560);
        LoadResultGate loadGate(c.editor);
        QSignalSpy opens(&c.library, &LibraryModel::openRequested);
        QSignalSpy loads(&c.editor, &EditorController::photoLoaded);
        require(opens.isValid() && loads.isValid(), "Cannot observe pending photo switch");
        d.click(c.card(1, true));
        d.wait([&] { return loadGate.snapshot().held.has_value(); },
               "Next real session decoded, with load success still pending");
        const auto pendingLoad = loadGate.snapshot();
        require(
            c.editor.busy() && c.editor.photoID().toStdString() == firstSaved.asset.id &&
                pendingLoad.held->sourcePath == second.toStdString() &&
                pendingLoad.deliveries == 0 && !pendingLoad.timedOut && !pendingLoad.hookFailed &&
                opens.count() == 1 && opens.at(0).at(0).toUrl().toLocalFile() == second,
            "Old completion must arrive after actual switch began and before new load delivery");
        const auto pendingUrl = c.editor.previewUrl();
        const auto pendingPixels = c.provider->requestImage({}, nullptr, {});
        const auto pendingHistogram = c.editor.histogram();
        QSignalSpy pendingChanges(&c.editor, &EditorController::stateChanged);
        QSignalSpy pendingHistograms(&c.editor, &EditorController::histogramChanged);
        gate.release();
        d.wait([&] { return gate.snapshot().deliveries == 1; },
               "Old render success delivered while new load is held");
        d.frame();
        require(gate.snapshot().rejections == 1 && c.editor.busy() && loads.isEmpty() &&
                    c.editor.previewUrl() == pendingUrl &&
                    c.provider->requestImage({}, nullptr, {}) == pendingPixels &&
                    c.editor.histogram() == pendingHistogram &&
                    c.editor.currentSettings() == obsolete && pendingChanges.isEmpty() &&
                    pendingHistograms.isEmpty(),
                "Photo switch failed to invalidate the old stage before the new photo loaded");
        d.log({{"action", "progressive_rejected_during_photo_load"},
               {"busy", c.editor.busy()},
               {"new_load_deliveries", loads.count()},
               {"old_render_rejections", int(gate.snapshot().rejections)}});
        loadGate.release();
        d.wait([&] { return loads.count() == 1 && !c.editor.busy(); },
               "Retained new photo load completed");
        ready(c, newExpected);
        const auto loaded = loadGate.snapshot();
        require(loaded.deliveries == 1 && loaded.successfulArrivals == 1 && loaded.lastDelivered &&
                    loaded.held && loaded.held->sameAs(*loaded.lastDelivered) &&
                    !loaded.hookFailed && !loaded.timedOut && !loaded.deliveryBeforeRelease &&
                    loaded.mismatchedDeliveries == 0 && loaded.leftHold &&
                    c.editor.sourcePath() == second && c.editor.currentSettings() == neutral &&
                    c.editor.photoID() == loads.at(0).at(0).toString() && !c.editor.canUndo() &&
                    !c.editor.canRedo(),
                "New real photo load lost identity/document/history after old render rejection");
        fitGeometry(c);
    } else {
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.67, .5});
        c.saved();
        newExpected = reference(c, decoded, c.editor.currentSettings(), 2560);
        ready(c, newExpected);
    }
    require(oldExpected != newExpected && held.held->job->cancellation.token().isCancelled(),
            "New revision must differ and cancel the old render handle");
    const auto document = c.editor.currentSettings();
    const auto photo = c.editor.photoID();
    const auto url = c.editor.previewUrl();
    const auto selected = c.library.selectionIDs();
    const auto histogram = c.editor.histogram();
    const auto before = savedSnapshot(c);
    const bool undo = c.editor.canUndo(), redo = c.editor.canRedo();
    require(!c.editor.histogramPending(), "New final histogram is still pending");
    QSignalSpy changes(&c.editor, &EditorController::stateChanged);
    QSignalSpy saves(&c.editor, &EditorController::editsSaved);
    QSignalSpy histograms(&c.editor, &EditorController::histogramChanged);
    require(changes.isValid() && saves.isValid() && histograms.isValid(),
            "Cannot audit stale result delivery side effects");
    gate.release();
    d.wait([&] { return gate.snapshot().deliveries == 1; },
           "Retained real success callback delivered");
    d.frame();
    const auto result = gate.snapshot();
    require(result.held && result.delivered && result.held->sameAs(*result.delivered) &&
                result.deliveries == 1 && result.rejections == 1 && result.released &&
                result.rejectionRevision > result.held->revision && !result.deliveryBeforeRelease &&
                !result.hookFailed,
            "Original revision guard did not reject the exact delayed success once");
    require(c.editor.previewUrl() == url &&
                c.provider->requestImage({}, nullptr, {}) == newExpected &&
                c.editor.histogram() == histogram && !c.editor.histogramPending() &&
                c.editor.currentSettings() == document && c.editor.photoID() == photo &&
                c.library.selectionIDs() == selected && c.editor.canUndo() == undo &&
                c.editor.canRedo() == redo && changes.isEmpty() && saves.isEmpty() &&
                histograms.isEmpty() && !c.editor.saving() && c.editor.error().isEmpty(),
            "Late completion changed current image, histogram, document, selection or history");
    sameSaved(c, before);
    d.log({{"action", "progressive_stale_success_rejected"},
           {"held_revision", QString::number(result.held->revision)},
           {"current_revision", QString::number(result.rejectionRevision)},
           {"held_uuid", QString::fromStdString(result.held->assetID)},
           {"current_uuid", photo},
           {"held_resolution", result.held->resolution},
           {"release_order", switchAsset ? "during next load" : "after newer final preview"},
           {"deliveries", int(result.deliveries)},
           {"guard_rejections", int(result.rejections)},
           {"old_handle_cancelled", result.held->job->cancellation.token().isCancelled()},
           {"retained_result_equals_old_cpu", held.pixels == oldExpected},
           {"current_result_equals_new_cpu",
            c.provider->requestImage({}, nullptr, {}) == newExpected},
           {"dispatch_boundary",
            "actual render success after last cancellation check, before QueuedConnection"}});
    if (switchAsset) {
        sameSaved(c, firstSaved);
        filmstrip(c, 0, source.path, obsolete, oldExpected);
    } else {
        d.click("main.undoButton");
        ready(c, oldExpected);
        require(c.editor.currentSettings() == obsolete && c.editor.canUndo() && c.editor.canRedo(),
                "Late render completion corrupted the two-gesture Undo history");
        d.click("main.redoButton");
        ready(c, newExpected);
        require(c.editor.currentSettings() == document && c.editor.canUndo() && !c.editor.canRedo(),
                "Late render completion corrupted Redo history");
        c.saved();
    }
    require(sourceHash(source.path) == source.hash &&
                (!switchAsset || sourceHash(second) == secondHash),
            "Progressive race changed original bytes");
    d.log({{"action", "progressive_race_originals_preserved"},
           {"first_sha256", QString::fromLatin1(source.hash)},
           {"second_sha256", QString::fromLatin1(secondHash)}});
    require(c.warnings.isEmpty(),
            "Progressive race produced QML warnings: " + c.warnings.join('\n'));
    return true;
}

bool progressiveTransientScenario(Context &c, const QString &id) {
    const bool originalCase = id == "UX-P06-PRG-007";
    if (!originalCase && id != "UX-P06-PRG-008")
        return false;
    auto &d = *c.driver;
    const auto source = openChart(c);
    const auto decoded = luma::PhotoDecoder().decode(source.path.toStdString());
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.6, .5});
    c.saved();
    const auto document = c.editor.currentSettings();
    require(document.exposure != 0 && c.editor.canUndo() && !c.editor.canRedo(),
            "Transient test needs one saved, non-neutral UI gesture");
    const auto edited = reference(c, decoded, document, 2560);
    const auto neutral = reference(c, decoded, {}, 2560);
    require(edited != neutral, "Transient fixture cannot distinguish Original and Processed");
    ready(c, edited);
    const auto before = savedSnapshot(c);
    const auto selection = c.library.selectionIDs();
    QSignalSpy saves(&c.editor, &EditorController::editsSaved);
    require(saves.isValid(), "Cannot observe unintended transient saves");
    if (originalCase) {
        const auto originalInteraction = reference(c, decoded, {}, 1024);
        const auto editedInteraction = reference(c, decoded, document, 1024);
        for (const bool original : {true, false}) {
            // Exercise both obsolete Original -> Processed and the reverse.
            if (!original) {
                d.click("develop.originalButton");
                ready(c, neutral);
            }
            const auto phase = original ? QStringLiteral("Original-to-Processed")
                                        : QStringLiteral("Processed-to-Original");
            PreviewResultGate gate(c.editor, 2560);
            const auto previous = c.editor.previewUrl();
            d.click(original ? "develop.originalButton" : "develop.processedButton");
            ready(c, original ? originalInteraction : editedInteraction, previous);
            require(c.editor.original() == original && !c.editor.presetPreview(),
                    "Original/Processed input did not select the intended mode");
            heldFinal(c, gate, original ? neutral : edited, phase);
            d.log({{"action", "progressive_transient_interaction"},
                   {"phase", phase},
                   {"width", c.provider->requestImage({}, nullptr, {}).width()}});
            d.click(original ? "develop.processedButton" : "develop.originalButton");
            const auto &replacement = original ? edited : neutral;
            ready(c, replacement);
            require(c.editor.original() != original, "Replacement mode was not selected");
            rejectTransient(c, gate, replacement, phase);
            sameSaved(c, before);
        }
        d.click("develop.processedButton");
        ready(c, edited);
    } else {
        const auto preset = c.presets.snapshot("builtin:soft-portrait");
        const auto hoveredDocument =
            luma::mergeSettings(document, preset.settings, preset.groups, preset.parameters);
        const auto hoverInteraction = reference(c, decoded, hoveredDocument, 1024);
        const auto hoverFinal = reference(c, decoded, hoveredDocument, 2560);
        const auto builtins = c.presets.builtinItems();
        const auto userPresets = c.presets.items();
        require(hoveredDocument != document && hoverFinal != edited,
                "Hover fixture must distinguish the declared preset subset");
        for (const bool obsolete : {false, true}) {
            const auto phase = obsolete ? QStringLiteral("hover-leave-before-final")
                                        : QStringLiteral("hover-final-before-leave");
            PreviewResultGate gate(c.editor, 2560);
            const auto previous = c.editor.previewUrl();
            d.move("filter_soft-portrait", {.5, .5});
            ready(c, hoverInteraction, previous);
            require(c.editor.presetPreview() && !c.editor.original() &&
                        d.object("filter_soft-portrait")->property("hovered").toBool(),
                    "Real pointer hover did not activate the preset preview");
            heldFinal(c, gate, hoverFinal, phase);
            if (!obsolete) {
                gate.release();
                ready(c, hoverFinal);
                const auto result = gate.snapshot();
                require(result.held && result.delivered && result.held->sameAs(*result.delivered) &&
                            result.deliveries == 1 && result.rejections == 0 &&
                            !result.deliveryBeforeRelease && !result.hookFailed &&
                            c.editor.presetPreview() && c.editor.currentSettings() == document &&
                            !c.editor.saving() && saves.isEmpty(),
                        "A current final hover must display without persisting the preset");
                d.log({{"action", "progressive_transient_current_accepted"},
                       {"phase", phase},
                       {"width", c.provider->requestImage({}, nullptr, {}).width()},
                       {"deliveries", int(result.deliveries)},
                       {"rejections", int(result.rejections)}});
                sameSaved(c, before);
            }
            d.move("develop.preview", {.5, .5});
            ready(c, edited);
            require(!c.editor.presetPreview(), "Pointer leave retained a preset preview");
            if (obsolete)
                rejectTransient(c, gate, edited, phase);
            sameSaved(c, before);
        }
        require(c.presets.builtinItems() == builtins && c.presets.items() == userPresets,
                "Hover changed builtin/user preset data");
    }
    sameSaved(c, before);
    require(c.editor.currentSettings() == document &&
                c.editor.photoID().toStdString() == before.asset.id &&
                c.editor.sourcePath() == source.path && c.library.count() == 1 &&
                c.library.selectionIDs() == selection && saves.isEmpty() && !c.editor.saving() &&
                c.editor.canUndo() && !c.editor.canRedo() && !c.editor.original() &&
                !c.editor.presetPreview(),
            "Original/hover modified persisted edits, identity, membership, selection or history");
    transientHistory(c, document, neutral, edited);
    require(sourceHash(source.path) == source.hash, "Transient preview changed original bytes");
    d.log({{"action", "progressive_transient_preserved"},
           {"scenario", id},
           {"original_sha256", QString::fromLatin1(source.hash)},
           {"saved_revision_before", QString::number(before.revision)},
           {"view_modes_created_history_entries", false}});
    require(c.warnings.isEmpty(),
            "Transient preview produced QML warnings: " + c.warnings.join('\n'));
    return true;
}

} // namespace ux

namespace ux {

namespace {
QPointF normalizedSource(Context &c, QPointF point) {
    auto *view = c.driver->item("develop.viewport");
    auto *image = c.driver->item("develop.preview");
    const auto local = image->mapFromItem(view, point);
    const auto width = image->property("paintedWidth").toDouble();
    const auto height = image->property("paintedHeight").toDouble();
    require(width > 0 && height > 0, "Zoom source has no painted geometry");
    return {(local.x() - (image->width() - width) / 2) / width,
            (local.y() - (image->height() - height) / 2) / height};
}
void anchored(Context &c, QPointF point, QPointF before, double sourcePixelBudget = 0) {
    const auto after = normalizedSource(c, point);
    c.driver->log({{"action", "zoom_pointer_anchor"},
                   {"before_x", before.x()},
                   {"before_y", before.y()},
                   {"after_x", after.x()},
                   {"after_y", after.y()},
                   {"source_pixel_budget", sourcePixelBudget}});
    const auto size = c.editor.previewPixelSize();
    require(std::abs(before.x() - after.x()) * size.width() <= sourcePixelBudget + 1e-6 &&
                std::abs(before.y() - after.y()) * size.height() <= sourcePixelBudget + 1e-6,
            "Zoom moved the source coordinate beneath the pointer");
}
void wheelAt(Context &c, QPointF point, QPoint pixels, QPoint angle,
             Qt::ScrollPhase phase = Qt::NoScrollPhase) {
    auto *view = c.driver->item("develop.viewport");
    const auto scene = view->mapToScene(point);
    QWheelEvent event(scene, c.window->mapToGlobal(scene.toPoint()), pixels, angle, Qt::NoButton,
                      Qt::NoModifier, phase, false);
    c.driver->log({{"action", "viewport_wheel_event"},
                   {"pixel_x", pixels.x()},
                   {"pixel_y", pixels.y()},
                   {"angle_x", angle.x()},
                   {"angle_y", angle.y()},
                   {"phase", int(phase)}});
    QCoreApplication::sendEvent(c.window, &event);
}
void scaleIs(Context &c, double scale) {
    c.driver->wait(
        [&] {
            return c.editor.zoomMode() == "manual" &&
                   std::abs(c.editor.zoomFactor() - scale) < 1e-9;
        },
        "Actual zoom factor from UI input");
    c.driver->frame();
    auto *image = c.driver->item("develop.preview");
    const auto source = c.editor.previewPixelSize();
    const auto dpr = c.window->devicePixelRatio();
    require(std::abs(image->width() * dpr / source.width() - scale) < 1e-9 &&
                std::abs(image->height() * dpr / source.height() - scale) < 1e-9,
            "Manual zoom factor differs from actual scene geometry");
}
void percent(Context &c, const QString &text, double scale) {
    c.driver->type("develop.zoomPercent", text);
    c.driver->key(Qt::Key_Return);
    scaleIs(c, scale);
}
} // namespace
bool zoomInteractionScenario(Context &c, const QString &id) {
    if (id != "UX-P06-ZOM-003" && id != "UX-P06-ZOM-004" && id != "UX-P06-ZOM-012" &&
        id != "UX-PRV-012")
        return false;
    auto &d = *c.driver;
    QObject availabilityObserver;
    observeZoomAvailability(c, availabilityObserver);
    const auto source = openChart(c);
    const auto document = c.editor.currentSettings();
    const auto [fit, full] = [&] {
        const auto decoded = luma::PhotoDecoder().decode(source.path.toStdString());
        return std::pair{reference(c, decoded, document, 2560), reference(c, decoded, document, 0)};
    }();
    ready(c, fit);
    awaitZoomControls(c);
    const auto saved = savedSnapshot(c);
    const auto selection = c.library.selectionIDs();
    QSignalSpy saves(&c.editor, &EditorController::editsSaved);
    auto *view = d.item("develop.viewport");
    const QPointF point(view->width() * .43, view->height() * .57);
    if (id == "UX-PRV-012") {
        for (int cycle = 0; cycle < 2; ++cycle) {
            d.doubleClick("develop.viewport");
            ready(c, full);
            mapping(c, full);
            d.doubleClick("develop.viewport");
            ready(c, fit);
            fitGeometry(c);
        }
        percent(c, "200", 2);
        ready(c, full);
        const auto scenePoint = view->mapToScene(point).toPoint();
        const auto localPoint = view->mapFromScene(scenePoint);
        const auto before = normalizedSource(c, localPoint);
        QTest::mouseDClick(c.window, Qt::LeftButton, Qt::NoModifier, scenePoint);
        scaleIs(c, 1);
        // The input point is exact; 1:1 snaps to the nearest backing pixel.
        anchored(c, localPoint, before, .5);
    } else if (id == "UX-P06-ZOM-003") {
        const auto beforeFit = normalizedSource(c, point);
        const auto fitScale = view->property("effectiveZoom").toDouble();
        wheelAt(c, point, {}, {0, 360});
        scaleIs(c, fitScale * 2);
        ready(c, full);
        // The 2560 x 1707 Fit raster rounds the aspect ratio by less than
        // half a full-resolution pixel; full-to-full anchors below remain exact.
        anchored(c, point, beforeFit, .5);
        const auto url = c.editor.previewUrl();
        const auto beforeBurst = normalizedSource(c, point);
        const auto old = c.editor.zoomFactor();
        // Deliberately no event-loop/layout wait between these actual events.
        for (int i = 0; i < 3; ++i)
            wheelAt(c, point, {}, {0, 120});
        scaleIs(c, old * 2);
        anchored(c, point, beforeBurst);
        percent(c, "200", 2);
        percent(c, "137.5", 1.375);
        percent(c, "1", .01);
        require(view->property("contentX").toDouble() == 0 &&
                    view->property("contentY").toDouble() == 0,
                "Small image retained an invalid pan offset");
        percent(c, "1600", 16);
        for (const auto *name : {"cropOverlay", "transformOverlay", "localPhotoOverlay"}) {
            const auto *overlay = d.item(QString::fromLatin1(name));
            require(!overlay->isVisible() && overlay->width() <= 84 && overlay->height() <= 84,
                    "Inactive tool Canvas grew to the magnified image dimensions");
        }
        d.screenshot(c.root + "/zoom-1600-scene.png");
        for (double invalid : {-1., 0., .009, 16.001, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()})
            c.editor.setZoomFactor(invalid);
        c.editor.setZoomMode("unknown");
        scaleIs(c, 16);
        d.type("develop.zoomPercent", "0");
        d.key(Qt::Key_Escape);
        scaleIs(c, 16);
        d.click("develop.fill");
        d.frame();
        const auto scale = std::max(view->width() * c.window->devicePixelRatio() / full.width(),
                                    view->height() * c.window->devicePixelRatio() / full.height());
        require(c.editor.zoomMode() == "fill" &&
                    std::abs(view->property("effectiveZoom").toDouble() - scale) < 1e-9,
                "Fill does not cover both canvas dimensions");
        d.click("develop.actualPixels");
        mapping(c, full);
        require(c.editor.previewUrl() == url && c.provider->requestImage({}, nullptr, {}) == full,
                "Viewport-only scale changes started a redundant render or changed pixels");
        d.resize({1000, 640});
        for (int language = 0; language < 8; ++language) {
            d.click("settingsButton");
            d.wait([&] { return d.object("preferencesPanel")->property("opened").toBool(); },
                   "Zoom localization preferences opened");
            d.choose("languageChoice", language);
            d.key(Qt::Key_Escape);
            d.wait([&] { return !d.object("preferencesPanel")->property("visible").toBool(); },
                   "Zoom localization preferences closed");
            d.frame();
            const auto *pane = d.item("develop.canvasPane");
            QRectF previous;
            for (const auto *name :
                 {"develop.fit", "develop.fill", "develop.zoomPercent", "develop.actualPixels"}) {
                const auto *control = d.item(QString::fromLatin1(name));
                const auto rectangle = control->mapRectToItem(pane, control->boundingRect());
                require(control->isVisible() && control->isEnabled() &&
                            pane->boundingRect().contains(rectangle) &&
                            (previous.isEmpty() || !previous.intersects(rectangle)),
                        "Localized zoom controls overlap or leave minimum-width canvas");
                previous = rectangle;
            }
            d.log({{"action", "zoom_minimum_window_locale"},
                   {"locale", c.localization->language()},
                   {"window_width", c.window->width()},
                   {"window_height", c.window->height()}});
        }
    } else {
        percent(c, "200", 2);
        ready(c, full);
        const auto url = c.editor.previewUrl();
        if (id == "UX-P06-ZOM-004") {
            wheelAt(c, point, {100000, 100000}, {}, Qt::ScrollUpdate);
            d.frame();
            require(view->property("contentX").toDouble() == 0 &&
                        view->property("contentY").toDouble() == 0,
                    "Pan escaped upper/left bounds");
            wheelAt(c, point, {-100000, 0}, {}, Qt::ScrollUpdate);
            d.frame();
            require(std::abs(view->property("contentX").toDouble() -
                             (view->property("contentWidth").toDouble() - view->width())) < 1e-6 &&
                        view->property("contentY").toDouble() == 0,
                    "Pure horizontal pan failed or changed vertical position");
            wheelAt(c, point, {0, -100000}, {}, Qt::ScrollMomentum);
            d.frame();
            require(std::abs(view->property("contentY").toDouble() -
                             (view->property("contentHeight").toDouble() - view->height())) < 1e-6,
                    "Momentum pan escaped lower bound");
            percent(c, "1", .01);
            wheelAt(c, point, {-100000, -100000}, {}, Qt::ScrollUpdate);
            d.frame();
            require(view->property("contentX").toDouble() == 0 &&
                        view->property("contentY").toDouble() == 0,
                    "Pan moved an image smaller than the canvas");
        } else {
            QPointingDevice pad(
                "Luma UX synthetic touchpad", 9137, QInputDevice::DeviceType::TouchPad,
                QPointingDevice::PointerType::Finger,
                QInputDevice::Capability::Position | QInputDevice::Capability::PixelScroll, 2, 0);
            const auto scene = view->mapToScene(point);
            const auto before = normalizedSource(c, point);
            const auto gesture = [&](Qt::NativeGestureType type, double value) {
                QNativeGestureEvent event(type, &pad, 2, scene, scene,
                                          c.window->mapToGlobal(scene.toPoint()), value, {});
                QCoreApplication::sendEvent(c.window, &event);
            };
            gesture(Qt::BeginNativeGesture, 0);
            gesture(Qt::ZoomNativeGesture, .25);
            gesture(Qt::ZoomNativeGesture, .2);
            gesture(Qt::EndNativeGesture, 0);
            scaleIs(c, 3);
            anchored(c, point, before);
            const double x = view->property("contentX").toDouble();
            const double y = view->property("contentY").toDouble();
            wheelAt(c, point, {-41, -29}, {}, Qt::ScrollUpdate);
            d.frame();
            require(view->property("contentX").toDouble() == x + 41 &&
                        view->property("contentY").toDouble() == y + 29 &&
                        c.editor.zoomFactor() == 3,
                    "Smooth scroll did not pan independently of pinch zoom");
            d.log({{"action", "native_gesture_contract"},
                   {"scope",
                    "synthetic Qt native-gesture delivery; physical trackpad not measured"}});
        }
        require(c.editor.previewUrl() == url && c.provider->requestImage({}, nullptr, {}) == full,
                "Pan/pinch changed full-resolution pixels or launched another render");
    }
    sameSaved(c, saved);
    require(sourceHash(source.path) == source.hash && c.editor.currentSettings() == document &&
                !c.editor.canUndo() && !c.editor.canRedo() && saves.isEmpty() &&
                selection == c.library.selectionIDs(),
            "Zoom input changed original, saved edits, selection or history");
    require(c.warnings.isEmpty(), "Zoom input produced QML warnings: " + c.warnings.join('\n'));
    d.log({{"action", "zoom_input_contract"},
           {"scope", "Actual Qt input and scene geometry; tool-overlay mapping remains separate"}});
    return true;
}

} // namespace ux

namespace ux {
bool zoomToolsScenario(Context &c, const QString &id) {
    const bool crop = id == "UX-P06-ZOM-006", guided = id == "UX-P06-ZOM-007",
               brush = id == "UX-P06-ZOM-008", targeted = id == "UX-P06-ZOM-009";
    if (!crop && !guided && !brush && !targeted)
        return false;
    auto &d = *c.driver;
    QObject pressureObserver;
    auto lastPressure = MemoryPressureProbe::current(c.editor);
    QObject::connect(&c.editor, &EditorController::stateChanged, &pressureObserver, [&] {
        const auto current = MemoryPressureProbe::current(c.editor);
        if (current != lastPressure) {
            d.log({{"action", "zoom_tool_actual_os_pressure"},
                   {"before", int(lastPressure)},
                   {"after", int(current)},
                   {"zoom_mode", c.editor.zoomMode()}});
            lastPressure = current;
        }
    });
    const auto source = openChart(c, false, targeted ? QSize{2048, 1536} : QSize{3072, 2304});
    const auto decoded = luma::PhotoDecoder().decode(source.path.toStdString());
    auto waitTool = [&] {
        d.wait(
            [&] {
                return !c.editor.histogramPending() && (!crop || c.editor.cropCanvasReady()) &&
                       (!guided || c.editor.guidedCanvasReady()) &&
                       (!brush || c.editor.localCanvasReady());
            },
            "Zoom tool canvas and final preview ready");
        d.frame();
    };
    if (crop) {
        c.reveal("cropTool");
        d.click("cropTool");
        waitTool();
        // A small inset is an explicit fixture precondition. The measured drag
        // below uses actual window input while this corner remains in view.
        c.editor.setCropRect(.45, .45, .1, .1);
        c.saved();
        waitTool();
    } else if (guided) {
        c.reveal("transformGuided");
        d.click("transformGuided");
        waitTool();
        c.reveal("transformGuideLayout");
        d.choose("transformGuideLayout", 0);
        d.dragBetween("transformGuidePoint0_0", {.5, .5}, "transformOverlay", {.45, .45});
    } else if (brush) {
        c.addMask(2);
        waitTool();
    }
    const int count = targeted ? 4 : 2;
    for (int pass = 0; pass < count; ++pass) {
        const bool hsl = targeted && pass >= 2;
        if (targeted && pass % 2 == 0) {
            if (hsl) {
                c.reveal("hsl.targetMode");
                d.choose("hsl.targetMode", 1);
            }
            const QString tool = hsl ? "hsl.target" : "curves.target";
            c.reveal(tool);
            d.click(tool);
            waitTool();
        }
        d.log({{"action", "zoom_tool_availability"},
               {"pressure", int(MemoryPressureProbe::current(c.editor))},
               {"busy", c.editor.busy()},
               {"locked", c.editor.editingLocked()},
               {"white_balance", c.editor.whiteBalancePicking()},
               {"point_color", c.editor.pointColorPicking()},
               {"retouch", c.editor.retouchMode()},
               {"available", c.editor.zoomAvailable()}});
        require(c.editor.zoomAvailable(), "Active editing tool disables required zoom/pan");
        if (pass % 2 == 0)
            d.click("develop.actualPixels");
        else
            percent(c, "137.5", 1.375);
        waitTool();
        const double scale = pass % 2 == 0 ? 1 : 1.375;
        require(c.editor.zoomMode() == "manual" && c.editor.zoomFactor() == scale,
                "Tool unexpectedly reset the requested zoom to Fit");
        require(c.provider->requestImage({}, nullptr, {}).size() ==
                    QSize(decoded.image.width, decoded.image.height),
                "Zoom tool renders a reduced rather than full source canvas");
        auto *view = d.item("develop.viewport"), *image = d.item("develop.preview");
        const auto oldX = view->property("contentX").toDouble();
        const auto oldY = view->property("contentY").toDouble();
        wheelAt(c, {view->width() / 2, view->height() / 2}, {-43, -31}, {}, Qt::ScrollUpdate);
        d.frame();
        require(view->property("contentX").toDouble() > oldX &&
                    view->property("contentY").toDouble() > oldY,
                "Editing tool cannot pan in both axes");
        const auto panX = view->property("contentX").toDouble(),
                   panY = view->property("contentY").toDouble();
        if (!targeted) {
            auto *canvas = d.item(crop     ? "CropOverlay.canvas"
                                  : guided ? "TransformOverlay.canvas"
                                           : "LocalOverlay.canvas");
            require(canvas->width() <= view->width() + 1 &&
                        canvas->height() <= view->height() + 1 &&
                        view->boundingRect()
                            .adjusted(-1, -1, 1, 1)
                            .contains(canvas->mapRectToItem(view, canvas->boundingRect())),
                    "Active zoomed Canvas backing is not bounded to the visible viewport");
        }
        const auto before = c.editor.currentSettings();
        const auto previous = c.editor.previewUrl();
        auto actualPoint = [&](const QString &area, QPointF fraction) {
            const auto scene = d.point(area, fraction);
            const auto viewportPoint = view->mapFromScene(scene);
            require(view->boundingRect().contains(viewportPoint),
                    "Zoom tool input is outside viewport clipping");
            const auto point = image->mapFromScene(scene);
            return QPointF(
                (point.x() - (image->width() - image->property("paintedWidth").toDouble()) / 2) /
                    image->property("paintedWidth").toDouble(),
                (point.y() - (image->height() - image->property("paintedHeight").toDouble()) / 2) /
                    image->property("paintedHeight").toDouble());
        };
        if (crop) {
            const auto start = actualPoint("cropDragArea", {.45, .45});
            const auto end = actualPoint("cropDragArea", {.47, .46});
            require(start.x() < end.x() && start.y() < end.y(), "Invalid crop pointer fixture");
            d.drag("cropDragArea", {.45, .45}, {.47, .46});
            c.saved();
            waitTool();
            const auto frame = c.editor.cropState();
            d.log({{"action", "zoom_crop_coordinates"},
                   {"actual", QJsonObject::fromVariantMap(frame)},
                   {"expected_x", end.x()},
                   {"expected_y", end.y()},
                   {"scale", scale}});
            require(std::abs(frame["x"].toDouble() - end.x()) < .001 &&
                        std::abs(frame["y"].toDouble() - end.y()) < .001 &&
                        std::abs(frame["x"].toDouble() + frame["width"].toDouble() - .55) < .001 &&
                        std::abs(frame["y"].toDouble() + frame["height"].toDouble() - .55) < .001,
                    "Zoomed crop corner does not match the actual source coordinate");
        } else if (guided) {
            const auto end = actualPoint("transformOverlay", {.50, .48});
            d.dragBetween("transformGuidePoint0_0", {.5, .5}, "transformOverlay", {.50, .48});
            const auto guide = c.editor.transformGuides().front().toMap();
            require(std::abs(guide["x1"].toDouble() - end.x()) < 1e-6 &&
                        std::abs(guide["y1"].toDouble() - end.y()) < 1e-6 &&
                        c.editor.currentSettings() == before,
                    "Zoomed guide coordinates differ or transient guide edited the document");
            d.dragBetween("transformGuidePoint0_0", {.5, .5}, "transformOverlay", {.45, .45});
        } else if (brush) {
            const auto start = actualPoint("localMaskGestureArea", {.5, .5});
            const auto end = actualPoint("localMaskGestureArea", {.53, .52});
            d.drag("localMaskGestureArea", {.5, .5}, {.53, .52});
            c.saved();
            waitTool();
            const auto after = c.editor.currentSettings();
            require(after.localMasks.size() == 1 && after.localMasks[0].strokes.size() == 1,
                    "Zoomed brush did not commit one stroke");
            const auto &points = after.localMasks[0].strokes[0].points;
            require(points.size() >= 2 && std::abs(points.front().x - start.x()) < 1e-6 &&
                        std::abs(points.front().y - start.y()) < 1e-6 &&
                        std::abs(points.back().x - end.x()) < 1e-6 &&
                        std::abs(points.back().y - end.y()) < 1e-6,
                    "Zoomed brush stored a different source-space path");
        } else {
            const auto start = actualPoint("develop.targetedArea", {.53, .47});
            const auto end = actualPoint("develop.targetedArea", {.53, .43});
            const double delta = start.y() - end.y();
            const auto sampling = reference(c, decoded, before, 0, false);
            const auto color = sampling.pixelColor(
                std::min(int(start.x() * sampling.width()), sampling.width() - 1),
                std::min(int(start.y() * sampling.height()), sampling.height() - 1));
            d.drag("develop.targetedArea", {.53, .47}, {.53, .43});
            c.saved();
            waitTool();
            const auto after = c.editor.currentSettings();
            auto expected = before;
            if (hsl) {
                const auto weights = luma::hueWeights(
                    luma::toHSL({color.redF(), color.greenF(), color.blueF()}).hue);
                expected.hsl = after.hsl;
                for (std::size_t i = 0; i < weights.size(); ++i)
                    require(std::abs(after.hsl[i].saturation - before.hsl[i].saturation -
                                     delta * 100 * weights[i]) < .2,
                            "Zoomed HSL drag sampled the wrong source color or delta");
            } else {
                expected.curves[0] = after.curves[0];
                const auto &points = after.curves[0].points;
                const double tone =
                    .2126 * color.redF() + .7152 * color.greenF() + .0722 * color.blueF();
                require(points.size() == 3 && std::abs(points[1].x - tone) < .005 &&
                            std::abs(points[1].y - points[1].x - delta) < 1e-6,
                        "Zoomed tone drag sampled the wrong source tone or delta");
            }
            require(after != before && after == expected,
                    "Zoomed targeted drag changed another group");
        }
        require(std::abs(view->property("contentX").toDouble() - panX) < 1e-6 &&
                    std::abs(view->property("contentY").toDouble() - panY) < 1e-6,
                "Editing gesture was stolen by viewport panning");
        if (!guided) {
            const auto after = c.editor.currentSettings();
            require(after != before && c.editor.previewUrl() != previous,
                    "Tool gesture did not update preview");
            d.click("main.undoButton");
            c.saved();
            waitTool();
            require(c.editor.currentSettings() == before,
                    "Zoom tool gesture was not exactly one Undo");
            d.click("main.redoButton");
            c.saved();
            waitTool();
            require(c.editor.currentSettings() == after, "Zoom tool Redo lost edits");
            d.click("main.undoButton");
            c.saved();
            waitTool();
        }
        d.log({{"action", "zoom_tool_mapping"},
               {"scenario", id},
               {"scale", scale},
               {"hsl", hsl},
               {"pan_x", panX},
               {"pan_y", panY}});
        if (crop || guided) {
            const auto frame = c.editor.cropState();
            const auto guide = c.editor.transformGuides();
            const QPointF expected = crop ? QPointF(frame["x"].toDouble(), frame["y"].toDouble())
                                          : QPointF(guide.front().toMap()["x1"].toDouble(),
                                                    guide.front().toMap()["y1"].toDouble());
            const auto point =
                image->mapToScene({expected.x() * image->width(), expected.y() * image->height()}) *
                c.window->devicePixelRatio();
            const auto scene = c.window->grabWindow();
            const QColor color(crop ? "#dfae77" : "#53d6e4");
            bool found = false;
            for (int y = -3; y <= 3; ++y)
                for (int x = -3; x <= 3; ++x) {
                    const QPoint p(qRound(point.x()) + x, qRound(point.y()) + y);
                    if (!scene.rect().contains(p))
                        continue;
                    const auto pixel = scene.pixelColor(p);
                    found |= std::abs(pixel.red() - color.red()) <= 8 &&
                             std::abs(pixel.green() - color.green()) <= 8 &&
                             std::abs(pixel.blue() - color.blue()) <= 8;
                }
            require(found,
                    "Actual scene does not display the tool grip at its expected image coordinate");
        }
        d.screenshot(c.root + QString("/zoom-tool-%1.png").arg(pass));
        d.click("develop.fit");
        waitTool();
    }
    percent(c, "1600", 16);
    waitTool();
    auto *viewport = d.item("develop.viewport");
    for (const auto *name :
         {"CropOverlay.canvas", "TransformOverlay.canvas", "LocalOverlay.canvas"}) {
        const auto *canvas = d.item(QString::fromLatin1(name));
        require(canvas->width() <= viewport->width() + 1 &&
                    canvas->height() <= viewport->height() + 1,
                "1600% tool view creates an unbounded Canvas backing store");
    }
    d.screenshot(c.root + "/zoom-tool-1600.png");
    d.click("develop.fit");
    waitTool();
    require(sourceHash(source.path) == source.hash, "Zoom tool changed original bytes");
    return true;
}
} // namespace ux
