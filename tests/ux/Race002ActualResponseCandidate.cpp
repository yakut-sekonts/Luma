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
#error "RACE002 candidate requires the real response observation seam"
#endif

// Official regression and historical supplemental ID share the same strict contract.
namespace ux {
namespace {
namespace trace = thumbnail_trace;
constexpr auto candidateID = "CANDIDATE-RACE002-THUMBNAIL-INVALIDATION";
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
    require(file.open(QIODevice::ReadOnly), "Cannot hash owned RACE002 file");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file), "Cannot read complete owned RACE002 file");
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
        luma::encodePreset({"RACE002 pinned settings", {}, asset.edits, luma::allSettingsGroups}));
    require(encoded.isObject(), "Cannot encode RACE002 preset-schema diagnostic settings");
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
            "RACE002 requires small opaque generated PNGs");
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
                     bool uncancelled) {
    const auto value = session.snapshot();
    require(!value.diagnosticFailure && value.gateEntered && !value.gateTimedOut &&
                !value.releaseRequested && !value.sessionClosing && value.gatedSerial != 0,
            "Actual OLD pending-completion gate is not currently held");
    const auto requests = requestsFor(value, url(old));
    require(requests.size() == 1 && requests.front().serial == value.gatedSerial &&
                requests.front().requestedSize == expectedRequestSize &&
                requests.front().providerRequestID == id(old) + "?generation=0",
            "OLD full source/size is missing, ambiguous, retried or belongs to another response");
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
QQuickItem *filmstripCard(Context &c, const Pin &pin) {
    return qobject_cast<QQuickItem *>(c.driver->find("filmstrip.card." + id(pin)));
}
void identity(Context &c, QQuickItem *card, const Pin &pin, const QUrl &source, int status,
              bool active, bool currentModelURL = true) {
    auto *view = c.driver->item("develop.filmstrip");
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
        "RACE002 current visible filmstrip UUID/row/path/source/Ready/retry/metadata ownership is "
        "invalid");
}
double meanRGB(const QImage &a, const QImage &b, int border = 4) {
    require(a.size() == b.size() && a.width() > 2 * border && a.height() > 2 * border,
            "Non-comparable RACE002 actual pixel region");
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
            "RACE002 actual saved cache differs from independent original/expected-document JPEG");
    const auto capture =
        readyItemGrab(c, *imageGuard, "RACE002 actual thumbnail Image-node readback");
    require(cardGuard && imageGuard && imageFor(cardGuard.data()) == imageGuard.data() &&
                imageGuard->size() == geometry,
            "RACE002 current Image rebound during actual readback");
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
        require(wrong.size() == own.size(), "RACE002 reference aspect ratios differ");
        const double separation = meanRGB(own, wrong), mismatch = meanRGB(cropped, wrong);
        require(separation > 12,
                "RACE002 OLD/NEW/control references indistinguishable at actual capture size");
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
        "RACE002 current thumbnail shows a stale revision/control or misses its expected pixels");
    const QJsonObject result{{"uuid", id(pin)},
                             {"source", source.toString(QUrl::FullyEncoded)},
                             {"expected_label", ownLabel},
                             {"mean_rgb_own_error", ownError},
                             {"closest_wrong_label", closestLabel},
                             {"mean_rgb_closest_wrong", closestWrong},
                             {"negative_references", negatives}};
    auto action = result;
    action.insert("action", "race002_independent_thumbnail_pixels");
    c.driver->log(action);
    return result;
}
QJsonArray readRows(Context &c, const char *query) {
    sqlite3 *opened = nullptr;
    const int result =
        sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &opened, SQLITE_OPEN_READONLY, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(opened, sqlite3_close);
    require(result == SQLITE_OK && db, "Cannot open RACE002 read-only journal observation");
    sqlite3_busy_timeout(db.get(), 1000);
    sqlite3_stmt *prepared = nullptr;
    const int preparedResult = sqlite3_prepare_v2(db.get(), query, -1, &prepared, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(prepared,
                                                                               sqlite3_finalize);
    require(preparedResult == SQLITE_OK && statement,
            "Cannot prepare RACE002 read-only journal observation");
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
    require(step == SQLITE_DONE, "Cannot complete RACE002 read-only journal observation");
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
        {"session", readRows(c, "SELECT slot,asset_id FROM session ORDER BY slot")}};
}
void durable(Context &c, const Pins &baseline, const Pin &expectedA,
             const QJsonObject &journalBefore) {
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto rows = catalog.libraryPage({}, 0, 2);
    require(baseline.size() == 2 && rows.size() == 2 && catalog.libraryCount() == 2,
            "RACE002 changed exact two-source membership");
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
                "RACE002 full typed document/revision/row/metadata/original identity mismatch");
    }
    const auto active = catalog.activeAsset();
    require(active && active->id == expectedA.saved.asset.id &&
                active->path == expectedA.saved.asset.path &&
                active->edits == expectedA.saved.asset.edits && journal(c) == journalBefore,
            "RACE002 changed selected durable session or completed Import journal");
    catalog.verify();
}
void editorState(Context &c, const Pin &a, bool undo, bool redo) {
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
            c.library.count() == 2 && c.window->isActive() && c.window->isExposed() &&
            !c.window->property("libraryMode").toBool() &&
            c.driver->object("library.search")->property("text").toString().isEmpty() &&
            c.driver->object("library.ratingFilter")->property("currentIndex").toInt() == 0 &&
            c.driver->object("libraryFlagFilter")->property("currentIndex").toInt() == 0 &&
            c.driver->object("libraryLabelFilter")->property("currentIndex").toInt() == 0 &&
            c.driver->object("library.pane")->property("currentCollection").toString().isEmpty(),
        "RACE002 editor/history/selection/filter/job state differs from exact expected state");
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
                   "RACE002 actual main Image Ready/current source/expected full-document pixels",
                   10000);
    c.driver->frame();
    require(ready() && c.editor.photoID() == id(a) &&
                c.editor.currentSettings() == a.saved.asset.edits,
            "RACE002 current preview/document did not remain correct through polish");
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
        "RACE002 natural exact full-document/revision autosave", 10000);
    require(c.editor.error().isEmpty(), c.editor.error());
}
Pins importHidden(Context &c) {
    auto &d = *c.driver;
    require(!c.editor.hasPhoto() && c.library.count() == 0 &&
                luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 0,
            "RACE002 needs a fresh isolated empty catalog");
    QSignalSpy searchResets(&c.library, &QAbstractItemModel::modelReset);
    require(searchResets.isValid(), "Cannot observe actual no-match filter reload");
    d.type("library.search", "race002-no-such-filter-8ff23a");
    d.wait(
        [&] { return searchResets.count() >= 2 && !c.library.loading() && c.library.count() == 0; },
        "actual no-match search fully reloaded");
    const auto directory = c.root + "/owned-race002-originals";
    require(QDir().mkpath(directory), "Cannot create own RACE002 source directory");
    const auto names = QDir(c.fixture).entryList({"Photo-*.png"}, QDir::Files, QDir::Name);
    require(names.size() == 2, "RACE002 requires exactly two private generated RGB PNG fixtures");
    QList<QUrl> sources;
    for (const auto &name : names) {
        const auto target = directory + '/' + name;
        require(QFile::copy(c.fixture + '/' + name, target) &&
                    digest(target) == digest(c.fixture + '/' + name),
                "Cannot copy identical private RACE002 source");
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
        "actual two-source Add behind no-match filter", 30000);
    d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
           "Import dialog closed");
    require(c.library.error().isEmpty(), c.library.error());
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto rows = catalog.libraryPage({}, 0, 2);
    require(rows.size() == 2 && catalog.libraryCount() == 2,
            "RACE002 Add did not commit exact two-source membership");
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
            "RACE002 imported baseline is not independently neutral/pinned");
        pins.push_back({*saved, row, expectedJPEG(*saved)});
        d.frame();
    }
    require(!d.find("library.card." + id(pins[0])) && !d.find("filmstrip.card." + id(pins[0])) &&
                !d.find("library.card." + id(pins[1])) && !d.find("filmstrip.card." + id(pins[1])),
            "RACE002 pre-Session cards exist despite no-match search");
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
            "RACE002 actual FileDialog rejected declared selected-file result");
    d.log({{"action", "file_dialog_selection_fixture_boundary"},
           {"id", "main.openDialog"},
           {"path", path(a)},
           {"native_selection_exercised", false},
           {"all_cards_hidden", true}});
    require(QMetaObject::invokeMethod(dialog, "close", Qt::DirectConnection) &&
                QMetaObject::invokeMethod(dialog, "accepted", Qt::DirectConnection),
            "RACE002 FileDialog production close/accepted handler unavailable");
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
            "RACE002 Open setup changed neutral document/history/selection");
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
                "RACE002 actual first Thumbnail invalidation did not overlap the unfinished held "
                "OLD request");
    }
    void requireCount(const QString &a, int expected) const {
        QStringList ids;
        for (int index = 0; index < expected; ++index)
            ids.append(a);
        require(
            !failed && exactRoles && notifications == expected && assets == ids,
            "RACE002 save did not cause the exact genuine targeted Thumbnail invalidations for A");
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
                std::fputs("RACE002 pointer cleanup failed\n", stderr);
            }
    }
};
} // namespace

