#include "apps/desktop/ThumbnailProvider.h"
#include "apps/shared/PhotoDecoder.h"
#include "apps/shared/PresetStore.h"
#include "core/catalog/Catalog.h"
#include "core/render/CpuRenderer.h"
#include "tests/ux/ThumbnailResponseTrace.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QBuffer>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaProperty>
#include <QPointer>
#include <QQuickItemGrabResult>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <sqlite3.h>
#include <tuple>
#include <vector>

#ifndef LUMA_UX_THUMBNAIL_TRACE
#error "LIB005 requires the real response observation seam"
#endif

// Actual away/save/back regression; the historical supplemental ID is retained.
namespace ux {
namespace {
namespace trace = thumbnail_trace;
constexpr auto candidateID = "CANDIDATE-LIB005-AWAY-BACK";
constexpr auto officialID = "UX-LIB-005";
constexpr int fixtureCount = 32;
using Kind = trace::Kind;
struct Pin {
    luma::AssetSnapshot saved;
    luma::LibraryItem row;
    QImage jpegPixels;
};
using Pins = std::vector<Pin>;
QImage pixels(QImage image) {
    image = image.convertToFormat(QImage::Format_RGBA8888);
    image.setDevicePixelRatio(1);
    image.setColorSpace({});
    return image;
}
QByteArray digest(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot hash owned LIB005 file");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file), "Cannot read complete owned LIB005 file");
    return hash.result();
}
QString id(const Pin &pin) {
    return QString::fromStdString(pin.saved.asset.id);
}
QString path(const Pin &pin) {
    return QString::fromStdString(pin.saved.asset.path.string());
}
QJsonObject pinJSON(const Pin &pin) {
    const auto &asset = pin.saved.asset;
    const auto encoded = QJsonDocument::fromJson(
        luma::encodePreset({"LIB005 pinned settings", {}, asset.edits, luma::allSettingsGroups}));
    require(encoded.isObject(), "Cannot encode LIB005 preset-schema diagnostic settings");
    const auto &m = asset.metadata;
    QJsonObject metadata{{"camera", QString::fromStdString(m.camera)},
                         {"lens", QString::fromStdString(m.lens)},
                         {"width", m.width},
                         {"height", m.height},
                         {"orientation", m.orientation}};
    metadata["iso"] = m.iso ? QJsonValue(*m.iso) : QJsonValue(QJsonValue::Null);
    metadata["shutter_seconds"] =
        m.shutterSeconds ? QJsonValue(*m.shutterSeconds) : QJsonValue(QJsonValue::Null);
    metadata["aperture"] = m.aperture ? QJsonValue(*m.aperture) : QJsonValue(QJsonValue::Null);
    metadata["focal_length"] =
        m.focalLength ? QJsonValue(*m.focalLength) : QJsonValue(QJsonValue::Null);
    metadata["capture_timestamp"] = m.captureTimestamp
                                        ? QJsonValue(QString::number(*m.captureTimestamp))
                                        : QJsonValue(QJsonValue::Null);
    return {{"uuid", id(pin)},
            {"source", path(pin)},
            {"source_sha256", QString::fromStdString(asset.checksum)},
            {"durable_revision", QString::number(pin.saved.revision)},
            {"metadata", metadata},
            {"preset_schema_settings", encoded.object()},
            {"local_masks_serialized",
             QString::fromStdString(luma::encodeLocalMasks(asset.edits.localMasks))},
            {"library_row", QJsonObject{{"filename", QString::fromStdString(pin.row.filename)},
                                        {"width", pin.row.width},
                                        {"height", pin.row.height},
                                        {"orientation", pin.row.orientation},
                                        {"rating", pin.row.rating},
                                        {"flag", pin.row.flag},
                                        {"label", QString::fromStdString(pin.row.label)},
                                        {"camera", QString::fromStdString(pin.row.camera)}}}};
}
QUrl url(const Pin &pin) {
    // Fresh empty-filter import has no editor/save/thumbnail invalidation.
    // This prediction only arms a future exact request; actual model/URL/trace
    // equality below is mandatory and no QML source is assigned by the fixture.
    return QUrl("image://thumbnail/" + id(pin) + "?generation=0");
}
QImage expectedJPEG(const luma::AssetSnapshot &saved) {
    const auto decoded = luma::PhotoDecoder().decode(saved.asset.path);
    require(decoded.image.width == 256 && decoded.image.height == 192 &&
                decoded.image.alpha.empty(),
            "LIB005 requires small opaque generated PNGs");
    const auto small = luma::standardPreview(decoded.image, 384);
    const double scale = std::min(static_cast<double>(small.width) / decoded.image.width,
                                  static_cast<double>(small.height) / decoded.image.height);
    const auto developed = luma::renderCPU(
        small, saved.asset.edits, saved.asset.metadata.orientation, {}, {}, decoded.calibration,
        luma::DetailDiagnostic::none, scale, saved.asset.metadata);
    QImage expected(developed.rgba.data(), developed.width, developed.height,
                    static_cast<qsizetype>(developed.width) * 4, QImage::Format_RGBA8888);
    auto detached = expected.copy();
    detached.setColorSpace(QColorSpace::SRgb);
    QByteArray encoded;
    QBuffer buffer(&encoded);
    require(buffer.open(QIODevice::WriteOnly) && detached.save(&buffer, "JPEG", 85),
            "Cannot construct independent saved-document thumbnail reference");
    const auto result = QImage::fromData(encoded, "JPEG");
    require(!result.isNull(), "Independent JPEG reference did not decode");
    return pixels(result);
}
QString cachePath(Context &c, const Pin &pin) {
    const auto &saved = pin.saved;
    const auto &asset = saved.asset;
    const auto encoded =
        luma::encodePreset({"Thumbnail", {}, asset.edits, luma::allSettingsGroups});
    const auto key = QByteArray::fromStdString(asset.checksum) + "|" +
                     QByteArray::number(saved.revision) + "|" + encoded + "|local-v2|" +
                     QByteArray::fromStdString(luma::encodeLocalMasks(asset.edits.localMasks)) +
                     "|retouch-v1|" +
                     QByteArray::fromStdString(luma::encodeRetouch(asset.edits.retouch));
    return c.catalogFile + ".luma-cache/thumbnails/" +
           QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex()) +
           "-edited-v4.jpg";
}
const char *kindName(Kind kind) {
    switch (kind) {
    case Kind::Created:
        return "Created";
    case Kind::SnapshotRead:
        return "SnapshotRead";
    case Kind::WorkerResultReady:
        return "WorkerResultReady";
    case Kind::GateEntered:
        return "GateEntered";
    case Kind::GateReleased:
        return "GateReleased";
    case Kind::GateTimedOut:
        return "GateTimedOut";
    case Kind::GateSkipped:
        return "GateSkipped";
    case Kind::Cancelled:
        return "Cancelled";
    case Kind::FinishedSignalObserved:
        return "FinishedSignalObserved";
    case Kind::TextureFactoryCalled:
        return "TextureFactoryCalled";
    case Kind::Destroyed:
        return "Destroyed";
    }
    return "Unknown";
}
QJsonObject traceJSON(const trace::Snapshot &value) {
    QJsonArray requests, events;
    for (const auto &request : value.requests)
        requests.append(QJsonObject{{"serial", QString::number(request.serial)},
                                    {"provider_request_id", request.providerRequestID},
                                    {"full_url", request.fullURL.toString(QUrl::FullyEncoded)},
                                    {"requested_width", request.requestedSize.width()},
                                    {"requested_height", request.requestedSize.height()}});
    for (const auto &event : value.events)
        events.append(QJsonObject{{"sequence", QString::number(event.sequence)},
                                  {"serial", QString::number(event.serial)},
                                  {"kind", kindName(event.kind)},
                                  {"revision", QString::number(event.revision)},
                                  {"thread_id", QString::number(event.threadID, 16)},
                                  {"successful_pixels", event.successfulPixels}});
    return {{"diagnostic_failure", value.diagnosticFailure},
            {"gate_entered", value.gateEntered},
            {"gate_timed_out", value.gateTimedOut},
            {"release_requested", value.releaseRequested},
            {"session_closing", value.sessionClosing},
            {"gated_serial", QString::number(value.gatedSerial)},
            {"requests", requests},
            {"events", events}};
}
int count(const trace::Snapshot &value, std::uint64_t serial, Kind kind) {
    return static_cast<int>(
        std::count_if(value.events.begin(), value.events.end(), [=](const auto &event) {
            return event.serial == serial && event.kind == kind;
        }));
}
const trace::Event &one(const trace::Snapshot &value, std::uint64_t serial, Kind kind) {
    require(count(value, serial, kind) == 1,
            QString("Expected exactly one actual response event: ") + kindName(kind));
    return *std::find_if(value.events.begin(), value.events.end(), [=](const auto &event) {
        return event.serial == serial && event.kind == kind;
    });
}
std::uint64_t lastSequence(const trace::Snapshot &value) {
    return value.events.empty() ? 0 : value.events.back().sequence;
}
std::vector<trace::Request> requestsFor(const trace::Snapshot &value, const QUrl &source) {
    std::vector<trace::Request> result;
    for (const auto &request : value.requests)
        if (request.fullURL == source)
            result.push_back(request);
    return result;
}
trace::Snapshot held(trace::Session &session, const Pin &old, const QSize &expectedRequestSize,
                     bool uncancelled, bool unique = false) {
    const auto value = session.snapshot();
    require(!value.diagnosticFailure && value.gateEntered && !value.gateTimedOut &&
                !value.releaseRequested && !value.sessionClosing && value.gatedSerial != 0,
            "Actual OLD pending-completion gate is not currently held");
    const auto requests = requestsFor(value, url(old));
    // Before leaving CURRENT OLD, the exact tuple must be unique. After actual away/back,
    // Qt may legitimately create another consumer for that same request URL. Only the
    // original held response has the already-pinned revision0 result. Its ownership remains
    // exact by serial; no per-card response ownership is invented.
    const auto gated = std::find_if(requests.begin(), requests.end(), [&](const auto &request) {
        return request.serial == value.gatedSerial;
    });
    require(gated != requests.end() && (!unique || requests.size() == 1) &&
                gated->requestedSize == expectedRequestSize &&
                gated->providerRequestID == id(old) + "?generation=0",
            "Exact gated OLD full source/size/serial is absent or initial tuple is ambiguous");
    const auto serial = value.gatedSerial;
    const auto &created = one(value, serial, Kind::Created);
    const auto &read = one(value, serial, Kind::SnapshotRead);
    const auto &result = one(value, serial, Kind::WorkerResultReady);
    const auto &gate = one(value, serial, Kind::GateEntered);
    require(read.revision == static_cast<std::uint64_t>(old.saved.revision) &&
                result.successfulPixels && created.sequence < read.sequence &&
                read.sequence < result.sequence && result.sequence < gate.sequence &&
                count(value, serial, Kind::GateReleased) == 0 &&
                count(value, serial, Kind::GateSkipped) == 0 &&
                count(value, serial, Kind::GateTimedOut) == 0 &&
                count(value, serial, Kind::FinishedSignalObserved) == 0 &&
                count(value, serial, Kind::TextureFactoryCalled) == 0 &&
                count(value, serial, Kind::Destroyed) == 0 &&
                count(value, serial, Kind::Cancelled) <= 1 &&
                (!uncancelled || count(value, serial, Kind::Cancelled) == 0),
            "OLD is not the successful current-revision request pending its real finished signal");
    return value;
}
QQuickItem *imageFor(QQuickItem *card) {
    return card ? card->findChild<QQuickItem *>("photoThumbnail") : nullptr;
}
QUrl generation(const Pin &pin, int value) {
    return QUrl("image://thumbnail/" + id(pin) + "?generation=" + QString::number(value));
}
QQuickItem *gridCard(Context &c, const Pin &pin) {
    return qobject_cast<QQuickItem *>(c.driver->find("library.card." + id(pin)));
}
void identity(Context &c, QQuickItem *card, const Pin &pin, const QUrl &source, int status,
              bool active, bool currentModelURL = true) {
    auto *view = c.driver->item("library.grid");
    auto *image = imageFor(card);
    const int row = card ? card->property("index").toInt() : -1;
    require(
        card && image && card->isVisible() && image->isVisible() && view->isVisible() &&
            card->mapRectToItem(view, card->boundingRect()).intersects(view->boundingRect()) &&
            image->width() > 0 && image->height() > 0 &&
            image->property("sourceSize").toSize() == QSize(384, 384) && row >= 0 &&
            row < c.library.count() && card->property("assetID").toString() == id(pin) &&
            c.library.data(c.library.index(row), LibraryModel::AssetID).toString() == id(pin) &&
            c.library.data(c.library.index(row), LibraryModel::Path).toUrl().toLocalFile() ==
                path(pin) &&
            (!currentModelURL ||
             c.library.data(c.library.index(row), LibraryModel::Thumbnail).toUrl() == source) &&
            card->property("thumbnailURL").toUrl() == source &&
            image->property("source").toUrl() == source &&
            image->property("status").toInt() == status &&
            card->property("retryCount").toInt() == 0 && !card->property("selected").toBool() &&
            card->property("activePhoto").toBool() == active &&
            c.library.data(c.library.index(row), LibraryModel::Active).toBool() == active &&
            !c.library.data(c.library.index(row), LibraryModel::Selected).toBool() &&
            card->property("filename").toString().toStdString() == pin.row.filename &&
            card->property("rating").toInt() == pin.row.rating &&
            card->property("flagValue").toInt() == pin.row.flag &&
            card->property("colorLabel").toString().toStdString() == pin.row.label &&
            c.library.data(c.library.index(row), LibraryModel::Filename).toString().toStdString() ==
                pin.row.filename &&
            c.library.data(c.library.index(row), LibraryModel::Rating).toInt() == pin.row.rating &&
            c.library.data(c.library.index(row), LibraryModel::Flag).toInt() == pin.row.flag &&
            c.library.data(c.library.index(row), LibraryModel::Label).toString().toStdString() ==
                pin.row.label &&
            c.library.data(c.library.index(row), LibraryModel::Camera).toString().toStdString() ==
                pin.row.camera,
        "LIB005 current visible Library grid UUID/row/path/source/Ready/retry/metadata ownership "
        "is "
        "invalid");
}
double meanRGB(const QImage &a, const QImage &b, int border = 4) {
    require(a.size() == b.size() && a.width() > 2 * border && a.height() > 2 * border,
            "Non-comparable LIB005 actual pixel region");
    double total = 0;
    std::uint64_t samples = 0;
    for (int y = border; y < a.height() - border; ++y)
        for (int x = border; x < a.width() - border; ++x) {
            const auto first = a.pixelColor(x, y), second = b.pixelColor(x, y);
            total += std::abs(first.red() - second.red()) +
                     std::abs(first.green() - second.green()) +
                     std::abs(first.blue() - second.blue());
            samples += 3;
        }
    return total / static_cast<double>(samples);
}
struct Reference {
    QString label;
    QImage jpeg;
};
QJsonObject cardPixels(Context &c, QQuickItem *card, const Pin &pin, const QUrl &source,
                       bool active, bool currentModelURL, const QString &ownLabel,
                       const std::vector<Reference> &references,
                       const std::function<void()> &checkpoint = {}) {
    identity(c, card, pin, source, 1, active, currentModelURL);
    QPointer<QQuickItem> cardGuard(card), imageGuard(imageFor(card));
    const auto geometry = imageGuard->size();
    const auto cached = QFileInfo(cachePath(c, pin));
    require(cached.isFile() && !cached.isSymLink() &&
                pixels(QImage(cached.filePath())) == pin.jpegPixels,
            "LIB005 actual saved cache differs from independent original/expected-document JPEG");
    const auto capture =
        readyItemGrab(c, *imageGuard, "LIB005 actual thumbnail Image-node readback");
    require(cardGuard && imageGuard && imageFor(cardGuard.data()) == imageGuard.data() &&
                imageGuard->size() == geometry,
            "LIB005 current Image rebound during actual readback");
    identity(c, cardGuard.data(), pin, source, 1, active, currentModelURL);
    if (checkpoint)
        checkpoint();
    const auto actual = pixels(capture->image());
    const auto own =
        pixels(pin.jpegPixels.scaled(actual.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    const auto cropped =
        actual.copy(QRect((actual.width() - own.width()) / 2, (actual.height() - own.height()) / 2,
                          own.width(), own.height()));
    const double ownError = meanRGB(cropped, own);
    double closestWrong = std::numeric_limits<double>::infinity();
    QString closestLabel;
    QJsonArray negatives;
    for (const auto &reference : references) {
        if (reference.label == ownLabel)
            continue;
        const auto wrong = pixels(
            reference.jpeg.scaled(actual.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        require(wrong.size() == own.size(), "LIB005 reference aspect ratios differ");
        const double separation = meanRGB(own, wrong), mismatch = meanRGB(cropped, wrong);
        require(separation > 12,
                "LIB005 OLD/NEW/control references indistinguishable at actual capture size");
        negatives.append(QJsonObject{{"label", reference.label},
                                     {"reference_separation", separation},
                                     {"actual_error", mismatch}});
        if (mismatch < closestWrong) {
            closestWrong = mismatch;
            closestLabel = reference.label;
        }
    }
    require(
        ownError <= 4 && closestWrong > ownError + 8,
        "LIB005 current thumbnail shows a stale revision/control or misses its expected pixels");
    const QJsonObject result{{"uuid", id(pin)},
                             {"source", source.toString(QUrl::FullyEncoded)},
                             {"expected_label", ownLabel},
                             {"mean_rgb_own_error", ownError},
                             {"closest_wrong_label", closestLabel},
                             {"mean_rgb_closest_wrong", closestWrong},
                             {"negative_references", negatives}};
    auto action = result;
    action.insert("action", "lib005_independent_thumbnail_pixels");
    c.driver->log(action);
    return result;
}
QJsonArray readRows(Context &c, const char *query) {
    sqlite3 *opened = nullptr;
    const int result =
        sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &opened, SQLITE_OPEN_READONLY, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(opened, sqlite3_close);
    require(result == SQLITE_OK && db, "Cannot open LIB005 read-only journal observation");
    sqlite3_busy_timeout(db.get(), 1000);
    sqlite3_stmt *prepared = nullptr;
    const int preparedResult = sqlite3_prepare_v2(db.get(), query, -1, &prepared, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(prepared,
                                                                               sqlite3_finalize);
    require(preparedResult == SQLITE_OK && statement,
            "Cannot prepare LIB005 read-only journal observation");
    QJsonArray rows;
    int step = SQLITE_OK;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW) {
        QJsonArray row;
        for (int column = 0; column < sqlite3_column_count(statement.get()); ++column) {
            if (sqlite3_column_type(statement.get(), column) == SQLITE_NULL)
                row.append(QJsonValue(QJsonValue::Null));
            else
                row.append(QString::fromUtf8(
                    reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), column))));
        }
        rows.append(row);
    }
    require(step == SQLITE_DONE, "Cannot complete LIB005 read-only journal observation");
    return rows;
}
QJsonObject journal(Context &c) {
    return {
        {"imports",
         readRows(
             c, "SELECT id,mode,destination,state,owner_pid,skip_duplicates,created_at,finished_at "
                "FROM import_sessions ORDER BY id")},
        {"items",
         readRows(c, "SELECT "
                     "session_id,position,source_path,target_path,temporary_path,checksum,filesize,"
                     "state,asset_id,error FROM import_items ORDER BY session_id,position")},
        {"session", readRows(c, "SELECT slot,asset_id FROM session ORDER BY slot")},
        {"collections", readRows(c, "SELECT id,name FROM collections ORDER BY id")},
        {"collection_assets", readRows(c, "SELECT collection_id,asset_id FROM collection_assets "
                                          "ORDER BY collection_id,asset_id")}};
}
void durable(Context &c, const Pins &baseline, const Pin &expectedA,
             const QJsonObject &journalBefore) {
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto rows = catalog.libraryPage({}, 0, fixtureCount);
    require(baseline.size() == static_cast<std::size_t>(fixtureCount) &&
                rows.size() == static_cast<std::size_t>(fixtureCount) &&
                catalog.libraryCount() == fixtureCount,
            "LIB005 changed exact bounded source membership");
    const auto metadata = [](const luma::PhotoMetadata &m) {
        return std::tie(m.camera, m.lens, m.iso, m.shutterSeconds, m.aperture, m.focalLength,
                        m.captureTimestamp, m.width, m.height, m.orientation);
    };
    const auto rowFields = [](const luma::LibraryItem &r) {
        return std::tie(r.id, r.path, r.filename, r.checksum, r.width, r.height, r.orientation,
                        r.rating, r.flag, r.label, r.camera);
    };
    for (std::size_t index = 0; index < baseline.size(); ++index) {
        const auto &pin = index == 0 ? expectedA : baseline[index];
        const auto saved = catalog.assetSnapshot(pin.saved.asset.id);
        require(saved && rowFields(rows[index]) == rowFields(baseline[index].row) &&
                    saved->asset.id == pin.saved.asset.id &&
                    saved->asset.path == pin.saved.asset.path &&
                    saved->asset.checksum == pin.saved.asset.checksum &&
                    saved->revision == pin.saved.revision &&
                    saved->asset.edits == pin.saved.asset.edits &&
                    metadata(saved->asset.metadata) == metadata(pin.saved.asset.metadata) &&
                    digest(path(pin)).toHex().toStdString() == pin.saved.asset.checksum,
                "LIB005 full typed document/revision/row/metadata/original identity mismatch");
    }
    const auto active = catalog.activeAsset();
    require(active && active->id == expectedA.saved.asset.id &&
                active->path == expectedA.saved.asset.path &&
                active->edits == expectedA.saved.asset.edits && journal(c) == journalBefore,
            "LIB005 changed selected durable session or completed Import journal");
    catalog.verify();
}
void editorState(Context &c, const Pin &a, bool undo, bool redo, bool libraryMode) {
    require(
        c.editor.hasPhoto() && c.editor.photoID() == id(a) && c.editor.sourcePath() == path(a) &&
            c.editor.currentSettings() == a.saved.asset.edits && c.editor.canUndo() == undo &&
            c.editor.canRedo() == redo && !c.editor.saving() && !c.editor.busy() &&
            !c.editor.transformAutoBusy() && !c.editor.localGestureActive() &&
            !c.editor.localSampling() && !c.editor.localMode() && !c.editor.cropMode() &&
            !c.editor.guidedMode() && !c.editor.exporting() && !c.batch.working() &&
            !c.exporter.working() && !c.editor.presetPreview() && !c.editor.original() &&
            c.editor.error().isEmpty() && !c.library.working() && !c.library.loading() &&
            !c.library.selecting() && c.library.error().isEmpty() &&
            c.library.selectedCount() == 0 && c.library.selectionIDs().empty() &&
            c.library.count() == fixtureCount && c.window->isActive() && c.window->isExposed() &&
            c.window->property("libraryMode").toBool() == libraryMode &&
            c.driver->object("library.search")->property("text").toString().isEmpty() &&
            c.driver->object("library.ratingFilter")->property("currentIndex").toInt() == 0 &&
            c.driver->object("libraryFlagFilter")->property("currentIndex").toInt() == 0 &&
            c.driver->object("libraryLabelFilter")->property("currentIndex").toInt() == 0 &&
            c.driver->object("library.pane")->property("currentCollection").toString().isEmpty(),
        "LIB005 editor/history/selection/filter/job state differs from exact expected state");
}
void rendered(Context &c, const Pin &a, const QImage &reference) {
    const auto ready = [&] {
        const auto *image = c.driver->item("develop.preview");
        return !c.editor.busy() && c.editor.hasPhoto() && image->isVisible() &&
               image->property("status").toInt() == 1 && !c.editor.previewUrl().isEmpty() &&
               image->property("source").toUrl().toString() == c.editor.previewUrl() &&
               c.provider->requestImage({}, nullptr, {}) == reference;
    };
    c.driver->wait(ready,
                   "LIB005 actual main Image Ready/current source/expected full-document pixels",
                   10000);
    c.driver->frame();
    require(ready() && c.editor.photoID() == id(a) &&
                c.editor.currentSettings() == a.saved.asset.edits,
            "LIB005 current preview/document did not remain correct through polish");
}
void naturalSave(Context &c, const Pin &a) {
    // No Context::saved/endEdit/flush/retry: genuine input must close and save itself.
    c.driver->wait(
        [&] {
            const auto saved =
                luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(a.saved.asset.id);
            return !c.editor.saving() && !c.editor.busy() && saved &&
                   saved->asset.edits == a.saved.asset.edits && saved->revision == a.saved.revision;
        },
        "LIB005 natural exact full-document/revision autosave", 10000);
    require(c.editor.error().isEmpty(), c.editor.error());
}
Pins importHidden(Context &c) {
    auto &d = *c.driver;
    require(!c.editor.hasPhoto() && c.library.count() == 0 &&
                luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 0,
            "LIB005 needs a fresh isolated empty catalog");
    QSignalSpy searchResets(&c.library, &QAbstractItemModel::modelReset);
    require(searchResets.isValid(), "Cannot observe actual no-match filter reload");
    d.type("library.search", "lib005-no-such-filter-8ff23a");
    d.wait(
        [&] { return searchResets.count() >= 2 && !c.library.loading() && c.library.count() == 0; },
        "actual no-match search fully reloaded");
    const auto directory = c.root + "/owned-lib005-originals";
    require(QDir().mkpath(directory), "Cannot create own LIB005 source directory");
    const auto names = QDir(c.fixture).entryList({"Photo-*.png"}, QDir::Files, QDir::Name);
    require(names.size() == fixtureCount,
            "LIB005 requires exactly32 private generated RGB PNG fixtures");
    QList<QUrl> sources;
    for (const auto &name : names) {
        const auto target = directory + '/' + name;
        require(QFile::copy(c.fixture + '/' + name, target) &&
                    digest(target) == digest(c.fixture + '/' + name),
                "Cannot copy identical private LIB005 source");
        sources.append(QUrl::fromLocalFile(target));
    }
    require(d.object("library.importDialog")->setProperty("sources", QVariant::fromValue(sources)),
            "Cannot provide declared Import file-chooser-result boundary");
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "actual Import dialog opened");
    d.choose("library.importMode", 0);
    if (d.object("library.importDuplicates")->property("checked").toBool())
        d.click("library.importDuplicates");
    d.click("library.importStart");
    d.wait(
        [&] {
            return !c.library.importing() && !c.library.working() && !c.library.loading() &&
                   c.library.count() == 0;
        },
        "actual bounded Add behind no-match filter", 30000);
    d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
           "Import dialog closed");
    require(c.library.error().isEmpty(), c.library.error());
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto rows = catalog.libraryPage({}, 0, fixtureCount);
    require(rows.size() == static_cast<std::size_t>(fixtureCount) &&
                catalog.libraryCount() == fixtureCount,
            "LIB005 Add did not commit exact bounded membership");
    Pins pins;
    for (const auto &row : rows) {
        const auto saved = catalog.assetSnapshot(row.id);
        require(
            saved && saved->asset.edits == luma::EditDocument{} && saved->revision == 0 &&
                saved->asset.path == row.path && row.rating == 0 && row.flag == 0 &&
                row.label.empty() && row.width == 256 && row.height == 192 &&
                row.orientation == 0 && saved->asset.metadata.width == 256 &&
                saved->asset.metadata.height == 192 && saved->asset.metadata.orientation == 0 &&
                row.camera == saved->asset.metadata.camera &&
                row.checksum == saved->asset.checksum &&
                row.filename ==
                    QFileInfo(QString::fromStdString(row.path.string())).fileName().toStdString() &&
                QString::fromStdString(row.path.string()).startsWith(directory + '/') &&
                digest(QString::fromStdString(row.path.string())).toHex().toStdString() ==
                    row.checksum,
            "LIB005 imported baseline is not independently neutral/pinned");
        pins.push_back({*saved, row, expectedJPEG(*saved)});
        d.frame();
    }
    for (const auto &pin : pins)
        require(!d.find("library.card." + id(pin)) && !d.find("filmstrip.card." + id(pin)),
                "LIB005 pre-Session cards exist despite no-match search");
    return pins;
}
void openHidden(Context &c, const Pin &a) {
    auto &d = *c.driver;
    d.click("openPhotoButton");
    auto *dialog = d.object("main.openDialog");
    d.wait([&] { return dialog->property("visible").toBool(); },
           "actual Open button opened FileDialog");
    require(dialog->setProperty("selectedFile", QUrl::fromLocalFile(path(a))) &&
                dialog->property("selectedFile").toUrl().toLocalFile() == path(a),
            "LIB005 actual FileDialog rejected declared selected-file result");
    d.log({{"action", "file_dialog_selection_fixture_boundary"},
           {"id", "main.openDialog"},
           {"path", path(a)},
           {"native_selection_exercised", false},
           {"all_cards_hidden", true}});
    require(QMetaObject::invokeMethod(dialog, "close", Qt::DirectConnection) &&
                QMetaObject::invokeMethod(dialog, "accepted", Qt::DirectConnection),
            "LIB005 FileDialog production close/accepted handler unavailable");
    // The declared file-selection seam does not exercise the platform's focus return.
    // Restore the test window during setup, before any response gate/observers exist.
    c.window->requestActivate();
    d.log({{"action", "file_dialog_fixture_return_activation"},
           {"native_focus_return_exercised", false}});
    d.wait(
        [&] {
            return !dialog->property("visible").toBool() && c.editor.hasPhoto() &&
                   c.editor.photoID() == id(a) && !c.editor.busy() && !c.editor.saving() &&
                   !c.library.loading() && !c.library.working() && c.library.count() == 0 &&
                   c.window->isActive() && c.window->isExposed() &&
                   QGuiApplication::focusWindow() == c.window;
        },
        "A active/activation reset settled under actual no-match filter", 10000);
    require(c.editor.currentSettings() == a.saved.asset.edits && !c.editor.canUndo() &&
                !c.editor.canRedo() && c.editor.error().isEmpty() && !c.library.selecting() &&
                c.library.selectedCount() == 0,
            "LIB005 Open setup changed neutral document/history/selection");
}
// Passive GUI signal observation only. No exception may escape a Qt callback.
class InvalidationAudit final {
  public:
    InvalidationAudit(Context &context, trace::Session &observed) : c(context), session(observed) {
        connection = QObject::connect(
            &c.library, &QAbstractItemModel::dataChanged, &lifetime,
            [this](const QModelIndex &first, const QModelIndex &last,
                   const QList<int> &roles) noexcept {
                if (!roles.contains(LibraryModel::Thumbnail))
                    return;
                try {
                    ++notifications;
                    QJsonArray currentRows;
                    for (int row = first.row(); row <= last.row(); ++row) {
                        const auto asset =
                            c.library.data(c.library.index(row), LibraryModel::AssetID).toString();
                        assets.append(asset);
                        currentRows.append(QJsonObject{
                            {"uuid", asset},
                            {"model_source",
                             c.library.data(c.library.index(row), LibraryModel::Thumbnail)
                                 .toUrl()
                                 .toString(QUrl::FullyEncoded)}});
                    }
                    exactRoles = exactRoles && roles == QList<int>{LibraryModel::Thumbnail};
                    const auto observed = session.snapshot();
                    if (!firstObservation)
                        firstObservation = observed;
                    observations.append(QJsonObject{{"first_row", first.row()},
                                                    {"last_row", last.row()},
                                                    {"rows", currentRows},
                                                    {"trace", traceJSON(observed)}});
                } catch (...) {
                    failed = true;
                }
            });
        require(static_cast<bool>(connection),
                "Cannot observe actual targeted Thumbnail invalidation");
    }
    ~InvalidationAudit() {
        QObject::disconnect(connection);
    }
    void requireSingle(const QString &a) const {
        requireCount(a, 1);
        require(firstObservation && firstObservation->gateEntered &&
                    !firstObservation->releaseRequested && !firstObservation->sessionClosing &&
                    !firstObservation->gateTimedOut && !firstObservation->diagnosticFailure &&
                    firstObservation->gatedSerial != 0 &&
                    count(*firstObservation, firstObservation->gatedSerial,
                          Kind::FinishedSignalObserved) == 0 &&
                    count(*firstObservation, firstObservation->gatedSerial, Kind::Destroyed) == 0,
                "LIB005 actual first Thumbnail invalidation did not overlap the unfinished held "
                "OLD request");
    }
    void requireCount(const QString &a, int expected) const {
        QStringList ids;
        for (int index = 0; index < expected; ++index)
            ids.append(a);
        require(
            !failed && exactRoles && notifications == expected && assets == ids,
            "LIB005 save did not cause the exact genuine targeted Thumbnail invalidations for A");
    }
    QJsonObject json() const {
        return {{"notifications", notifications},
                {"assets", QJsonArray::fromStringList(assets)},
                {"exact_thumbnail_roles", exactRoles},
                {"observation_failed", failed},
                {"observations", observations}};
    }