bool race002ActualResponseCandidate(Context &c, const QString &identifier) {
    if (identifier != candidateID && identifier != "UX-RACE-002")
        return false;
    auto &d = *c.driver;
    QSignalSpy allSaves(&c.editor, &EditorController::editsSaved);
    require(allSaves.isValid(), "Cannot observe natural editor saves from fresh setup");
    const auto pins = importHidden(c);
    const auto &old = pins[0], &b = pins[1];
    openHidden(c, old);
    const auto oldPreview = fixtureReference(c, old.saved.asset.edits, path(old));
    rendered(c, old, oldPreview);
    const auto journalBefore = journal(c);
    QJsonArray expectedSession;
    expectedSession.append(QJsonValue(QJsonArray{QStringLiteral("1"), id(old)}));
    const QJsonArray legacyExpectedSession{QJsonArray{"1", id(old)}};
    writeJson(c.root + "/race002-setup-journal.json",
              {{"actual", journalBefore},
               {"expected_session", expectedSession},
               {"legacy_expected_session", legacyExpectedSession},
               {"expected_active_uuid", id(old)},
               {"imports_count_matches", journalBefore["imports"].toArray().size() == 1},
               {"items_count_matches", journalBefore["items"].toArray().size() == 2},
               {"session_matches", journalBefore["session"].toArray() == expectedSession}});
    require(journalBefore["imports"].toArray().size() == 1 &&
                journalBefore["items"].toArray().size() == 2 &&
                journalBefore["session"].toArray() == expectedSession,
            "RACE002 setup Import/session journal cardinality is invalid");
    const auto imported = journalBefore["imports"].toArray().at(0).toArray();
    require(imported.size() == 8 && imported.at(1).toString() == "add" &&
                imported.at(2).toString().isEmpty() && imported.at(3).toString() == "completed" &&
                imported.at(4).toString().toLongLong() > 0 && imported.at(5).toString() == "0",
            "RACE002 actual import journal is not one completed Add with duplicates disabled");
    for (int index = 0; index < 2; ++index) {
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
                "RACE002 actual completed Import item differs from its private original/UUID/hash");
    }
    durable(c, pins, old, journalBefore);
    require(allSaves.count() == 0 && c.library.count() == 0 && !c.library.loading() &&
                !d.find("library.card." + id(old)) && !d.find("filmstrip.card." + id(old)) &&
                !d.find("library.card." + id(b)) && !d.find("filmstrip.card." + id(b)),
            "RACE002 hidden Open warmed cards or emitted unexpected pre-Session invalidation");
    Pin edited = old;
    edited.saved.asset.edits.exposure =
        5.0; // Literal UI endpoint; never copied from actual controller output.
    edited.saved.revision = old.saved.revision + 1;
    edited.jpegPixels = expectedJPEG(edited.saved);
    const auto editedPreview = fixtureReference(c, edited.saved.asset.edits, path(edited));
    const std::vector<Reference> references{
        {"A_OLD", old.jpegPixels}, {"A_NEW", edited.jpegPixels}, {"B_CONTROL", b.jpegPixels}};
    QJsonArray baseline;
    for (const auto &pin : pins)
        baseline.append(pinJSON(pin));
    writeJson(c.root + "/race002-pinned-baseline.json",
              {{"assets", baseline},
               {"journal", journalBefore},
               {"expected_edit", pinJSON(edited)},
               {"actual_open_native_picker_exercised", false}});
    auto *provider = dynamic_cast<ThumbnailProvider *>(c.engine.imageProvider("thumbnail"));
    require(provider, "RACE002 actual engine ThumbnailProvider unavailable");
    // Qt6.10.3 QQuickImageBase::loadPixmap scales explicit sourceSize by effective DPR.
    // Independent plan BEFORE arm; never infer size from whichever tuple arrives.
    const qreal devicePixelRatio = c.window->effectiveDevicePixelRatio();
    require(std::isfinite(devicePixelRatio) && devicePixelRatio > 0,
            "RACE002 window effective DPR is not finite positive");
    const QSize expectedRequestSize = QSize(384, 384) * devicePixelRatio;
    writeJson(c.root + "/race002-dpr-plan.json",
              {{"effective_device_pixel_ratio", devicePixelRatio},
               {"logical_source_width", 384},
               {"logical_source_height", 384},
               {"expected_request_width", expectedRequestSize.width()},
               {"expected_request_height", expectedRequestSize.height()}});
    trace::Session session(
        provider->uxTraceSlot(),
        trace::GateSpec{url(old), expectedRequestSize, std::chrono::milliseconds(30000)});
    ReleasePointer pointer{c, {}}; // catch remains inside both pointer and Session lifetimes.
    QPointer<QQuickItem> oldCard, oldImage, bCard, bImage;
    std::unique_ptr<QSignalSpy> resets, opens, loads, bSources;
    std::unique_ptr<InvalidationAudit> invalidations;
    QJsonArray racePixels, finalPixels, historyPixels;
    QJsonObject baselineBPixels, baselineOldCache;
    std::uint64_t pressSequence = 0, newSerial = 0;
    try {
        d.click("main.libraryButton");
        QSignalSpy setupResets(&c.library, &QAbstractItemModel::modelReset);
        require(setupResets.isValid(), "Cannot observe actual clear-search setup reload");
        d.type("library.search", "");
        d.wait(
            [&] {
                const auto value = session.snapshot();
                return value.gateEntered || value.diagnosticFailure || value.gateTimedOut;
            },
            "RACE002 first post-activation actual A successful pending completion", 10000);
        d.wait(
            [&] {
                return setupResets.count() >= 2 && !c.library.loading() && c.library.count() == 2 &&
                       !c.library.data(c.library.index(0), LibraryModel::AssetID)
                            .toString()
                            .isEmpty() &&
                       !c.library.data(c.library.index(1), LibraryModel::AssetID)
                            .toString()
                            .isEmpty();
            },
            "RACE002 actual clear-search reset and both metadata rows complete");
        held(session, old, expectedRequestSize, true);
        d.click("main.developButton");
        c.reveal("develop.exposureSlider");
        d.wait(
            [&] {
                return filmstripCard(c, old) && imageFor(filmstripCard(c, old)) &&
                       filmstripCard(c, b) && imageFor(filmstripCard(c, b));
            },
            "RACE002 actual current filmstrip delegates");
        oldCard = filmstripCard(c, old);
        oldImage = imageFor(oldCard.data());
        bCard = filmstripCard(c, b);
        bImage = imageFor(bCard.data());
        identity(c, oldCard.data(), old, url(old), 2, true);
        d.wait(
            [&] {
                held(session, old, expectedRequestSize, true);
                return bCard && bImage && bImage->property("source").toUrl() == url(b) &&
                       bImage->property("status").toInt() == 1;
            },
            "RACE002 initial B source/Ready positive control before source-change observers", 5000);
        baselineBPixels =
            cardPixels(c, bCard.data(), b, url(b), false, true, "B_CONTROL", references,
                       [&] { held(session, old, expectedRequestSize, true); });
        // The successful OLD worker has finished the same real cache read/write before its gate.
        // One independent cache positive control fixes that OLD payload baseline; no manual
        // response access.
        held(session, old, expectedRequestSize, true);
        const QFileInfo oldCache(cachePath(c, old));
        require(oldCache.isFile() && !oldCache.isSymLink(),
                "RACE002 actual OLD cache is not an owned regular JPEG");
        const auto oldCached = pixels(QImage(oldCache.filePath()));
        require(
            !oldCached.isNull() && oldCached == old.jpegPixels,
            "RACE002 successful OLD cache does not match independent pinned OLD document pixels");
        baselineOldCache = {
            {"cache", oldCache.filePath()},
            {"cache_sha256", QString::fromLatin1(digest(oldCache.filePath()).toHex())},
            {"actual_width", oldCached.width()},
            {"actual_height", oldCached.height()},
            {"reference_width", old.jpegPixels.width()},
            {"reference_height", old.jpegPixels.height()},
            {"exact_pinned_old_jpeg_pixels", true}};
        held(session, old, expectedRequestSize, true);
        // All legal activation/clear-search/layout changes are BEFORE this checkpoint.
        resets = std::make_unique<QSignalSpy>(&c.library, &QAbstractItemModel::modelReset);
        opens = std::make_unique<QSignalSpy>(&c.library, &LibraryModel::openRequested);
        loads = std::make_unique<QSignalSpy>(&c.editor, &EditorController::photoLoaded);
        const auto sourceProperty =
            bImage->metaObject()->property(bImage->metaObject()->indexOfProperty("source"));
        bSources = std::make_unique<QSignalSpy>(bImage.data(), sourceProperty.notifySignal());
        require(resets->isValid() && opens->isValid() && loads->isValid() && bSources->isValid(),
                "RACE002 passive race-checkpoint signal observations unavailable");
        invalidations = std::make_unique<InvalidationAudit>(c, session);
        const auto currentOld = [&] {
            require(oldCard && oldImage && filmstripCard(c, old) == oldCard.data() &&
                        imageFor(oldCard.data()) == oldImage.data(),
                    "RACE002 OLD current UI binding replaced before actual input");
            identity(c, oldCard.data(), old, url(old), 2, true);
            const auto observed = held(session, old, expectedRequestSize, true);
            require(allSaves.count() == 0 && resets->count() == 0 && opens->count() == 0 &&
                        loads->count() == 0 &&
                        c.window->effectiveDevicePixelRatio() == devicePixelRatio,
                    "RACE002 actual OLD setup was cancelled/reset/opened/saved before input");
            editorState(c, old, false, false);
            return observed;
        };
        currentOld();
        durable(c, pins, old, journalBefore);
        auto *slider = d.item("develop.exposureSlider");
        require(slider->isVisible() && slider->isEnabled() &&
                    slider->property("from").toDouble() == -5.0 &&
                    slider->property("to").toDouble() == 5.0 &&
                    slider->property("stepSize").toDouble() == 0.01 &&
                    slider->property("value").toDouble() == 0,
                "RACE002 actual Exposure control does not expose known literal endpoint fixture");
        pointer.last = d.point("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"));
        d.press("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"));
        pointer.down = true;
        require(slider->property("pressed").toBool(), "RACE002 actual Exposure press not acquired");
        const auto beforeMove = currentOld(); // AFTER genuine press, BEFORE any genuine move.
        pressSequence = lastSequence(beforeMove);
        writeJson(c.root + "/race002-before-move.json",
                  {{"trace", traceJSON(beforeMove)},
                   {"current_old_source",
                    oldImage->property("source").toUrl().toString(QUrl::FullyEncoded)},
                   {"current_old_status", oldImage->property("status").toInt()},
                   {"slider_pressed", true},
                   {"old_uuid", id(old)},
                   {"baseline_old_cache", baselineOldCache},
                   {"baseline_b_pixels", baselineBPixels},
                   {"setup_model_resets", setupResets.count()},
                   {"race_model_resets", resets->count()}});
        const QPointF endpoint(1.0, .5);
        pointer.last = d.point("develop.exposureSlider", endpoint);
        d.move("develop.exposureSlider", endpoint);
        d.release("develop.exposureSlider", endpoint);
        pointer.down = false;
        require(!slider->property("pressed").toBool() &&
                    slider->property("value").toDouble() == 5.0 &&
                    c.editor.currentSettings() == edited.saved.asset.edits && c.editor.canUndo() &&
                    !c.editor.canRedo(),
                "RACE002 genuine one-move Exposure gesture missed literal endpoint/full "
                "document/history/release");
        naturalSave(c, edited);
        require(allSaves.count() == 1 && allSaves.at(0).at(0).toString() == id(old),
                "RACE002 genuine gesture did not produce exactly one natural A save");
        invalidations->requireSingle(id(old));
        const auto afterSave = held(session, old, expectedRequestSize, false);
        const auto expectedURL = generation(edited, 1);
        require(c.library.data(c.library.index(0), LibraryModel::Thumbnail).toUrl() ==
                        expectedURL &&
                    bCard && bImage && filmstripCard(c, b) == bCard.data() &&
                    imageFor(bCard.data()) == bImage.data() && bSources->count() == 0 &&
                    bImage->property("source").toUrl() == url(b) && resets->count() == 0 &&
                    opens->count() == 0 && loads->count() == 0,
                "RACE002 targeted A save changed B binding, reset/opened a photo or missed its "
                "natural generation");
        durable(c, pins, edited, journalBefore);
        editorState(c, edited, true, false);
        writeJson(c.root + "/race002-natural-invalidation.json",
                  {{"trace", traceJSON(afterSave)},
                   {"invalidation", invalidations->json()},
                   {"actual_a_model_source", expectedURL.toString(QUrl::FullyEncoded)},
                   {"actual_b_binding_source",
                    bImage->property("source").toUrl().toString(QUrl::FullyEncoded)},
                   {"natural_saved_revision", QString::number(edited.saved.revision)},
                   {"saves", allSaves.count()}});
        d.wait(
            [&] {
                held(session, old, expectedRequestSize, false);
                auto *card = filmstripCard(c, edited);
                auto *image = imageFor(card);
                const auto requests = requestsFor(session.snapshot(), expectedURL);
                return card && image && image->property("status").toInt() == 1 &&
                       requests.size() == 1 &&
                       count(session.snapshot(), requests.front().serial,
                             Kind::FinishedSignalObserved) == 1;
            },
            "RACE002 NEW actual saved-revision thumbnail Ready while OLD still held", 10000);
        const auto newReady = held(session, old, expectedRequestSize, false);
        const auto requests = requestsFor(newReady, expectedURL);
        require(requests.size() == 1 && requests.front().requestedSize == expectedRequestSize &&
                    requests.front().providerRequestID == id(edited) + "?generation=1",
                "RACE002 NEW exact request tuple ambiguous or absent");
        newSerial = requests.front().serial;
        require(newSerial != newReady.gatedSerial &&
                    one(newReady, newSerial, Kind::Created).sequence > pressSequence &&
                    one(newReady, newSerial, Kind::Created).sequence <
                        one(newReady, newSerial, Kind::SnapshotRead).sequence &&
                    one(newReady, newSerial, Kind::SnapshotRead).revision ==
                        static_cast<std::uint64_t>(edited.saved.revision) &&
                    one(newReady, newSerial, Kind::SnapshotRead).sequence <
                        one(newReady, newSerial, Kind::WorkerResultReady).sequence &&
                    one(newReady, newSerial, Kind::WorkerResultReady).successfulPixels &&
                    one(newReady, newSerial, Kind::WorkerResultReady).sequence <
                        one(newReady, newSerial, Kind::FinishedSignalObserved).sequence &&
                    count(newReady, newSerial, Kind::TextureFactoryCalled) == 1 &&
                    one(newReady, newSerial, Kind::TextureFactoryCalled).sequence >
                        one(newReady, newSerial, Kind::FinishedSignalObserved).sequence &&
                    count(newReady, newSerial, Kind::Cancelled) == 0,
                "RACE002 NEW actual response did not consume successful natural saved revision "
                "before OLD release");
        d.wait(
            [&] {
                auto *card = filmstripCard(c, b);
                auto *image = imageFor(card);
                return card && image && image->property("status").toInt() == 1;
            },
            "RACE002 unchanged B actual thumbnail Ready", 5000);
        const auto stillHeld = [&] { held(session, old, expectedRequestSize, false); };
        racePixels.append(cardPixels(c, filmstripCard(c, edited), edited, expectedURL, true, true,
                                     "A_NEW", references, stillHeld));
        // B is still generation0: refreshThumbnails increments the global counter but only notifies
        // A.
        racePixels.append(cardPixels(c, filmstripCard(c, b), b, url(b), false, false, "B_CONTROL",
                                     references, stillHeld));
        rendered(c, edited, editedPreview);
        const auto beforeRelease = held(session, old, expectedRequestSize, false);
        editorState(c, edited, true, false);
        durable(c, pins, edited, journalBefore);
        require(resets->count() == 0 && opens->count() == 0 && loads->count() == 0 &&
                    bSources->count() == 0,
                "RACE002 unrelated reset/open/B-source change appeared before release");
        const auto viewSize = d.item("develop.filmstrip")->size();
        const auto viewOrigin = d.item("develop.filmstrip")->mapToScene(QPointF{});
        const double contentX = d.item("develop.filmstrip")->property("contentX").toDouble();
        writeJson(c.root + "/race002-new-ready-old-held.json",
                  {{"trace", traceJSON(beforeRelease)},
                   {"new_actual_serial", QString::number(newSerial)},
                   {"invalidation", invalidations->json()},
                   {"pixels", racePixels},
                   {"pending_completion_not_disk_io", true}});
        session.release();
        d.wait(
            [&] {
                const auto value = session.snapshot();
                return value.diagnosticFailure ||
                       (count(value, value.gatedSerial, Kind::FinishedSignalObserved) == 1 &&
                        count(value, value.gatedSerial, Kind::Destroyed) == 1);
            },
            "RACE002 OLD genuine finished/destruction after explicit gate release", 10000);
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
            "RACE002 OLD terminal evidence is stale, failed or out of order");
        const int cancelled = count(terminal, terminal.gatedSerial, Kind::Cancelled);
        require(
            cancelled <= 1 &&
                ((cancelled == 1 &&
                  one(terminal, terminal.gatedSerial, Kind::Cancelled).sequence <
                      one(terminal, terminal.gatedSerial, Kind::FinishedSignalObserved).sequence &&
                  count(terminal, terminal.gatedSerial, Kind::TextureFactoryCalled) == 0) ||
                 (cancelled == 0 &&
                  count(terminal, terminal.gatedSerial, Kind::TextureFactoryCalled) == 1)),
            "RACE002 actual OLD cancel/consume cleanup branch inconsistent");
        d.frame();
        finalPixels.append(cardPixels(c, filmstripCard(c, edited), edited, expectedURL, true, true,
                                      "A_NEW", references));
        finalPixels.append(
            cardPixels(c, filmstripCard(c, b), b, url(b), false, false, "B_CONTROL", references));
        rendered(c, edited, editedPreview);
        editorState(c, edited, true, false);
        durable(c, pins, edited, journalBefore);
        invalidations->requireSingle(id(old));
        const auto finalTrace = session.snapshot();
        require(!finalTrace.diagnosticFailure && !finalTrace.gateTimedOut &&
                    count(finalTrace, finalTrace.gatedSerial, Kind::FinishedSignalObserved) == 1 &&
                    count(finalTrace, finalTrace.gatedSerial, Kind::Destroyed) == 1 &&
                    requestsFor(finalTrace, expectedURL).size() == 1 &&
                    count(finalTrace, newSerial, Kind::FinishedSignalObserved) == 1 &&
                    count(finalTrace, newSerial, Kind::TextureFactoryCalled) == 1 &&
                    count(finalTrace, newSerial, Kind::Cancelled) == 0 && resets->count() == 0 &&
                    opens->count() == 0 && loads->count() == 0 && bSources->count() == 0 &&
                    allSaves.count() == 1 &&
                    c.window->effectiveDevicePixelRatio() == devicePixelRatio &&
                    d.item("develop.filmstrip")->size() == viewSize &&
                    d.item("develop.filmstrip")->mapToScene(QPointF{}) == viewOrigin &&
                    d.item("develop.filmstrip")->property("contentX").toDouble() == contentX,
                "RACE002 late completion altered current binding/viewport/observation counts");
        writeJson(c.root + "/race002-race-terminal.json",
                  {{"trace", traceJSON(finalTrace)},
                   {"new_actual_serial", QString::number(newSerial)},
                   {"processed_old_cancel", cancelled == 1},
                   {"old_gui_reply_claimed", false},
                   {"before_release_pixels", racePixels},
                   {"after_release_pixels", finalPixels},
                   {"invalidation", invalidations->json()},
                   {"expected_document", pinJSON(edited)}});
        // A single bounded Undo/Redo validates the one genuine gesture's history unit AFTER
        // terminal race evidence.
        Pin undo = old;
        undo.saved.revision = edited.saved.revision + 1;
        d.click("main.undoButton");
        naturalSave(c, undo);
        rendered(c, undo, oldPreview);
        editorState(c, undo, false, true);
        durable(c, pins, undo, journalBefore);
        const auto undoURL = generation(undo, 2);
        d.wait(
            [&] {
                auto *card = filmstripCard(c, undo);
                auto *image = imageFor(card);
                return card && image && image->property("status").toInt() == 1 &&
                       image->property("source").toUrl() == undoURL;
            },
            "RACE002 single Undo natural thumbnail Ready", 10000);
        historyPixels.append(
            cardPixels(c, filmstripCard(c, undo), undo, undoURL, true, true, "A_OLD", references));
        Pin redo = edited;
        redo.saved.revision = undo.saved.revision + 1;
        d.click("main.redoButton");
        naturalSave(c, redo);
        rendered(c, redo, editedPreview);
        editorState(c, redo, true, false);
        durable(c, pins, redo, journalBefore);
        const auto redoURL = generation(redo, 3);
        d.wait(
            [&] {
                auto *card = filmstripCard(c, redo);
                auto *image = imageFor(card);
                return card && image && image->property("status").toInt() == 1 &&
                       image->property("source").toUrl() == redoURL;
            },
            "RACE002 single Redo natural thumbnail Ready", 10000);
        historyPixels.append(
            cardPixels(c, filmstripCard(c, redo), redo, redoURL, true, true, "A_NEW", references));
        historyPixels.append(
            cardPixels(c, filmstripCard(c, b), b, url(b), false, false, "B_CONTROL", references));
        invalidations->requireCount(id(old), 3);
        editorState(c, redo, true, false);
        durable(c, pins, redo, journalBefore);
        require(
            allSaves.count() == 3 && allSaves.at(1).at(0).toString() == id(old) &&
                allSaves.at(2).at(0).toString() == id(old) && resets->count() == 0 &&
                opens->count() == 0 && loads->count() == 0 && bSources->count() == 0 &&
                !session.snapshot().diagnosticFailure,
            "RACE002 bounded Undo/Redo leaked unrelated saves/reset/selection/B response state");
        writeJson(c.root + "/race002-actual-response-summary.json",
                  {{"candidate", identifier},
                   {"race_terminal_trace", traceJSON(finalTrace)},
                   {"history_final_trace", traceJSON(session.snapshot())},
                   {"baseline_old_cache", baselineOldCache},
                   {"baseline_b_pixels", baselineBPixels},
                   {"race_before_release_pixels", racePixels},
                   {"race_after_release_pixels", finalPixels},
                   {"single_undo_redo_pixels", historyPixels},
                   {"race_saved_revision", QString::number(edited.saved.revision)},
                   {"final_redo_revision", QString::number(redo.saved.revision)},
                   {"history_invalidation_observations", invalidations->json()},
                   {"baseline_journal", journalBefore},
                   {"boundary", "actual current unfinished QML thumbnail invalidated by natural "
                                "saved edit; successful pending completion, not active disk I/O"},
                   {"scope", "Qt/QTest window input; sampled Image-node frames; shared "
                             "CPU/ICC/JPEG reference path; no hardware/physical-scanout claim"}});
        return true;
    } catch (...) {
        // BEFORE pointer/Session cleanup so actual original held/pressed state is retained.
        try {
            writeJson(
                c.root + "/race002-original-failure-before-cleanup.json",
                {{"trace", traceJSON(session.snapshot())},
                 {"pointer_down", pointer.down},
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
                 {"actual_saving", c.editor.saving()},
                 {"actual_photo_uuid", c.editor.photoID()},
                 {"old_card_present", !oldCard.isNull()},
                 {"old_image_present", !oldImage.isNull()},
                 {"old_image_source",
                  oldImage ? oldImage->property("source").toUrl().toString(QUrl::FullyEncoded)
                           : QString()},
                 {"old_image_status", oldImage ? oldImage->property("status").toInt() : -1},
                 {"invalidation", invalidations ? invalidations->json() : QJsonObject()},
                 {"natural_saves", allSaves.count()},
                 {"baseline_old_cache", baselineOldCache},
                 {"baseline_b_pixels", baselineBPixels},
                 {"race_pixels", racePixels},
                 {"after_release_pixels", finalPixels},
                 {"history_pixels", historyPixels},
                 {"press_sequence", QString::number(pressSequence)}});
        } catch (...) {
            std::fputs("RACE002 failure evidence failed; original exception retained\n", stderr);
        }
        throw;
    }
}
} // namespace ux