  private:
    Context &c;
    trace::Session &session;
    QObject lifetime;
    QMetaObject::Connection connection;
    int notifications = 0;
    bool exactRoles = true, failed = false;
    QStringList assets;
    QJsonArray observations;
    std::optional<trace::Snapshot> firstObservation;
};
struct ReleasePointer {
    Context &c;
    QPoint last;
    bool down = false;
    ~ReleasePointer() noexcept {
        if (down)
            try {
                QTest::mouseRelease(c.window, Qt::LeftButton, Qt::NoModifier, last);
            } catch (...) {
                std::fputs("LIB005 pointer cleanup failed\n", stderr);
            }
    }
};
std::vector<QPointer<QQuickItem>> visibleCards(Context &c) {
    auto *grid = c.driver->item("library.grid");
    std::vector<QPointer<QQuickItem>> result;
    std::function<void(QQuickItem *)> visit = [&](QQuickItem *parent) {
        for (auto *item : parent->childItems()) {
            if (item->objectName().startsWith("library.card.")) {
                if (item->isVisible() && item->mapRectToItem(grid, item->boundingRect())
                                             .intersects(grid->boundingRect()))
                    result.push_back(item);
            } else
                visit(item);
        }
    };
    visit(grid);
    return result;
}
bool offscreen(Context &c, const QPointer<QQuickItem> &card) {
    auto *grid = c.driver->item("library.grid");
    return !card || !card->isVisible() ||
           !card->mapRectToItem(grid, card->boundingRect()).intersects(grid->boundingRect());
}
QJsonObject plan(Context &c, int count) {
    auto *grid = c.driver->item("library.grid");
    const double width = grid->width(), height = grid->height();
    const double cellWidth = grid->property("cellWidth").toDouble();
    const double cellHeight = grid->property("cellHeight").toDouble();
    const double cache = grid->property("cacheBuffer").toDouble();
    require(width > 0 && height > 0 && cellWidth > 0 && cellHeight > 12 && cache >= 0,
            "Actual GridView has no bounded fixture geometry");
    const int columns = std::max(1, static_cast<int>(std::floor(width / cellWidth + 1e-6)));
    int selected = 0;
    for (int candidate : {8, 12, 32}) {
        const int rows = (candidate + columns - 1) / columns;
        if (rows * cellHeight - height > cache + cellHeight &&
            (rows - 1) * cellHeight > height + cache) {
            selected = candidate;
            break;
        }
    }
    const QJsonObject result{{"grid_width", width},
                             {"grid_height", height},
                             {"cell_width", cellWidth},
                             {"cell_height", cellHeight},
                             {"cache_buffer", cache},
                             {"columns", columns},
                             {"minimal_fixture_count", selected},
                             {"provided_count", count}};
    writeJson(c.root + "/lib005-fixture-plan.json", result);
    require(selected != 0 && selected == count,
            "Fixed32 profile differs from the actual-grid minimum8/12/32 plan; fixture boundary "
            "not reached, no bulk fallback or hidden count substitution");
    return result;
}
} // namespace

bool lib005ActualResponseCandidate(Context &c, const QString &identifier) {
    if (identifier != candidateID && identifier != officialID)
        return false;
    auto &d = *c.driver;
    const auto geometryPlan = plan(c, fixtureCount);
    QSignalSpy saves(&c.editor, &EditorController::editsSaved);
    require(saves.isValid(), "Cannot observe natural LIB005 saves from fresh setup");
    const auto pins = importHidden(c);
    const auto &old = pins.front(), &tail = pins.back();
    openHidden(c, old); // All activation/reload and chooser-result setup precedes Session.
    const auto oldPreview = fixtureReference(c, old.saved.asset.edits, path(old));
    rendered(c, old, oldPreview);
    const auto journalBefore = journal(c);
    QJsonArray expectedSession;
    expectedSession.append(QJsonValue(QJsonArray{QStringLiteral("1"), id(old)}));
    writeJson(c.root + "/lib005-pre-session-journal.json", {{"actual_journal", journalBefore},
                                                            {"expected_session", expectedSession},
                                                            {"expected_import_items", fixtureCount},
                                                            {"expected_active_uuid", id(old)}});
    require(journalBefore["imports"].toArray().size() == 1 &&
                journalBefore["items"].toArray().size() == fixtureCount &&
                journalBefore["session"].toArray() == expectedSession &&
                journalBefore["collections"].toArray().isEmpty() &&
                journalBefore["collection_assets"].toArray().isEmpty(),
            "LIB005 initial durable session/import/collection cardinality is invalid");
    const auto imported = journalBefore["imports"].toArray().at(0).toArray();
    require(imported.size() == 8 && imported.at(1).toString() == "add" &&
                imported.at(2).toString().isEmpty() && imported.at(3).toString() == "completed" &&
                imported.at(4).toString().toLongLong() > 0 && imported.at(5).toString() == "0",
            "LIB005 actual Add journal is not one completed import with duplicates disabled");
    for (int index = 0; index < fixtureCount; ++index) {
        const auto item = journalBefore["items"].toArray().at(index).toArray();
        const auto &pin = pins[static_cast<std::size_t>(index)];
        require(item.size() == 10 && item.at(0) == imported.at(0) &&
                    item.at(1).toString() == QString::number(index) &&
                    item.at(2).toString() == path(pin) && item.at(3).toString() == path(pin) &&
                    item.at(4).toString().isEmpty() &&
                    item.at(5).toString().toStdString() == pin.saved.asset.checksum &&
                    item.at(6).toString().toLongLong() == QFileInfo(path(pin)).size() &&
                    item.at(7).toString() == "committed" && item.at(8).toString() == id(pin) &&
                    item.at(9).toString().isEmpty(),
                "LIB005 completed import item differs from its private original/UUID/hash");
    }
    require(saves.count() == 0 && c.library.count() == 0 && !c.library.loading(),
            "Hidden setup emitted a save/invalidation or left Library metadata pending");
    for (const auto &pin : pins)
        require(!d.find("library.card." + id(pin)) && !d.find("filmstrip.card." + id(pin)) &&
                    !QFileInfo::exists(cachePath(c, pin)),
                "Hidden setup warmed a real thumbnail delegate/cache before Session");
    durable(c, pins, old, journalBefore);

    Pin edited = old;
    edited.saved.asset.edits.exposure = 5.0; // Independent literal endpoint, BEFORE input.
    edited.saved.revision = old.saved.revision + 1;
    edited.jpegPixels = expectedJPEG(edited.saved);
    const auto editedPreview = fixtureReference(c, edited.saved.asset.edits, path(edited));
    std::vector<Reference> references{{"A_OLD", old.jpegPixels}, {"A_NEW", edited.jpegPixels}};
    for (std::size_t index = 1; index < pins.size(); ++index)
        references.push_back({"CONTROL_" + id(pins[index]), pins[index].jpegPixels});
    // Independently establish discrimination before any race input. At actual node size,
    // cardPixels repeats these checks without weakening the reviewed tolerance.
    require(meanRGB(old.jpegPixels, edited.jpegPixels) > 12,
            "Literal OLD/NEW references are indistinguishable for this fixture");
    for (std::size_t index = 1; index < pins.size(); ++index)
        require(meanRGB(edited.jpegPixels, pins[index].jpegPixels) > 12,
                "Expected NEW A is visually ambiguous with a control original");
    QJsonArray baseline;
    for (const auto &pin : pins)
        baseline.append(pinJSON(pin));
    writeJson(c.root + "/lib005-pinned-baseline.json",
              {{"assets", baseline},
               {"journal", journalBefore},
               {"literal_expected_edit", pinJSON(edited)},
               {"fixture_plan", geometryPlan},
               {"actual_open_native_picker_exercised", false}});

    auto *provider = dynamic_cast<ThumbnailProvider *>(c.engine.imageProvider("thumbnail"));
    require(provider, "LIB005 real engine ThumbnailProvider is absent");
    const qreal dpr = c.window->effectiveDevicePixelRatio();
    require(std::isfinite(dpr) && dpr > 0, "LIB005 effective DPR must be finite positive");
    const QSize physicalRequestSize = QSize(384, 384) * dpr;
    writeJson(c.root + "/lib005-dpr-plan.json",
              {{"effective_device_pixel_ratio", dpr},
               {"logical_source_width", 384},
               {"logical_source_height", 384},
               {"expected_request_width", physicalRequestSize.width()},
               {"expected_request_height", physicalRequestSize.height()}});
    trace::Session session(
        provider->uxTraceSlot(),
        trace::GateSpec{url(old), physicalRequestSize, std::chrono::milliseconds(30000)});
    ReleasePointer pointer{c, {}};
    QPointer<QQuickItem> oldCard, oldImage, returnedCard, returnedImage;
    std::unique_ptr<QSignalSpy> resets, opens, loads;
    std::unique_ptr<InvalidationAudit> invalidations;
    QJsonArray preReleasePixels, postReleasePixels, historyPixels;
    QJsonObject awayCheckpoint, returnCheckpoint, initialOldCache;
    std::uint64_t awayPressSequence = 0, backPressSequence = 0, newSerial = 0;
    try {
        d.click("main.libraryButton");
        QSignalSpy setupResets(&c.library, &QAbstractItemModel::modelReset);
        require(setupResets.isValid(), "Cannot observe the actual setup clear-search reload");
        d.type("library.search", "");
        d.wait(
            [&] {
                const auto value = session.snapshot();
                return value.gateEntered || value.diagnosticFailure || value.gateTimedOut;
            },
            "LIB005 actual OLD successful pending completion", 10000);
        d.wait(
            [&] {
                if (setupResets.count() < 2 || c.library.loading() ||
                    c.library.count() != fixtureCount)
                    return false;
                for (int row = 0; row < fixtureCount; ++row)
                    if (c.library.data(c.library.index(row), LibraryModel::AssetID)
                            .toString()
                            .isEmpty())
                        return false;
                return gridCard(c, old) != nullptr;
            },
            "LIB005 setup metadata/all activation resets settled", 10000);
        oldCard = gridCard(c, old);
        oldImage = imageFor(oldCard.data());
        identity(c, oldCard.data(), old, url(old), 2, true);
        held(session, old, physicalRequestSize, true, true);
        const auto oldCache = QFileInfo(cachePath(c, old));
        require(oldCache.isFile() && !oldCache.isSymLink() &&
                    pixels(QImage(oldCache.filePath())) == old.jpegPixels,
                "LIB005 held OLD result cache differs from independent original/neutral document");
        const auto oldCacheSHA = digest(oldCache.filePath());
        initialOldCache = {{"path", oldCache.filePath()},
                           {"sha256", QString::fromLatin1(oldCacheSHA.toHex())},
                           {"exact_neutral_jpeg_pixels", true}};
        resets = std::make_unique<QSignalSpy>(&c.library, &QAbstractItemModel::modelReset);
        opens = std::make_unique<QSignalSpy>(&c.library, &LibraryModel::openRequested);
        loads = std::make_unique<QSignalSpy>(&c.editor, &EditorController::photoLoaded);
        require(resets->isValid() && opens->isValid() && loads->isValid(),
                "LIB005 passive post-setup observations are unavailable");
        invalidations = std::make_unique<InvalidationAudit>(c, session);
        auto normal = [&](const Pin &expected, bool undo, bool redo, bool libraryMode) {
            editorState(c, expected, undo, redo, libraryMode);
            require(resets->count() == 0 && opens->count() == 0 && loads->count() == 0 &&
                        c.window->effectiveDevicePixelRatio() == dpr,
                    "LIB005 race reset/reopened/unloaded the asset or changed effective DPR");
        };
        auto currentOld = [&] {
            require(oldCard && oldImage && gridCard(c, old) == oldCard.data() &&
                        imageFor(oldCard.data()) == oldImage.data(),
                    "LIB005 CURRENT OLD binding was replaced before away input");
            identity(c, oldCard.data(), old, url(old), 2, true);
            const auto value = held(session, old, physicalRequestSize, true, true);
            normal(old, false, false, true);
            require(saves.count() == 0, "LIB005 CURRENT OLD already has an unexpected save");
            return value;
        };
        currentOld();
        durable(c, pins, old, journalBefore);
        auto *grid = d.item("library.grid");
        auto *bar = d.object("library.scrollBar");
        const double startY = grid->property("contentY").toDouble();
        const double firstSize = bar->property("size").toDouble();
        const double firstPosition = bar->property("position").toDouble();
        require(startY == grid->property("originY").toDouble() && firstSize > 0 && firstSize < 1,
                "LIB005 initial actual grid scrollbar has no top/overflow");
        const QPointF awayFrom(.5, firstPosition + firstSize / 2), awayTo(.5, 1 - firstSize / 2);
        pointer.last = d.point("library.scrollBar", awayFrom);
        d.press("library.scrollBar", awayFrom);
        pointer.down = true;
        require(bar->property("pressed").toBool(), "LIB005 genuine away bar press not acquired");
        const auto beforeAway = currentOld(); // AFTER actual press, BEFORE the first move.
        awayPressSequence = lastSequence(beforeAway);
        writeJson(c.root + "/lib005-before-away.json", {{"trace", traceJSON(beforeAway)},
                                                        {"old_uuid", id(old)},
                                                        {"old_loading", true},
                                                        {"bar_pressed", true},
                                                        {"content_y", startY},
                                                        {"initial_old_cache", initialOldCache}});
        pointer.last = d.point("library.scrollBar", awayTo);
        d.move("library.scrollBar", awayTo);
        held(session, old, physicalRequestSize, false);
        d.release("library.scrollBar", awayTo);
        pointer.down = false;
        require(!bar->property("pressed").toBool(), "Away input retained a pointer grab");
        d.wait([&] { return !grid->property("moving").toBool(); }, "LIB005 away movement settled",
               5000);
        const double awayY = grid->property("contentY").toDouble();
        require(awayY > startY + grid->height() && offscreen(c, oldCard),
                "LIB005 real away input did not leave the original CURRENT OLD viewport");
        d.wait(
            [&] {
                held(session, old, physicalRequestSize, false);
                auto *card = gridCard(c, tail);
                auto *image = card ? imageFor(card) : nullptr;
                return card && !offscreen(c, QPointer<QQuickItem>(card)) && image &&
                       image->property("source").toUrl() == url(tail) &&
                       image->property("status").toInt() == 1;
            },
            "LIB005 real away tail control Ready while OLD pending", 10000);
        const auto tailPixels =
            cardPixels(c, gridCard(c, tail), tail, url(tail), false, true, "CONTROL_" + id(tail),
                       references, [&] { held(session, old, physicalRequestSize, false); });
        normal(old, false, false, true);
        awayCheckpoint = {{"trace", traceJSON(session.snapshot())},
                          {"content_y", awayY},
                          {"old_offscreen_or_destroyed", offscreen(c, oldCard)},
                          {"tail_control_pixels", tailPixels}};
        writeJson(c.root + "/lib005-actual-away.json", awayCheckpoint);

        // A remains active in the Editor. Develop changes view visibility only; it does
        // not call activate/openFile or reset the Library model. Cached/filmstrip A may
        // lawfully receive NEW before back; no invented cold-request requirement.
        d.click("main.developButton");
        c.reveal("develop.exposureSlider");
        held(session, old, physicalRequestSize, false);
        normal(old, false, false, false);
        auto *slider = d.item("develop.exposureSlider");
        require(slider->isVisible() && slider->isEnabled() &&
                    slider->property("from").toDouble() == -5.0 &&
                    slider->property("to").toDouble() == 5.0 &&
                    slider->property("stepSize").toDouble() == 0.01 &&
                    slider->property("value").toDouble() == 0,
                "LIB005 Exposure control does not expose its known literal endpoint");
        pointer.last = d.point("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"));
        d.press("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"));
        pointer.down = true;
        require(slider->property("pressed").toBool(), "LIB005 real Exposure press not acquired");
        held(session, old, physicalRequestSize, false);
        const QPointF endpoint(1.0, .5);
        pointer.last = d.point("develop.exposureSlider", endpoint);
        d.move("develop.exposureSlider", endpoint);
        d.release("develop.exposureSlider", endpoint);
        pointer.down = false;
        require(!slider->property("pressed").toBool() &&
                    slider->property("value").toDouble() == 5.0 &&
                    c.editor.currentSettings() == edited.saved.asset.edits && c.editor.canUndo() &&
                    !c.editor.canRedo(),
                "LIB005 one actual gesture missed literal full edit/history/release");
        naturalSave(c, edited);
        require(saves.count() == 1 && saves.at(0).at(0).toString() == id(old),
                "LIB005 one gesture did not cause exactly one natural A autosave");
        invalidations->requireSingle(id(old));
        held(session, old, physicalRequestSize, false);
        normal(edited, true, false, false);
        durable(c, pins, edited, journalBefore);
        const auto expectedURL = generation(edited, 1);
        require(c.library.data(c.library.index(0), LibraryModel::Thumbnail).toUrl() == expectedURL,
                "LIB005 saved A missed exact natural generation=1 invalidation");
        writeJson(c.root + "/lib005-away-natural-save.json",
                  {{"trace", traceJSON(session.snapshot())},
                   {"invalidation", invalidations->json()},
                   {"expected_document", pinJSON(edited)},
                   {"new_may_be_requested_by_cached_or_filmstrip_consumer", true},
                   {"per_grid_consumer_response_identity_claimed", false}});

        d.click("main.libraryButton");
        d.frame();
        held(session, old, physicalRequestSize, false);
        normal(edited, true, false, true);
        require(grid->property("contentY").toDouble() > startY + grid->height() &&
                    (!gridCard(c, old) || offscreen(c, QPointer<QQuickItem>(gridCard(c, old)))),
                "LIB005 return-to-Library lost away viewport before actual back input");
        const double backSize = bar->property("size").toDouble();
        const double backPosition = bar->property("position").toDouble();
        require(backSize > 0 && backSize < 1 && backPosition > 0,
                "LIB005 actual back input has no away scrollbar baseline");
        const QPointF backFrom(.5, backPosition + backSize / 2), backTo(.5, backSize / 2);
        pointer.last = d.point("library.scrollBar", backFrom);
        d.press("library.scrollBar", backFrom);
        pointer.down = true;
        require(bar->property("pressed").toBool(), "LIB005 genuine back bar press not acquired");
        const auto beforeBack = held(session, old, physicalRequestSize, false);
        backPressSequence = lastSequence(beforeBack);
        writeJson(c.root + "/lib005-before-back.json",
                  {{"trace", traceJSON(beforeBack)},
                   {"bar_pressed", true},
                   {"actual_away_content_y", grid->property("contentY").toDouble()},
                   {"expected_new_source", expectedURL.toString(QUrl::FullyEncoded)}});
        pointer.last = d.point("library.scrollBar", backTo);
        d.move("library.scrollBar", backTo);
        held(session, old, physicalRequestSize, false);
        d.release("library.scrollBar", backTo);
        pointer.down = false;
        require(!bar->property("pressed").toBool(), "Back input retained a pointer grab");
        d.wait([&] { return !grid->property("moving").toBool(); }, "LIB005 back movement settled",
               5000);
        d.wait(
            [&] {
                held(session, old, physicalRequestSize, false);
                auto *card = gridCard(c, edited);
                auto *image = card ? imageFor(card) : nullptr;
                return card && image && !offscreen(c, QPointer<QQuickItem>(card)) &&
                       image->property("source").toUrl() == expectedURL &&
                       image->property("status").toInt() == 1;
            },
            "LIB005 returned current grid A NEW Ready before OLD release", 10000);
        require(grid->property("contentY").toDouble() == grid->property("originY").toDouble(),
                "LIB005 real back movement did not reach the original grid top");
        returnedCard = gridCard(c, edited);
        returnedImage = imageFor(returnedCard.data());
        identity(c, returnedCard.data(), edited, expectedURL, 1, true);
        const auto newReady = held(session, old, physicalRequestSize, false);
        const auto newRequests = requestsFor(newReady, expectedURL);
        QJsonArray allNewSerials;
        for (const auto &request : newRequests) {
            require(request.requestedSize == physicalRequestSize &&
                        request.providerRequestID == id(edited) + "?generation=1" &&
                        one(newReady, request.serial, Kind::Created).sequence > awayPressSequence,
                    "LIB005 NEW full URL/size/creation does not belong to the genuine saved "
                    "generation");
            allNewSerials.append(QString::number(request.serial));
            // The actual response can be shared by multiple Qt Image consumers. Select a
            // successful consumed result in this source domain, never pretend its serial
            // proves which individual card requested/consumed it.
            if (count(newReady, request.serial, Kind::SnapshotRead) == 1 &&
                count(newReady, request.serial, Kind::WorkerResultReady) == 1 &&
                count(newReady, request.serial, Kind::FinishedSignalObserved) == 1 &&
                count(newReady, request.serial, Kind::TextureFactoryCalled) >= 1 &&
                count(newReady, request.serial, Kind::Cancelled) == 0) {
                const auto &created = one(newReady, request.serial, Kind::Created);
                const auto &read = one(newReady, request.serial, Kind::SnapshotRead);
                const auto &result = one(newReady, request.serial, Kind::WorkerResultReady);
                const auto &finished = one(newReady, request.serial, Kind::FinishedSignalObserved);
                require(
                    read.revision == static_cast<std::uint64_t>(edited.saved.revision) &&
                        result.successfulPixels && created.sequence < read.sequence &&
                        read.sequence < result.sequence && result.sequence < finished.sequence,
                    "LIB005 actual NEW response did not consume exact saved revision successfully");
                require(std::any_of(newReady.events.begin(), newReady.events.end(),
                                    [&](const auto &event) {
                                        return event.serial == request.serial &&
                                               event.kind == Kind::TextureFactoryCalled &&
                                               event.sequence > finished.sequence;
                                    }),
                        "LIB005 actual NEW texture consumption did not follow its finished signal");
                if (!newSerial)
                    newSerial = request.serial;
            }
        }
        require(
            newSerial != 0 && newSerial != newReady.gatedSerial,
            "LIB005 no genuine consumed successful NEW source-domain response before OLD release");
        const auto stillHeld = [&] { held(session, old, physicalRequestSize, false); };
        std::optional<QSet<QString>> returnedVisibleUUIDs;
        auto visiblePixels = [&](const std::function<void()> &checkpoint) {
            QJsonArray result;
            const auto cards = visibleCards(c);
            require(!cards.empty(), "LIB005 returned viewport has no actual visible cards");
            bool foundA = false;
            QSet<QString> observedUUIDs;
            for (const auto &card : cards) {
                require(card, "LIB005 visible card disappeared before ownership readback");
                const auto asset = card->property("assetID").toString();
                require(!asset.isEmpty() && !observedUUIDs.contains(asset),
                        "LIB005 returned visible-card UUID is empty or duplicated");
                observedUUIDs.insert(asset);
                const auto found = std::find_if(pins.begin(), pins.end(),
                                                [&](const auto &pin) { return id(pin) == asset; });
                require(found != pins.end(), "LIB005 visible card has an unknown unpinned UUID");
                const bool isA = asset == id(edited);
                const auto &expected = isA ? edited : *found;
                d.wait(
                    [&] {
                        if (checkpoint)
                            checkpoint();
                        const auto *currentImage = card ? imageFor(card.data()) : nullptr;
                        return currentImage && currentImage->property("status").toInt() == 1;
                    },
                    "LIB005 returned visible card actual Ready", 10000);
                auto *image = card ? imageFor(card.data()) : nullptr;
                require(image, "LIB005 visible Image disappeared after its Ready wait");
                const auto source = image->property("source").toUrl();
                require(source == (isA ? expectedURL : url(expected)) ||
                            (!isA && source == generation(expected, 1)),
                        "LIB005 control binding has an unrelated generation/retry source");
                result.append(cardPixels(c, card.data(), expected, source, isA, isA,
                                         isA ? "A_NEW" : "CONTROL_" + asset, references,
                                         checkpoint));
                foundA = foundA || isA;
            }
            require(foundA,
                    "LIB005 returned viewport omitted CURRENT A from visible-card readback");
            if (returnedVisibleUUIDs)
                require(observedUUIDs == *returnedVisibleUUIDs,
                        "LIB005 OLD completion changed the exact returned visible-card UUID set");
            else
                returnedVisibleUUIDs = observedUUIDs;
            return result;
        };
        preReleasePixels = visiblePixels(stillHeld);
        normal(edited, true, false, true);
        durable(c, pins, edited, journalBefore);
        invalidations->requireSingle(id(old));
        require(returnedCard && returnedImage && gridCard(c, edited) == returnedCard.data() &&
                    imageFor(returnedCard.data()) == returnedImage.data(),
                "LIB005 returned A binding was lost during other visible-card readbacks");
        identity(c, returnedCard.data(), edited, expectedURL, 1, true);
        const auto returnedGeometry = returnedImage->size();
        const auto viewportSize = grid->size();
        const auto viewportOrigin = grid->mapToScene(QPointF{});
        const double returnedY = grid->property("contentY").toDouble();
        const auto observeProperty = [](QObject *object, const char *name) {
            require(object, "LIB005 observed returned binding QObject is absent");
            const int index = object->metaObject()->indexOfProperty(name);
            require(index >= 0, "LIB005 observed returned binding property is absent");
            const auto property = object->metaObject()->property(index);
            require(property.hasNotifySignal(),
                    "LIB005 returned property has no passive notify signal");
            auto spy = std::make_unique<QSignalSpy>(object, property.notifySignal());
            require(spy->isValid(), "LIB005 cannot passively observe returned binding");
            return spy;
        };
        const auto returnedSources = observeProperty(returnedImage.data(), "source");
        const auto returnedAssets = observeProperty(returnedCard.data(), "assetID");
        const auto returnedURLs = observeProperty(returnedCard.data(), "thumbnailURL");
        const auto returnedPositions = observeProperty(grid, "contentY");
        const auto beforeRelease = held(session, old, physicalRequestSize, false);
        require(returnedVisibleUUIDs.has_value(),
                "LIB005 returned visible UUID baseline is absent");
        QStringList visibleUUIDNames;
        for (const auto &asset : *returnedVisibleUUIDs)
            visibleUUIDNames.append(asset);
        visibleUUIDNames.sort();
        returnCheckpoint = {
            {"trace", traceJSON(beforeRelease)},
            {"visible_uuid_set", QJsonArray::fromStringList(visibleUUIDNames)},
            {"all_new_source_domain_serials", allNewSerials},
            {"observed_new_consumed_serial", QString::number(newSerial)},
            {"actual_returned_grid_uuid", id(edited)},
            {"actual_returned_grid_source", expectedURL.toString(QUrl::FullyEncoded)},
            {"actual_returned_grid_ready", true},
            {"actual_back_input", true},
            {"content_y", returnedY},
            {"pixels", preReleasePixels},
            {"same_qobject_across_away_back_required", false},
            {"new_request_only_after_back_required", false},
            {"per_grid_consumer_response_identity_claimed", false}};
        writeJson(c.root + "/lib005-new-ready-old-held.json", returnCheckpoint);
        session.release();
        d.wait(
            [&] {
                const auto value = session.snapshot();
                return value.diagnosticFailure ||
                       (count(value, value.gatedSerial, Kind::FinishedSignalObserved) == 1 &&
                        count(value, value.gatedSerial, Kind::Destroyed) == 1);
            },
            "LIB005 OLD real finished/destruction after explicit release", 10000);
        const auto terminal = session.snapshot();
        require(
            !terminal.diagnosticFailure && !terminal.gateTimedOut && terminal.releaseRequested &&
                !terminal.sessionClosing && terminal.gatedSerial == beforeRelease.gatedSerial &&
                one(terminal, terminal.gatedSerial, Kind::GateReleased).sequence >
                    lastSequence(beforeRelease) &&
                one(terminal, terminal.gatedSerial, Kind::FinishedSignalObserved).sequence >
                    one(terminal, terminal.gatedSerial, Kind::GateReleased).sequence &&
                one(terminal, terminal.gatedSerial, Kind::Destroyed).sequence >
                    one(terminal, terminal.gatedSerial, Kind::FinishedSignalObserved).sequence &&
                count(terminal, terminal.gatedSerial, Kind::GateTimedOut) == 0 &&
                count(terminal, terminal.gatedSerial, Kind::GateSkipped) == 0,
            "LIB005 OLD terminal observation is failed/stale/out of order");
        const int cancelled = count(terminal, terminal.gatedSerial, Kind::Cancelled);
        require(
            cancelled <= 1 &&
                ((cancelled == 1 &&
                  count(terminal, terminal.gatedSerial, Kind::TextureFactoryCalled) == 0 &&
                  one(terminal, terminal.gatedSerial, Kind::Cancelled).sequence <
                      one(terminal, terminal.gatedSerial, Kind::FinishedSignalObserved).sequence) ||
                 (cancelled == 0 &&
                  count(terminal, terminal.gatedSerial, Kind::TextureFactoryCalled) >= 1)),
            "LIB005 OLD actual cancel/consume cleanup branch is inconsistent");
        for (int frame = 0; frame < 2; ++frame) {
            d.frame(); // Bounded ownership checkpoints; no FPS/latency/performance assertion.
            require(returnedCard && returnedImage && gridCard(c, edited) == returnedCard.data() &&
                        imageFor(returnedCard.data()) == returnedImage.data() &&
                        returnedImage->size() == returnedGeometry && grid->size() == viewportSize &&
                        grid->mapToScene(QPointF{}) == viewportOrigin &&
                        grid->property("contentY").toDouble() == returnedY,
                    "LIB005 late OLD completion rebound the returned card or altered its viewport");
            postReleasePixels.append(
                QJsonObject{{"frame_checkpoint", frame}, {"visible_cards", visiblePixels({})}});
            normal(edited, true, false, true);
        }
        durable(c, pins, edited, journalBefore);
        require(returnedSources->count() == 0 && returnedAssets->count() == 0 &&
                    returnedURLs->count() == 0 && returnedPositions->count() == 0,
                "LIB005 OLD terminal changed a returned source/UUID/URL/viewport even transiently");
        require(digest(oldCache.filePath()) == oldCacheSHA && saves.count() == 1,
                "LIB005 stale completion altered neutral OLD cache or generated another edit/save");
        invalidations->requireSingle(id(old));
        const auto raceTerminal = session.snapshot();
        writeJson(c.root + "/lib005-race-terminal.json",
                  {{"trace", traceJSON(raceTerminal)},
                   {"before_release_pixels", preReleasePixels},
                   {"after_release_pixels", postReleasePixels},
                   {"old_cancelled", cancelled == 1},
                   {"old_gui_reply_claimed", false},
                   {"normal_model_resets", resets->count()},
                   {"new_serial_is_source_domain_only", true},
                   {"returned_image_source_changes", returnedSources->count()},
                   {"returned_card_asset_changes", returnedAssets->count()},
                   {"returned_card_url_changes", returnedURLs->count()},
                   {"returned_viewport_position_changes", returnedPositions->count()},
                   {"invalidation", invalidations->json()},
                   {"expected_document", pinJSON(edited)}});

        // History is checked after the race has terminated, through actual Develop toolbar
        // actions (Library toolbar Undo belongs to batch). Exactly one Undo/Redo, no cycles.
        d.click("main.developButton");
        rendered(c, edited, editedPreview);
        normal(edited, true, false, false);
        Pin undo = old;
        undo.saved.revision = edited.saved.revision + 1;
        d.click("main.undoButton");
        naturalSave(c, undo);
        rendered(c, undo, oldPreview);
        normal(undo, false, true, false);
        durable(c, pins, undo, journalBefore);
        historyPixels.append(
            QJsonObject{{"action", "actual_one_undo"}, {"expected", pinJSON(undo)}});
        Pin redo = edited;
        redo.saved.revision = undo.saved.revision + 1;
        d.click("main.redoButton");
        naturalSave(c, redo);
        rendered(c, redo, editedPreview);
        normal(redo, true, false, false);
        durable(c, pins, redo, journalBefore);
        historyPixels.append(
            QJsonObject{{"action", "actual_one_redo"}, {"expected", pinJSON(redo)}});
        invalidations->requireCount(id(old), 3);
        require(saves.count() == 3 && saves.at(1).at(0).toString() == id(old) &&
                    saves.at(2).at(0).toString() == id(old) &&
                    !session.snapshot().diagnosticFailure,
                "LIB005 bounded one-edit Undo/Redo leaked saves or invalid trace state");
        writeJson(
            c.root + "/lib005-actual-response-summary.json",
            {{"scenario", identifier},
             {"status", "PASS"},
             {"inventory_domain", identifier == officialID ? "specification" : "supplemental"},
             {"full_release_qualified", false},
             {"race_terminal_trace", traceJSON(raceTerminal)},
             {"history_final_trace", traceJSON(session.snapshot())},
             {"old_uuid", id(old)},
             {"old_serial", QString::number(raceTerminal.gatedSerial)},
             {"new_consumed_source_domain_serial", QString::number(newSerial)},
             {"away_press_sequence", QString::number(awayPressSequence)},
             {"back_press_sequence", QString::number(backPressSequence)},
             {"actual_away", awayCheckpoint},
             {"actual_back", returnCheckpoint},
             {"one_edit_undo_redo", historyPixels},
             {"baseline_journal", journalBefore},
             {"race_revision", QString::number(edited.saved.revision)},
             {"final_redo_revision", QString::number(redo.saved.revision)},
             {"boundary", "actual Library away before successful OLD pending completion; "
                          "genuine saved A edit; actual Library back NEW Ready before OLD release"},
             {"scope", "Qt/QTest window input and actual Image-node readback; shared source-domain "
                       "response trace; CPU/ICC/JPEG reference path; no physical-scanout or active "
                       "CPU-render claim"}});
        return true;
    } catch (...) {
        // Preserve actual failure state before RAII repairs pointer/session lifetime.
        try {
            writeJson(
                c.root + "/lib005-original-failure-before-cleanup.json",
                {{"trace", traceJSON(session.snapshot())},
                 {"pointer_down", pointer.down},
                 {"library_bar_pressed",
                  d.object("library.scrollBar")->property("pressed").toBool()},
                 {"slider_pressed",
                  d.object("develop.exposureSlider")->property("pressed").toBool()},
                 {"actual_editor_settings",
                  QJsonDocument::fromJson(luma::encodePreset({"Actual failure settings",
                                                              {},
                                                              c.editor.currentSettings(),
                                                              luma::allSettingsGroups}))
                      .object()},
                 {"actual_local_masks_serialized", QString::fromStdString(luma::encodeLocalMasks(
                                                       c.editor.currentSettings().localMasks))},
                 {"actual_photo_uuid", c.editor.photoID()},
                 {"actual_source", c.editor.sourcePath()},
                 {"actual_saving", c.editor.saving()},
                 {"actual_model_count", c.library.count()},
                 {"actual_content_y", d.item("library.grid")->property("contentY").toDouble()},
                 {"actual_library_mode", c.window->property("libraryMode").toBool()},
                 {"old_card_present", !oldCard.isNull()},
                 {"returned_card_present", !returnedCard.isNull()},
                 {"away_checkpoint", awayCheckpoint},
                 {"return_checkpoint", returnCheckpoint}});
        } catch (...) {
            std::fputs("LIB005 failure evidence capture failed; original exception preserved.\n",
                       stderr);
        }
        throw;
    }
}
} // namespace ux
