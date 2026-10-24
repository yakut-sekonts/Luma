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
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaProperty>
#include <QPointer>
#include <QQuickItemGrabResult>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QUrlQuery>
#include <algorithm>
#include <array>
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
#error "RACE007 candidate requires the real response observation seam"
#endif

// Genuine partial OLD render across one real UI batch transaction; official and historical IDs.
namespace ux {
namespace {
namespace trace = thumbnail_trace;
constexpr auto candidateID = "CANDIDATE-RACE007-PARTIAL-THUMBNAIL";
constexpr auto officialID = "UX-RACE-007";
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
    require(file.open(QIODevice::ReadOnly), "Cannot hash owned RACE007 file");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file), "Cannot read complete owned RACE007 file");
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
        luma::encodePreset({"RACE007 pinned settings", {}, asset.edits, luma::allSettingsGroups}));
    require(encoded.isObject(), "Cannot encode RACE007 preset-schema diagnostic settings");
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
QUrl generation(const Pin &pin, int value) {
    return QUrl("image://thumbnail/" + id(pin) + "?generation=" + QString::number(value));
}
QUrl url(const Pin &pin) {
    // The one setup C autosave increments global invalidation even while the filter is empty.
    return generation(pin, 1);
}
QImage expectedJPEG(const luma::AssetSnapshot &saved) {
    const auto decoded = luma::PhotoDecoder().decode(saved.asset.path);
    require(decoded.image.width == 256 && decoded.image.height == 192 &&
                decoded.image.alpha.empty(),
            "RACE007 requires small opaque generated PNGs");
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
const char *cpuName(trace::CpuStage value) {
    switch (value) {
    case trace::CpuStage::Entered:
        return "Entered";
    case trace::CpuStage::FirstRowCompleted:
        return "FirstRowCompleted";
    case trace::CpuStage::Returned:
        return "Returned";
    case trace::CpuStage::Aborted:
        return "Aborted";
    }
    return "Unknown";
}
QJsonObject traceJSON(const trace::Snapshot &value) {
    QJsonArray requests, events, cpu;
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
    for (const auto &event : value.cpuRenders)
        cpu.append(QJsonObject{
            {"sequence", QString::number(event.sequence)},
            {"serial", QString::number(event.serial)},
            {"stage", cpuName(event.stage)},
            {"revision", QString::number(event.revision)},
            {"thread_id", QString::number(event.threadID, 16)},
            {"width", event.width},
            {"height", event.height},
            {"completed_rows", event.completedRows},
            {"completed_row_rgba_hex", QString::fromLatin1(event.completedRowRGBA.toHex())},
            {"completed_row_sha256",
             QString::fromLatin1(
                 QCryptographicHash::hash(event.completedRowRGBA, QCryptographicHash::Sha256)
                     .toHex())}});
    return {{"diagnostic_failure", value.diagnosticFailure},
            {"gate_entered", value.gateEntered},
            {"gate_timed_out", value.gateTimedOut},
            {"release_requested", value.releaseRequested},
            {"session_closing", value.sessionClosing},
            {"gated_serial", QString::number(value.gatedSerial)},
            {"requests", requests},
            {"events", events},
            {"cpu_renders", cpu}};
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
    std::uint64_t result = 0;
    for (const auto &event : value.events)
        result = std::max(result, event.sequence);
    for (const auto &event : value.cpuRenders)
        result = std::max(result, event.sequence);
    return result;
}
std::vector<trace::Request> requestsFor(const trace::Snapshot &value, const QUrl &source) {
    std::vector<trace::Request> result;
    for (const auto &request : value.requests)
        if (request.fullURL == source)
            result.push_back(request);
    return result;
}
int cpuCount(const trace::Snapshot &value, std::uint64_t serial, trace::CpuStage stage) {
    return static_cast<int>(
        std::count_if(value.cpuRenders.begin(), value.cpuRenders.end(), [=](const auto &event) {
            return event.serial == serial && event.stage == stage;
        }));
}
const trace::CpuRenderEvent &cpuOne(const trace::Snapshot &value, std::uint64_t serial,
                                    trace::CpuStage stage) {
    require(cpuCount(value, serial, stage) == 1, "RACE007 exact actual CPU stage absent/ambiguous");
    return *std::find_if(value.cpuRenders.begin(), value.cpuRenders.end(), [=](const auto &event) {
        return event.serial == serial && event.stage == stage;
    });
}
constexpr std::array<std::array<int, 3>, 3> literalColors{{{32, 2, 2}, {2, 32, 2}, {2, 2, 32}}};
QByteArray literalRow() {
    QByteArray row;
    row.reserve(256 * 4);
    for (int x = 0; x < 256; ++x) {
        for (const int value : literalColors[0])
            row.append(static_cast<char>(value));
        row.append(static_cast<char>(255));
    }
    return row;
}
struct RowReference {
    QString label;
    QByteArray rgba;
};
using Rows = std::vector<RowReference>;
QByteArray referenceRow(const Pin &pin) {
    // All documents are immutable literal expectations, computed before actual UI edits/Session.
    const auto decoded = luma::PhotoDecoder().decode(pin.saved.asset.path);
    const auto small = luma::standardPreview(decoded.image, 384);
    const auto image = luma::renderCPU(
        small, pin.saved.asset.edits, pin.saved.asset.metadata.orientation, {}, {},
        decoded.calibration, luma::DetailDiagnostic::none, 1, pin.saved.asset.metadata);
    require(image.width == 256 && image.height == 192 && image.rgba.size() == 256 * 192 * 4,
            "RACE007 immutable RAW row reference changed plain source/output mapping");
    return QByteArray(reinterpret_cast<const char *>(image.rgba.data()), 256 * 4);
}
double rowDistance(const QByteArray &a, const QByteArray &b) {
    require(a.size() == 256 * 4 && b.size() == a.size(),
            "RACE007 RAW row controls must be complete independent opaque256-pixel rows");
    double difference = 0;
    for (qsizetype i = 0; i < a.size(); i += 4) {
        require(static_cast<unsigned char>(a.at(i + 3)) == 255 &&
                    static_cast<unsigned char>(b.at(i + 3)) == 255,
                "RACE007 RAW reference prefix unexpectedly has transparency");
        for (qsizetype channel = 0; channel < 3; ++channel)
            difference += std::abs(int(static_cast<unsigned char>(a.at(i + channel))) -
                                   int(static_cast<unsigned char>(b.at(i + channel))));
    }
    return difference / (256.0 * 3);
}
QJsonArray rowsJSON(const Rows &rows) {
    QJsonArray result;
    for (const auto &row : rows)
        result.append(QJsonObject{
            {"label", row.label},
            {"rgba_hex", QString::fromLatin1(row.rgba.toHex())},
            {"sha256",
             QString::fromLatin1(
                 QCryptographicHash::hash(row.rgba, QCryptographicHash::Sha256).toHex())}});
    return result;
}
void validateRowControls(const QByteArray &actual, const Rows &rows) {
    require(rows.size() == 6 && actual == literalRow(),
            "RACE007 actual partial prefix is not independently literal OLD RGBA");
    int own = 0;
    for (const auto &reference : rows) {
        const double distance = rowDistance(actual, reference.rgba);
        if (reference.label == "A_OLD") {
            ++own;
            require(distance == 0 && reference.rgba == literalRow(),
                    "RACE007 original numerical row control does not equal literal OLD source");
        } else {
            require(distance > 12,
                    "RACE007 actual partial RAW row matches NEW/wrong source or lacks margin");
        }
    }
    require(own == 1, "RACE007 expected RAW OLD positive control absent/ambiguous");
}
QJsonObject rowControls(const trace::Snapshot &value, const Rows &rows) {
    const auto &event = cpuOne(value, value.gatedSerial, trace::CpuStage::FirstRowCompleted);
    validateRowControls(event.completedRowRGBA, rows);
    QJsonArray negatives;
    for (const auto &reference : rows)
        if (reference.label != "A_OLD")
            negatives.append(QJsonObject{{"label", reference.label},
                                         {"mean_abs_rgb_byte_distance",
                                          rowDistance(event.completedRowRGBA, reference.rgba)}});
    return {{"exact_literal_old_rgba", true},
            {"completed_rows", event.completedRows},
            {"own_raw_mean_abs_rgb_error", 0},
            {"negative_raw_rows", negatives}};
}
trace::Snapshot held(trace::Session &session, const Pin &old, const QSize &size, bool uncancelled,
                     const Rows &rows) {
    const auto value = session.snapshot();
    require(!value.diagnosticFailure && value.gateEntered && !value.gateTimedOut &&
                !value.releaseRequested && !value.sessionClosing && value.gatedSerial,
            "RACE007 actual partially-computed OLD row gate is not held");
    const auto requests = requestsFor(value, url(old));
    require(requests.size() == 1 && requests.front().serial == value.gatedSerial &&
                requests.front().requestedSize == size &&
                requests.front().providerRequestID == id(old) + "?generation=1",
            "RACE007 OLD exact current-source physical tuple missing/retried/ambiguous");
    const auto serial = value.gatedSerial;
    const auto &created = one(value, serial, Kind::Created);
    const auto &read = one(value, serial, Kind::SnapshotRead);
    const auto &entered = cpuOne(value, serial, trace::CpuStage::Entered);
    const auto &row = cpuOne(value, serial, trace::CpuStage::FirstRowCompleted);
    const auto &gate = one(value, serial, Kind::GateEntered);
    require(read.revision == 0 && entered.revision == 0 && row.revision == 0 &&
                gate.revision == 0 && row.width == 256 && row.height == 192 &&
                row.completedRows == 1 && row.completedRowRGBA == literalRow() &&
                created.sequence < read.sequence && read.sequence < entered.sequence &&
                entered.sequence < row.sequence && row.sequence < gate.sequence &&
                read.threadID == entered.threadID && entered.threadID == row.threadID &&
                row.threadID == gate.threadID &&
                row.threadID != reinterpret_cast<quintptr>(QThread::currentThreadId()) &&
                cpuCount(value, serial, trace::CpuStage::Returned) == 0 &&
                cpuCount(value, serial, trace::CpuStage::Aborted) == 0 &&
                count(value, serial, Kind::WorkerResultReady) == 0 &&
                count(value, serial, Kind::GateReleased) == 0 &&
                count(value, serial, Kind::GateSkipped) == 0 &&
                count(value, serial, Kind::GateTimedOut) == 0 &&
                count(value, serial, Kind::FinishedSignalObserved) == 0 &&
                count(value, serial, Kind::TextureFactoryCalled) == 0 &&
                count(value, serial, Kind::Destroyed) == 0 &&
                count(value, serial, Kind::Cancelled) <= 1 &&
                (!uncancelled || count(value, serial, Kind::Cancelled) == 0),
            "RACE007 OLD is not actual neutral first-row partial render, or already completed");
    validateRowControls(row.completedRowRGBA, rows);
    return value;
}
QQuickItem *imageFor(QQuickItem *card) {
    return card ? card->findChild<QQuickItem *>("photoThumbnail") : nullptr;
}
QQuickItem *gridCard(Context &c, const Pin &pin) {
    return qobject_cast<QQuickItem *>(c.driver->find("library.card." + id(pin)));
}
void identity(Context &c, QQuickItem *card, const Pin &pin, const QUrl &source, int status,
              bool selected, bool active) {
    auto *view = c.driver->item("library.grid");
    auto *image = imageFor(card);
    const int row = card ? card->property("index").toInt() : -1;
    require(card && image && card->isVisible() && image->isVisible() && view->isVisible() &&
                view->boundingRect().contains(card->mapRectToItem(view, card->boundingRect())) &&
                c.window->contentItem()->boundingRect().contains(
                    card->mapRectToScene(card->boundingRect())) &&
                image->width() > 0 && image->height() > 0 &&
                image->property("sourceSize").toSize() == QSize(384, 384) && row >= 0 && row < 3 &&
                card->property("assetID").toString() == id(pin) &&
                c.library.data(c.library.index(row), LibraryModel::AssetID).toString() == id(pin) &&
                c.library.data(c.library.index(row), LibraryModel::Path).toUrl().toLocalFile() ==
                    path(pin) &&
                c.library.data(c.library.index(row), LibraryModel::Thumbnail).toUrl() == source &&
                card->property("thumbnailURL").toUrl() == source &&
                image->property("source").toUrl() == source &&
                image->property("status").toInt() == status &&
                card->property("retryCount").toInt() == 0 &&
                card->property("selected").toBool() == selected &&
                card->property("activePhoto").toBool() == active &&
                c.library.data(c.library.index(row), LibraryModel::Selected).toBool() == selected &&
                c.library.data(c.library.index(row), LibraryModel::Active).toBool() == active &&
                card->property("filename").toString().toStdString() == pin.row.filename &&
                card->property("rating").toInt() == pin.row.rating &&
                card->property("flagValue").toInt() == pin.row.flag &&
                card->property("colorLabel").toString().toStdString() == pin.row.label,
            "RACE007 actual fully-visible card/model/source/selection/metadata identity mismatch");
}
double meanRGB(const QImage &a, const QImage &b, int border = 4) {
    require(a.size() == b.size() && a.width() > 2 * border && a.height() > 2 * border,
            "Non-comparable RACE007 actual pixel region");
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
QJsonObject cardPixels(Context &c, const Pin &pin, const QUrl &source, bool selected, bool active,
                       const QString &ownLabel, const QString &stage,
                       const std::vector<Reference> &references,
                       const std::function<void()> &checkpoint = {}) {
    auto *card = gridCard(c, pin);
    identity(c, card, pin, source, 1, selected, active);
    QPointer<QQuickItem> cardGuard(card), imageGuard(imageFor(card));
    const auto geometry = imageGuard->size();
    const auto cached = QFileInfo(cachePath(c, pin));
    require(cached.isFile() && !cached.isSymLink() &&
                pixels(QImage(cached.filePath())) == pin.jpegPixels,
            "RACE007 actual saved cache differs from independent original/expected-document JPEG");
    // The shared UX helper waits for actual GUI ready and retains any failed
    // pending result through Context.engine/window teardown before releasing it.
    const auto capture = readyItemGrab(c, *imageGuard.data(),
                                       "RACE007 actual thumbnail Image-node ready signal", 5000);
    require(cardGuard && imageGuard && !capture->image().isNull() &&
                imageFor(cardGuard.data()) == imageGuard.data() && imageGuard->size() == geometry,
            "RACE007 current Image rebound during actual ready-acknowledged readback");
    identity(c, cardGuard.data(), pin, source, 1, selected, active);
    if (checkpoint)
        checkpoint();
    const auto actual = pixels(capture->image());
    const auto artifact = c.root + "/race007-node-" + stage + "-" + id(pin) + ".png";
    require(actual.save(artifact), "Cannot save RACE007 actual Image-node pixels");
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
        require(wrong.size() == own.size(), "RACE007 reference aspect ratios differ");
        const double separation = meanRGB(own, wrong), mismatch = meanRGB(cropped, wrong);
        require(separation > 12,
                "RACE007 OLD/NEW/control references indistinguishable at actual capture size");
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
        "RACE007 current thumbnail shows a stale revision/control or misses its expected pixels");
    const QJsonObject result{{"uuid", id(pin)},
                             {"stage", stage},
                             {"node_png", artifact},
                             {"node_png_sha256", QString::fromLatin1(digest(artifact).toHex())},
                             {"actual_grab_ready_acknowledged", true},
                             {"source", source.toString(QUrl::FullyEncoded)},
                             {"expected_label", ownLabel},
                             {"mean_rgb_own_error", ownError},
                             {"closest_wrong_label", closestLabel},
                             {"mean_rgb_closest_wrong", closestWrong},
                             {"negative_references", negatives}};
    auto action = result;
    action.insert("action", "race007_independent_thumbnail_pixels");
    c.driver->log(action);
    return result;
}
QJsonArray readRows(Context &c, const char *query) {
    sqlite3 *opened = nullptr;
    const int result =
        sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &opened, SQLITE_OPEN_READONLY, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(opened, sqlite3_close);
    require(result == SQLITE_OK && db, "Cannot open RACE007 read-only journal observation");
    sqlite3_busy_timeout(db.get(), 1000);
    sqlite3_stmt *prepared = nullptr;
    const int preparedResult = sqlite3_prepare_v2(db.get(), query, -1, &prepared, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(prepared,
                                                                               sqlite3_finalize);
    require(preparedResult == SQLITE_OK && statement,
            "Cannot prepare RACE007 read-only journal observation");
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
    require(step == SQLITE_DONE, "Cannot complete RACE007 read-only journal observation");
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
QJsonObject facts(Context &c) {
    return {{"assets", readRows(c, "SELECT * FROM assets ORDER BY filename,id")},
            {"edit_documents", readRows(c, "SELECT * FROM edit_documents ORDER BY asset_id")},
            {"edit_curve_points",
             readRows(c, "SELECT * FROM edit_curve_points ORDER BY asset_id,channel,position")},
            {"edit_hsl_bands", readRows(c, "SELECT * FROM edit_hsl_bands ORDER BY asset_id,band")},
            {"edit_local_masks", readRows(c, "SELECT * FROM edit_local_masks ORDER BY asset_id")},
            {"collections", readRows(c, "SELECT * FROM collections ORDER BY id")},
            {"collection_assets",
             readRows(c, "SELECT * FROM collection_assets ORDER BY collection_id,asset_id")},
            {"journal", journal(c)}};
}
QJsonArray docsJSON(const Pins &pins) {
    QJsonArray result;
    for (const auto &pin : pins)
        result.append(pinJSON(pin));
    return result;
}
void durable(Context &c, const Pins &baseline, const Pins &expected,
             const QJsonObject &journalBefore, const QJsonObject &initialFacts) {
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto rows = catalog.libraryPage({}, 0, 3);
    require(baseline.size() == 3 && expected.size() == 3 && rows.size() == 3 &&
                catalog.libraryCount() == 3,
            "RACE007 exact three-source membership changed");
    const auto metadata = [](const luma::PhotoMetadata &m) {
        return std::tie(m.camera, m.lens, m.iso, m.shutterSeconds, m.aperture, m.focalLength,
                        m.captureTimestamp, m.width, m.height, m.orientation);
    };
    const auto fields = [](const luma::LibraryItem &r) {
        return std::tie(r.id, r.path, r.filename, r.checksum, r.width, r.height, r.orientation,
                        r.rating, r.flag, r.label, r.camera);
    };
    for (std::size_t i = 0; i < 3; ++i) {
        const auto &pin = expected[i];
        const auto saved = catalog.assetSnapshot(pin.saved.asset.id);
        require(saved && fields(rows[i]) == fields(baseline[i].row) &&
                    saved->asset.id == pin.saved.asset.id &&
                    saved->asset.path == pin.saved.asset.path &&
                    saved->asset.checksum == pin.saved.asset.checksum &&
                    saved->revision == pin.saved.revision &&
                    saved->asset.edits == pin.saved.asset.edits &&
                    metadata(saved->asset.metadata) == metadata(pin.saved.asset.metadata) &&
                    digest(path(pin)).toHex().toStdString() == pin.saved.asset.checksum,
                "RACE007 full typed document/revision/row/metadata/source SHA mismatch");
    }
    const auto active = catalog.activeAsset();
    require(active && active->id == expected[2].saved.asset.id &&
                active->path == expected[2].saved.asset.path &&
                active->edits == expected[2].saved.asset.edits && journal(c) == journalBefore,
            "RACE007 changed durable active C/session/completed Import journal");
    const auto current = facts(c);
    for (const auto *table : {"assets", "collections", "collection_assets"})
        require(current[table] == initialFacts[table],
                "RACE007 immutable complete SQL table changed");
    catalog.verify();
}
void editorState(Context &c, const Pin &active, const QImage &preview,
                 const std::vector<std::string> &selection) {
    require(
        c.editor.hasPhoto() && c.editor.photoID() == id(active) &&
            c.editor.sourcePath() == path(active) &&
            c.editor.currentSettings() == active.saved.asset.edits && c.editor.canUndo() &&
            !c.editor.canRedo() && !c.editor.uxHistoryGestureActive() && !c.editor.saving() &&
            !c.editor.busy() && !c.editor.transformAutoBusy() && !c.editor.localGestureActive() &&
            !c.editor.localSampling() && !c.editor.localMode() && !c.editor.cropMode() &&
            !c.editor.guidedMode() && !c.editor.exporting() && !c.editor.presetPreview() &&
            !c.editor.original() && c.editor.error().isEmpty() && !c.batch.working() &&
            c.batch.error().isEmpty() && !c.exporter.working() && !c.library.working() &&
            !c.library.loading() && !c.library.selecting() && c.library.error().isEmpty() &&
            c.library.count() == 3 && c.library.selectionIDs() == selection &&
            c.library.selectedCount() == static_cast<int>(selection.size()) &&
            c.window->isActive() && c.window->isExposed() &&
            c.window->property("libraryMode").toBool() &&
            c.driver->object("library.search")->property("text").toString().isEmpty() &&
            c.driver->object("library.ratingFilter")->property("currentIndex").toInt() == 0 &&
            c.driver->object("libraryFlagFilter")->property("currentIndex").toInt() == 0 &&
            c.driver->object("libraryLabelFilter")->property("currentIndex").toInt() == 0 &&
            c.driver->object("library.pane")->property("currentCollection").toString().isEmpty() &&
            c.driver->item("develop.preview")->property("status").toInt() == 1 &&
            c.driver->item("develop.preview")->property("source").toUrl().toString() ==
                c.editor.previewUrl() &&
            c.provider->requestImage({}, nullptr, {}) == preview,
        "RACE007 active unselected C/history/preview/selection/filter/job state changed");
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
                   "RACE007 actual main Image Ready/current source/expected full-document pixels",
                   10000);
    c.driver->frame();
    require(ready() && c.editor.photoID() == id(a) &&
                c.editor.currentSettings() == a.saved.asset.edits,
            "RACE007 current preview/document did not remain correct through polish");
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
        "RACE007 natural exact full-document/revision autosave", 10000);
    require(c.editor.error().isEmpty(), c.editor.error());
}
Pins importHidden(Context &c) {
    auto &d = *c.driver;
    require(!c.editor.hasPhoto() && c.library.count() == 0 &&
                luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 0,
            "RACE007 needs a fresh isolated empty catalog");
    QSignalSpy searchResets(&c.library, &QAbstractItemModel::modelReset);
    require(searchResets.isValid(), "Cannot observe actual no-match filter reload");
    d.type("library.search", "race007-no-such-filter-8ff23a");
    d.wait(
        [&] { return searchResets.count() >= 2 && !c.library.loading() && c.library.count() == 0; },
        "actual no-match search fully reloaded");
    const auto directory = c.root + "/owned-race007-originals";
    require(QDir().mkpath(directory), "Cannot create own RACE007 source directory");
    const auto names = QDir(c.fixture).entryList({"Photo-*.png"}, QDir::Files, QDir::Name);
    require(names.size() == 3, "RACE007 requires exactly three private generated RGB PNG fixtures");
    QList<QUrl> sources;
    for (const auto &name : names) {
        const auto target = directory + '/' + name;
        require(QFile::copy(c.fixture + '/' + name, target) &&
                    digest(target) == digest(c.fixture + '/' + name),
                "Cannot copy identical private RACE007 source");
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
        "actual three-source Add behind no-match filter", 30000);
    d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
           "Import dialog closed");
    require(c.library.error().isEmpty(), c.library.error());
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto rows = catalog.libraryPage({}, 0, 3);
    require(rows.size() == 3 && catalog.libraryCount() == 3,
            "RACE007 Add did not commit exact three-source membership");
    Pins pins;
    int literalIndex = 0;
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
            "RACE007 imported baseline is not independently neutral/pinned");
        const auto original = pixels(QImage(QString::fromStdString(row.path.string())));
        require(original.size() == QSize(256, 192),
                "RACE007 literal RGB source dimensions invalid");
        const auto color = literalColors[static_cast<std::size_t>(literalIndex++)];
        for (int y = 0; y < original.height(); ++y)
            for (int x = 0; x < original.width(); ++x) {
                const auto value = original.pixelColor(x, y);
                require(value.red() == color[0] && value.green() == color[1] &&
                            value.blue() == color[2] && value.alpha() == 255,
                        "RACE007 generated opaque literal RGB source changed");
            }
        pins.push_back({*saved, row, expectedJPEG(*saved)});
        d.frame();
    }
    require(!d.find("library.card." + id(pins[0])) && !d.find("filmstrip.card." + id(pins[0])) &&
                !d.find("library.card." + id(pins[1])) &&
                !d.find("filmstrip.card." + id(pins[1])) &&
                !d.find("library.card." + id(pins[2])) && !d.find("filmstrip.card." + id(pins[2])),
            "RACE007 pre-Session cards exist despite no-match search");
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
            "RACE007 actual FileDialog rejected declared selected-file result");
    d.log({{"action", "file_dialog_selection_fixture_boundary"},
           {"id", "main.openDialog"},
           {"path", path(a)},
           {"native_selection_exercised", false},
           {"all_cards_hidden", true}});
    require(QMetaObject::invokeMethod(dialog, "close", Qt::DirectConnection) &&
                QMetaObject::invokeMethod(dialog, "accepted", Qt::DirectConnection),
            "RACE007 FileDialog production close/accepted handler unavailable");
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
            "RACE007 Open setup changed neutral document/history/selection");
}
struct ReleasePointer {
    Context &c;
    QPoint last;
    bool down = false;
    bool cancelButton = false;
    ~ReleasePointer() noexcept {
        if (down)
            try {
                // Failure cleanup must not accidentally click a held Paste button.
                // Evidence is captured before this real move/release cancellation.
                if (cancelButton) {
                    const QPoint outsideButton(1, 1);
                    QTest::mouseMove(c.window, outsideButton);
                    QTest::mouseRelease(c.window, Qt::LeftButton, Qt::NoModifier, outsideButton);
                } else {
                    QTest::mouseRelease(c.window, Qt::LeftButton, Qt::NoModifier, last);
                }
            } catch (...) {
                std::fputs("RACE007 pointer cleanup failed\n", stderr);
            }
    }
};
// Passive callbacks capture only observations and never throw through Qt.
class BatchAudit {
  public:
    BatchAudit(Context &context, trace::Session &session) : c_(context), session_(session) {
        connection_ =
            QObject::connect(&c_.batch, &BatchController::changed, &lifetime_, [this]() noexcept {
                try {
                    const bool current = c_.batch.working();
                    if (current && !working_)
                        ++starts_;
                    if (!current && working_)
                        ++ends_;
                    working_ = current;
                    observations_.append(
                        QJsonObject{{"working", current},
                                    {"waiting_for_save", c_.batch.waitingForSave()},
                                    {"progress", c_.batch.progress()},
                                    {"error", c_.batch.error()},
                                    {"can_undo", c_.batch.canUndo()},
                                    {"can_redo", c_.batch.canRedo()},
                                    {"trace", traceJSON(session_.snapshot())}});
                } catch (...) {
                    failed_ = true;
                }
            });
        require(static_cast<bool>(connection_),
                "Cannot observe genuine BatchController transitions");
    }
    ~BatchAudit() {
        QObject::disconnect(connection_);
    }
    void requireOperations(int count) const {
        require(!failed_ && !working_ && starts_ == count && ends_ == count,
                "RACE007 one actual start/terminal per semantic batch operation absent");
    }
    QJsonObject json() const {
        return {{"starts", starts_},
                {"ends", ends_},
                {"observation_failed", failed_},
                {"observations", observations_}};
    }

  private:
    Context &c_;
    trace::Session &session_;
    QObject lifetime_;
    QMetaObject::Connection connection_;
    bool working_ = false, failed_ = false;
    int starts_ = 0, ends_ = 0;
    QJsonArray observations_;
};
class InvalidationAudit {
  public:
    InvalidationAudit(Context &c, trace::Session &session) {
        connection_ = QObject::connect(
            &c.library, &QAbstractItemModel::dataChanged, &lifetime_,
            [this, &c, &session](const QModelIndex &first, const QModelIndex &last,
                                 const QList<int> &roles) noexcept {
                if (!roles.contains(LibraryModel::Thumbnail))
                    return;
                try {
                    require(roles == QList<int>{LibraryModel::Thumbnail} && first == last,
                            "RACE007 unexpected invalidation role/range");
                    const auto asset = c.library.data(first, LibraryModel::AssetID).toString();
                    ids_.append(asset);
                    observations_.append(QJsonObject{
                        {"uuid", asset},
                        {"source",
                         c.library.data(first, LibraryModel::Thumbnail).toUrl().toString()},
                        {"trace", traceJSON(session.snapshot())}});
                } catch (...) {
                    failed_ = true;
                }
            });
        require(static_cast<bool>(connection_),
                "Cannot observe global actual thumbnail invalidation");
    }
    ~InvalidationAudit() {
        QObject::disconnect(connection_);
    }
    void requireOperations(const Pins &pins, int operations) const {
        QStringList expected;
        for (int n = 0; n < operations; ++n)
            for (const auto &pin : pins)
                expected.append(id(pin));
        require(!failed_ && ids_ == expected,
                "RACE007 global refresh did not notify exact three populated rows per batch");
    }
    QJsonObject json() const {
        return {{"ids", QJsonArray::fromStringList(ids_)},
                {"failed", failed_},
                {"observations", observations_}};
    }

  private:
    QObject lifetime_;
    QMetaObject::Connection connection_;
    bool failed_ = false;
    QStringList ids_;
    QJsonArray observations_;
};
class CStateAudit {
  public:
    CStateAudit(Context &c, const Pin &expected) {
        const auto uuid = id(expected);
        const auto settings = expected.saved.asset.edits;
        connection_ = QObject::connect(
            &c.editor, &EditorController::stateChanged, &lifetime_,
            [this, &c, uuid, settings]() noexcept {
                try {
                    const auto current = c.editor.currentSettings();
                    const bool valid = c.editor.photoID() == uuid && current == settings &&
                                       !c.editor.uxHistoryGestureActive() && c.editor.canUndo() &&
                                       !c.editor.canRedo();
                    failed_ = failed_ || !valid;
                    observations_.append(QJsonObject{
                        {"uuid", c.editor.photoID()},
                        {"full_document_equal", current == settings},
                        {"history_gesture_active", c.editor.uxHistoryGestureActive()},
                        {"can_undo", c.editor.canUndo()},
                        {"can_redo", c.editor.canRedo()},
                        {"settings",
                         QJsonDocument::fromJson(
                             luma::encodePreset(
                                 {"RACE007 actual C signal", {}, current, luma::allSettingsGroups}))
                             .object()},
                        {"local_masks_serialized",
                         QString::fromStdString(luma::encodeLocalMasks(current.localMasks))}});
                } catch (...) {
                    failed_ = true;
                }
            });
        require(static_cast<bool>(connection_),
                "Cannot observe retained actual C document/history");
    }
    ~CStateAudit() {
        QObject::disconnect(connection_);
    }
    void requireUnchanged() const {
        require(!failed_,
                "RACE007 active C document/history changed during an observed actual state signal");
    }
    QJsonObject json() const {
        return {{"failed", failed_}, {"observations", observations_}};
    }

  private:
    QObject lifetime_;
    QMetaObject::Connection connection_;
    bool failed_ = false;
    QJsonArray observations_;
};
std::unique_ptr<QSignalSpy> propertySpy(QObject *object, const char *name) {
    require(object, "RACE007 passive property target missing");
    const int index = object->metaObject()->indexOfProperty(name);
    require(index >= 0 && object->metaObject()->property(index).hasNotifySignal(),
            "RACE007 passive property notify signal unavailable");
    auto spy =
        std::make_unique<QSignalSpy>(object, object->metaObject()->property(index).notifySignal());
    require(spy->isValid(), "RACE007 invalid actual property observer");
    return spy;
}
struct BindingWatch {
    QPointer<QQuickItem> card, image;
    std::unique_ptr<QSignalSpy> source, asset, thumbnail;
    QJsonObject json() const {
        return {{"card_alive", !card.isNull()},
                {"image_alive", !image.isNull()},
                {"source_changes", source->count()},
                {"asset_changes", asset->count()},
                {"thumbnail_url_changes", thumbnail->count()}};
    }
    void requireUnchanged(Context &c, const Pin &pin) const {
        require(card && image && gridCard(c, pin) == card.data() &&
                    imageFor(card.data()) == image.data() && source->count() == 0 &&
                    asset->count() == 0 && thumbnail->count() == 0,
                "RACE007 late OLD terminal rebound/destroyed current source/UUID binding");
    }
};
QSet<QString> visibleIDs(Context &c) {
    QSet<QString> result;
    auto *view = c.driver->item("library.grid");
    auto *content =
        qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(view->property("contentItem")));
    require(content, "RACE007 actual grid contentItem unavailable");
    for (auto *card : content->childItems()) {
        const auto uuid = card->property("assetID").toString();
        if (!uuid.isEmpty() && card->isVisible() &&
            card->mapRectToItem(view, card->boundingRect()).intersects(view->boundingRect())) {
            require(!result.contains(uuid), "RACE007 duplicate actual visible UUID");
            result.insert(uuid);
        }
    }
    return result;
}
void waitReady(Context &c, const Pin &pin, int gen, const std::function<void()> &checkpoint = {}) {
    c.driver->wait(
        [&] {
            if (checkpoint)
                checkpoint();
            auto *card = gridCard(c, pin);
            auto *image = imageFor(card);
            return card && image && image->property("source").toUrl() == generation(pin, gen) &&
                   image->property("status").toInt() == 1;
        },
        "RACE007 actual current generation thumbnail Ready", 10000);
}
void checkNewResponse(const trace::Snapshot &value, const Pin &pin, int gen, const QSize &size,
                      std::uint64_t beforeInput) {
    const auto requests = requestsFor(value, generation(pin, gen));
    require(requests.size() == 1 && requests.front().requestedSize == size,
            "RACE007 NEW exact physical response tuple missing/retried/ambiguous");
    const auto serial = requests.front().serial;
    const auto &created = one(value, serial, Kind::Created);
    const auto &read = one(value, serial, Kind::SnapshotRead);
    const auto &result = one(value, serial, Kind::WorkerResultReady);
    const auto &finished = one(value, serial, Kind::FinishedSignalObserved);
    require(serial != value.gatedSerial && created.sequence > beforeInput &&
                created.sequence < read.sequence &&
                read.revision == static_cast<std::uint64_t>(pin.saved.revision) &&
                read.sequence < result.sequence && result.successfulPixels &&
                result.sequence < finished.sequence &&
                count(value, serial, Kind::TextureFactoryCalled) >= 1 &&
                std::all_of(
                    value.events.begin(), value.events.end(),
                    [=](const auto &event) {
                        return event.serial != serial || event.kind != Kind::TextureFactoryCalled ||
                               event.sequence > finished.sequence;
                    }) &&
                count(value, serial, Kind::Cancelled) == 0,
            "RACE007 genuine NEW revision1 success/finished/texture before OLD release absent");
}
void batchIdle(Context &c) {
    c.driver->wait(
        [&] { return !c.batch.working() && !c.library.working() && !c.library.loading(); },
        "RACE007 one actual batch transaction reached terminal", 10000);
    c.driver->wait([&] { return !c.driver->object("batch.wait")->property("visible").toBool(); },
                   "RACE007 actual batch modal closed", 5000);
    require(c.batch.error().isEmpty(), c.batch.error());
}
} // namespace

bool race007PartialRenderCandidate(Context &c, const QString &identifier) {
    if (identifier != candidateID && identifier != officialID)
        return false;
    auto &d = *c.driver;
    std::unique_ptr<trace::Session> session;
    ReleasePointer pointer{c, {}};
    std::unique_ptr<BatchAudit> batchAudit;
    std::unique_ptr<InvalidationAudit> invalidations;
    std::unique_ptr<CStateAudit> activeAudit;
    std::unique_ptr<QSignalSpy> resets, opens, loads;
    QSignalSpy saves(&c.editor, &EditorController::editsSaved);
    require(saves.isValid(), "RACE007 natural save observer unavailable");
    Pins baseline, before, applied, undo, redo;
    QJsonObject journalBefore, initialFacts;
    QJsonArray pixelsBeforeRelease, pixelsAfterRelease, historyPixels, terminalBindings;
    std::vector<BindingWatch> watches;
    std::unique_ptr<QSignalSpy> viewportChanges;
    QString phase = "setup";
    try {
        baseline = importHidden(c);
        const auto &old = baseline[0];
        openHidden(c, baseline[2]);
        const auto neutralPreview =
            fixtureReference(c, baseline[2].saved.asset.edits, path(baseline[2]));
        rendered(c, baseline[2], neutralPreview);
        journalBefore = journal(c);
        initialFacts = facts(c);
        QJsonArray expectedSession;
        expectedSession.append(QJsonValue(QJsonArray{QStringLiteral("1"), id(baseline[2])}));
        const auto imports = journalBefore["imports"].toArray();
        const auto items = journalBefore["items"].toArray();
        require(imports.size() == 1 && items.size() == 3 &&
                    journalBefore["session"].toArray() == expectedSession &&
                    initialFacts["collections"].toArray().isEmpty() &&
                    initialFacts["collection_assets"].toArray().isEmpty(),
                "RACE007 setup Import/session/collection cardinality invalid");
        const auto imported = imports.at(0).toArray();
        require(imported.size() == 8 && imported.at(1).toString() == "add" &&
                    imported.at(2).toString().isEmpty() &&
                    imported.at(3).toString() == "completed" &&
                    imported.at(4).toString().toLongLong() == QCoreApplication::applicationPid() &&
                    imported.at(5).toString() == "0" && !imported.at(6).toString().isEmpty() &&
                    !imported.at(7).toString().isEmpty(),
                "RACE007 setup is not one actually completed owned Add");
        for (int i = 0; i < 3; ++i) {
            const auto item = items.at(i).toArray();
            const auto &pin = baseline[static_cast<std::size_t>(i)];
            require(item.size() == 10 && item.at(0) == imported.at(0) &&
                        item.at(1).toString() == QString::number(i) &&
                        item.at(2).toString() == path(pin) && item.at(3).toString() == path(pin) &&
                        item.at(4).toString().isEmpty() &&
                        item.at(5).toString().toStdString() == pin.saved.asset.checksum &&
                        item.at(6).toString().toLongLong() == QFileInfo(path(pin)).size() &&
                        item.at(7).toString() == "committed" && item.at(8).toString() == id(pin) &&
                        item.at(9).toString().isEmpty(),
                    "RACE007 Import item does not pin exact source/target/hash/size/UUID");
        }
        durable(c, baseline, baseline, journalBefore, initialFacts);
        before = baseline;
        before[2].saved.asset.edits.exposure = 5.0; // literal one real Slider endpoint
        before[2].saved.revision = 1;
        before[2].jpegPixels = expectedJPEG(before[2].saved);
        applied = before;
        for (std::size_t i = 0; i < 2; ++i) {
            applied[i].saved.asset.edits.exposure = 5.0;
            applied[i].saved.revision = 1;
            applied[i].jpegPixels = expectedJPEG(applied[i].saved);
        }
        undo = before;
        redo = applied;
        for (std::size_t i = 0; i < 2; ++i) {
            undo[i].saved.revision = 2;
            redo[i].saved.revision = 3;
        }
        const auto cPreview = fixtureReference(c, before[2].saved.asset.edits, path(before[2]));
        const std::vector<Reference> references{
            {"A_OLD", baseline[0].jpegPixels}, {"B_OLD", baseline[1].jpegPixels},
            {"C_OLD", baseline[2].jpegPixels}, {"A_NEW", applied[0].jpegPixels},
            {"B_NEW", applied[1].jpegPixels},  {"C_NEW", before[2].jpegPixels}};
        const Rows rowReferences{
            {"A_OLD", referenceRow(baseline[0])}, {"B_OLD", referenceRow(baseline[1])},
            {"C_OLD", referenceRow(baseline[2])}, {"A_NEW", referenceRow(applied[0])},
            {"B_NEW", referenceRow(applied[1])},  {"C_NEW", referenceRow(before[2])}};
        QJsonArray rowMargins;
        for (std::size_t i = 0; i < rowReferences.size(); ++i)
            for (std::size_t j = i + 1; j < rowReferences.size(); ++j) {
                const double distance = rowDistance(rowReferences[i].rgba, rowReferences[j].rgba);
                require(distance > 12,
                        "RACE007 pre-Session immutable RAW row controls lack margin");
                rowMargins.append(QJsonObject{{"first", rowReferences[i].label},
                                              {"second", rowReferences[j].label},
                                              {"mean_abs_rgb_byte_distance", distance}});
            }
        validateRowControls(literalRow(), rowReferences);
        for (std::size_t i = 0; i < references.size(); ++i)
            for (std::size_t j = i + 1; j < references.size(); ++j)
                require(meanRGB(references[i].jpeg, references[j].jpeg) > 12,
                        "RACE007 literal OLD/NEW/wrong RGB palette lacks independent margin");
        // Independently fixed mapping: opaque256x192, orientation0, no crop/transform,
        // maximum384 means 1:1 row0. Literal bytes are not copied from gate output.
        const auto decoded = luma::PhotoDecoder().decode(old.saved.asset.path);
        const auto small = luma::standardPreview(decoded.image, 384);
        const auto oldRGBA =
            luma::renderCPU(small, luma::EditDocument{}, 0, {}, {}, decoded.calibration,
                            luma::DetailDiagnostic::none, 1, old.saved.asset.metadata);
        require(oldRGBA.width == 256 && oldRGBA.height == 192 &&
                    oldRGBA.rgba.size() == 256 * 192 * 4 &&
                    QByteArray(reinterpret_cast<const char *>(oldRGBA.rgba.data()), 256 * 4) ==
                        literalRow(),
                "RACE007 neutral original renderer does not preserve independently literal row");
        writeJson(
            c.root + "/race007-pinned-baseline.json",
            {{"candidate", identifier},
             {"baseline", docsJSON(baseline)},
             {"before_batch", docsJSON(before)},
             {"expected_applied", docsJSON(applied)},
             {"expected_undo", docsJSON(undo)},
             {"expected_redo", docsJSON(redo)},
             {"initial_sql", initialFacts},
             {"completed_journal", journalBefore},
             {"literal_old_row_rgba_hex", QString::fromLatin1(literalRow().toHex())},
             {"immutable_raw_row_references", rowsJSON(rowReferences)},
             {"pre_session_raw_row_pairwise_margins", rowMargins},
             {"mapping", "source row0 -> output row0, orientation0, neutral document, opaque, 1:1"},
             {"reference_limit",
              "ordinary CPU/ICC/JPEG shared numerical implementation; literal OLD row independent"},
             {"native_chooser_navigation_exercised", false}});
        phase = "seed-C-one-real-gesture";
        c.reveal("develop.exposureSlider");
        auto *slider = d.item("develop.exposureSlider");
        require(slider->property("from").toDouble() == -5 &&
                    slider->property("to").toDouble() == 5 &&
                    slider->property("stepSize").toDouble() == .01 &&
                    slider->property("value").toDouble() == 0 && saves.count() == 0,
                "RACE007 actual Exposure literal endpoint fixture unavailable");
        pointer.last = d.point("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"));
        d.press("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"));
        pointer.down = true;
        require(slider->property("pressed").toBool() && c.editor.uxHistoryGestureActive() &&
                    c.editor.currentSettings() == baseline[2].saved.asset.edits,
                "RACE007 actual C press did not acquire its real gesture without editing");
        pointer.last = d.point("develop.exposureSlider", {1, .5});
        d.move("develop.exposureSlider", {1, .5});
        d.release("develop.exposureSlider", {1, .5});
        pointer.down = false;
        require(!slider->property("pressed").toBool() && !c.editor.uxHistoryGestureActive() &&
                    c.editor.currentSettings() == before[2].saved.asset.edits &&
                    c.editor.canUndo() && !c.editor.canRedo(),
                "RACE007 seed is not one naturally terminated literal C Exposure5 edit");
        naturalSave(c, before[2]);
        rendered(c, before[2], cPreview);
        require(saves.count() == 1 && saves.at(0).at(0).toString() == id(before[2]),
                "RACE007 seed did not naturally persist exactly one C edit");
        phase = "actual-copy-all-groups";
        // The empty Presets tab avoids unrelated hover preview and keeps Copy reachable.
        d.click("preset.tabs.presets");
        d.frame();
        auto *copy = d.item("preset.copy");
        auto *presetScroll = d.item("develop.presetsScroll");
        if (!presetScroll->boundingRect().contains(
                copy->mapRectToItem(presetScroll, copy->boundingRect()))) {
            auto *bar = d.item("develop.presetsBar");
            auto *thumb =
                qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(bar->property("contentItem")));
            auto *content = qvariant_cast<QObject *>(presetScroll->property("contentItem"));
            require(bar->isVisible() && bar->isEnabled() && thumb && content &&
                        thumb->height() > 0 && bar->height() > 0 && bar->width() > 0 &&
                        bar->property("availableHeight").toDouble() > thumb->height(),
                    "RACE007 actual Copy button has no usable scrollbar route");
            const auto center = thumb->mapToItem(bar, thumb->boundingRect().center());
            const QPointF from(center.x() / bar->width(), center.y() / bar->height());
            const QPointF to(from.x(),
                             (bar->property("topPadding").toDouble() +
                              bar->property("availableHeight").toDouble() - thumb->height() / 2) /
                                 bar->height());
            const auto initialY = content->property("contentY").toDouble();
            pointer.last = d.point("develop.presetsBar", from);
            d.press("develop.presetsBar", from);
            pointer.down = true;
            require(bar->property("pressed").toBool(),
                    "RACE007 actual Copy-scroll press not acquired");
            pointer.last = d.point("develop.presetsBar", to);
            d.move("develop.presetsBar", to);
            d.release("develop.presetsBar", to);
            pointer.down = false;
            require(!bar->property("pressed").toBool(),
                    "RACE007 Copy-scroll release not processed");
            d.wait(
                [&] {
                    return content->property("contentY").toDouble() > initialY &&
                           !content->property("moving").toBool();
                },
                "RACE007 real positive Copy-scroll acknowledgment", 3000);
            d.frame();
        }
        require(copy->isVisible() && copy->isEnabled() &&
                    presetScroll->boundingRect().contains(
                        copy->mapRectToItem(presetScroll, copy->boundingRect())),
                "RACE007 actual Copy button remains clipped");
        d.click("preset.copy");
        d.wait([&] { return d.object("presetEditor")->property("opened").toBool(); },
               "RACE007 actual Copy settings dialog opened", 5000);
        require(d.object("presetEditor")->property("copyMode").toBool() &&
                    d.object("presetEditor")->property("groups").toInt() == luma::allSettingsGroups,
                "RACE007 actual Copy dialog did not select all current settings groups");
        d.click("presetSave");
        d.wait([&] { return !d.object("presetEditor")->property("visible").toBool(); },
               "RACE007 real Copy dialog closed", 5000);
        const auto copied = c.presets.copiedSettings();
        require(copied && copied->groups == luma::allSettingsGroups && copied->parameters.empty() &&
                    copied->settings == before[2].saved.asset.edits && c.presets.error().isEmpty(),
                "RACE007 actual Copy did not capture exact literal all-groups C snapshot");
        durable(c, baseline, before, journalBefore, initialFacts);
        require(c.library.count() == 0 && !c.library.loading() && !c.editor.presetPreview() &&
                    !gridCard(c, old) && !QFileInfo::exists(cachePath(c, old)),
                "RACE007 no-match setup warmed OLD card/cache or leaked a preview");
        auto *provider = dynamic_cast<ThumbnailProvider *>(c.engine.imageProvider("thumbnail"));
        require(provider, "RACE007 actual engine ThumbnailProvider missing");
        const qreal dpr = c.window->effectiveDevicePixelRatio();
        require(std::isfinite(dpr) && dpr > 0, "RACE007 actual finite positive DPR unavailable");
        const QSize requestSize = QSize(384, 384) * dpr;
        writeJson(c.root + "/race007-dpr-plan.json", {{"effective_dpr", dpr},
                                                      {"logical_width", 384},
                                                      {"logical_height", 384},
                                                      {"physical_width", requestSize.width()},
                                                      {"physical_height", requestSize.height()}});
        session = std::make_unique<trace::Session>(
            provider->uxTraceSlot(),
            trace::GateSpec{url(old), requestSize, std::chrono::milliseconds(30000),
                            trace::GatePoint::AfterFirstCpuRow});
        phase = "actual-first-row-gate";
        d.click("main.libraryButton");
        QSignalSpy setupResets(&c.library, &QAbstractItemModel::modelReset);
        require(setupResets.isValid(), "RACE007 setup reset observer unavailable");
        d.type("library.search", "");
        d.wait(
            [&] {
                const auto value = session->snapshot();
                return value.gateEntered || value.diagnosticFailure || value.gateTimedOut;
            },
            "RACE007 genuine first completed OLD RGBA row gate", 10000);
        d.wait(
            [&] {
                return setupResets.count() >= 2 && !c.library.loading() && c.library.count() == 3 &&
                       gridCard(c, old) && imageFor(gridCard(c, old)) && gridCard(c, before[1]) &&
                       imageFor(gridCard(c, before[1])) && gridCard(c, before[2]) &&
                       imageFor(gridCard(c, before[2]));
            },
            "RACE007 actual three metadata-backed visible cards", 5000);
        const auto stillUncancelled = [&] {
            held(*session, old, requestSize, true, rowReferences);
        };
        stillUncancelled();
        identity(c, gridCard(c, old), old, url(old), 2, false, false);
        waitReady(c, before[1], 1, stillUncancelled);
        waitReady(c, before[2], 1, stillUncancelled);
        cardPixels(c, before[1], url(before[1]), false, false, "B_OLD", "baseline", references,
                   stillUncancelled);
        cardPixels(c, before[2], url(before[2]), false, true, "C_NEW", "baseline", references,
                   stillUncancelled);
        phase = "select-exact-two-targets";
        identity(c, gridCard(c, old), old, url(old), 2, false, false);
        d.click(gridCard(c, old)->objectName());
        d.wait(
            [&] {
                return !c.library.selecting() &&
                       c.library.selectionIDs() == std::vector<std::string>{old.saved.asset.id};
            },
            "RACE007 real A-only selection", 3000);
        stillUncancelled();
        identity(c, gridCard(c, before[1]), before[1], url(before[1]), 1, false, false);
        d.click(gridCard(c, before[1])->objectName(), Qt::ControlModifier);
        const QSet<QString> expectedSelected{id(old), id(before[1])};
        d.wait(
            [&] {
                QSet<QString> selected;
                for (const auto &uuid : c.library.selectionIDs())
                    selected.insert(QString::fromStdString(uuid));
                return !c.library.selecting() && c.library.selectedCount() == 2 &&
                       selected == expectedSelected;
            },
            "RACE007 real modified click selected exactly A/B", 3000);
        const auto selected = c.library.selectionIDs();
        identity(c, gridCard(c, old), old, url(old), 2, true, false);
        identity(c, gridCard(c, before[1]), before[1], url(before[1]), 1, true, false);
        identity(c, gridCard(c, before[2]), before[2], url(before[2]), 1, false, true);
        const auto prior = held(*session, old, requestSize, true, rowReferences);
        editorState(c, before[2], cPreview, selected);
        durable(c, baseline, before, journalBefore, initialFacts);
        require(saves.count() == 1 && c.window->effectiveDevicePixelRatio() == dpr &&
                    !QFileInfo::exists(cachePath(c, old)),
                "RACE007 partial OLD unexpectedly cached/completed before real Paste");
        resets = std::make_unique<QSignalSpy>(&c.library, &QAbstractItemModel::modelReset);
        opens = std::make_unique<QSignalSpy>(&c.library, &LibraryModel::openRequested);
        loads = std::make_unique<QSignalSpy>(&c.editor, &EditorController::photoLoaded);
        require(resets->isValid() && opens->isValid() && loads->isValid(),
                "RACE007 acceptance reset/open/load observers unavailable");
        batchAudit = std::make_unique<BatchAudit>(c, *session);
        invalidations = std::make_unique<InvalidationAudit>(c, *session);
        activeAudit = std::make_unique<CStateAudit>(c, before[2]);
        writeJson(c.root + "/race007-before-paste.json",
                  {{"trace", traceJSON(prior)},
                   {"partial_raw_row_controls", rowControls(prior, rowReferences)},
                   {"actual_sql", facts(c)},
                   {"expected_documents", docsJSON(before)},
                   {"selection", QJsonArray{id(old), id(before[1])}},
                   {"active_uuid", id(before[2])},
                   {"old_current_image_loading", true}});
        phase = "one-actual-batch-paste";
        require(d.item("batchPaste")->isVisible() && d.item("batchPaste")->isEnabled(),
                "RACE007 actual batch Paste unavailable");
        pointer.last = d.point("batchPaste");
        d.press("batchPaste");
        pointer.down = true;
        pointer.cancelButton = true;
        require(d.object("batchPaste")->property("pressed").toBool(),
                "RACE007 actual Paste press not acquired");
        // Fresh guard AFTER genuine button press and BEFORE clicked/transaction on release.
        identity(c, gridCard(c, old), old, url(old), 2, true, false);
        const auto pressed = held(*session, old, requestSize, true, rowReferences);
        editorState(c, before[2], cPreview, selected);
        durable(c, baseline, before, journalBefore, initialFacts);
        require(resets->count() == 0 && opens->count() == 0 && loads->count() == 0 &&
                    saves.count() == 1,
                "RACE007 genuine Paste press changed baseline before batch commit");
        const auto beforeInput = lastSequence(pressed);
        writeJson(c.root + "/race007-paste-pressed-before-commit.json",
                  {{"trace", traceJSON(pressed)},
                   {"partial_raw_row_controls", rowControls(pressed, rowReferences)},
                   {"actual_sql", facts(c)},
                   {"actual_paste_pressed", true},
                   {"expected_documents", docsJSON(before)}});
        d.release("batchPaste");
        pointer.down = false;
        pointer.cancelButton = false;
        require(!d.object("batchPaste")->property("pressed").toBool(),
                "RACE007 actual Paste release not processed");
        batchIdle(c);
        batchAudit->requireOperations(1);
        activeAudit->requireUnchanged();
        invalidations->requireOperations(baseline, 1);
        require(c.batch.canUndo() && !c.batch.canRedo() && saves.count() == 1 &&
                    resets->count() == 0 && opens->count() == 0 && loads->count() == 0,
                "RACE007 real batch Paste changed active C/save/open/history unexpectedly");
        durable(c, baseline, applied, journalBefore, initialFacts);
        editorState(c, before[2], cPreview, selected);
        const auto afterCommit = held(*session, old, requestSize, false, rowReferences);
        writeJson(c.root + "/race007-durable-commit-old-partial.json",
                  {{"trace", traceJSON(afterCommit)},
                   {"partial_raw_row_controls", rowControls(afterCommit, rowReferences)},
                   {"actual_sql", facts(c)},
                   {"expected_documents", docsJSON(applied)},
                   {"batch", batchAudit->json()},
                   {"invalidations", invalidations->json()},
                   {"actual_cpu_instructions_overlap_claimed", false}});
        phase = "new-ready-while-old-partial";
        const auto stillHeld = [&] { held(*session, old, requestSize, false, rowReferences); };
        for (std::size_t i = 0; i < 3; ++i)
            waitReady(c, applied[i], 2, stillHeld);
        const auto ready = held(*session, old, requestSize, false, rowReferences);
        checkNewResponse(ready, applied[0], 2, requestSize, beforeInput);
        checkNewResponse(ready, applied[1], 2, requestSize, beforeInput);
        for (std::size_t i = 0; i < 3; ++i)
            pixelsBeforeRelease.append(
                cardPixels(c, applied[i], generation(applied[i], 2), i < 2, i == 2,
                           QString("%1_NEW").arg(
                               QChar::fromLatin1(static_cast<char>('A' + static_cast<int>(i)))),
                           "new-before-old-release", references, stillHeld));
        durable(c, baseline, applied, journalBefore, initialFacts);
        editorState(c, before[2], cPreview, selected);
        const auto beforeRelease = held(*session, old, requestSize, false, rowReferences);
        const auto viewSize = d.item("library.grid")->size();
        const auto viewOrigin = d.item("library.grid")->mapToScene(QPointF{});
        const double contentY = d.item("library.grid")->property("contentY").toDouble();
        const auto visible = visibleIDs(c);
        require(visible == QSet<QString>{id(old), id(before[1]), id(before[2])},
                "RACE007 exact three actual visible UUID set is not complete");
        for (const auto &pin : applied) {
            auto *card = gridCard(c, pin);
            auto *image = imageFor(card);
            require(card && image, "RACE007 current NEW binding unavailable before OLD release");
            watches.push_back({card, image, propertySpy(image, "source"),
                               propertySpy(card, "assetID"), propertySpy(card, "thumbnailURL")});
        }
        viewportChanges = propertySpy(d.item("library.grid"), "contentY");
        writeJson(c.root + "/race007-new-ready-old-partial.json",
                  {{"trace", traceJSON(beforeRelease)},
                   {"partial_raw_row_controls", rowControls(beforeRelease, rowReferences)},
                   {"actual_sql", facts(c)},
                   {"expected_documents", docsJSON(applied)},
                   {"pixels", pixelsBeforeRelease},
                   {"batch", batchAudit->json()},
                   {"invalidations", invalidations->json()},
                   {"boundary", "actual first RGBA row computed; remaining OLD render suspended "
                                "across actual durable batch commit"},
                   {"active_cpu_instructions_overlap_claimed", false}});
        phase = "explicit-old-release";
        session->release();
        d.wait(
            [&] {
                const auto value = session->snapshot();
                return value.diagnosticFailure ||
                       (count(value, value.gatedSerial, Kind::FinishedSignalObserved) == 1 &&
                        count(value, value.gatedSerial, Kind::Destroyed) == 1);
            },
            "RACE007 original OLD render cancellation/completion/destruction", 10000);
        const auto terminal = session->snapshot();
        const auto oldSerial = beforeRelease.gatedSerial;
        require(!terminal.diagnosticFailure && !terminal.gateTimedOut &&
                    terminal.releaseRequested && !terminal.sessionClosing &&
                    terminal.gatedSerial == oldSerial &&
                    one(terminal, oldSerial, Kind::GateReleased).sequence >
                        lastSequence(beforeRelease) &&
                    one(terminal, oldSerial, Kind::WorkerResultReady).sequence >
                        one(terminal, oldSerial, Kind::GateReleased).sequence &&
                    one(terminal, oldSerial, Kind::FinishedSignalObserved).sequence >
                        one(terminal, oldSerial, Kind::WorkerResultReady).sequence &&
                    one(terminal, oldSerial, Kind::Destroyed).sequence >
                        one(terminal, oldSerial, Kind::FinishedSignalObserved).sequence &&
                    count(terminal, oldSerial, Kind::GateSkipped) == 0 &&
                    count(terminal, oldSerial, Kind::GateTimedOut) == 0,
                "RACE007 OLD real terminal stage order invalid");
        const int cancelled = count(terminal, oldSerial, Kind::Cancelled);
        require(cancelled <= 1, "RACE007 OLD actual cancellation count invalid");
        const int aborted = cpuCount(terminal, oldSerial, trace::CpuStage::Aborted);
        const int returned = cpuCount(terminal, oldSerial, trace::CpuStage::Returned);
        require(aborted + returned == 1,
                "RACE007 OLD real render terminal must be exactly aborted or returned");
        if (aborted) {
            const auto &event = cpuOne(terminal, oldSerial, trace::CpuStage::Aborted);
            require(cancelled == 1 &&
                        one(terminal, oldSerial, Kind::Cancelled).sequence < event.sequence &&
                        event.sequence > one(terminal, oldSerial, Kind::GateReleased).sequence &&
                        event.sequence <
                            one(terminal, oldSerial, Kind::WorkerResultReady).sequence &&
                        !one(terminal, oldSerial, Kind::WorkerResultReady).successfulPixels &&
                        count(terminal, oldSerial, Kind::TextureFactoryCalled) == 0 &&
                        !QFileInfo::exists(cachePath(c, old)),
                    "RACE007 naturally cancelled OLD was overridden/published or aborted without "
                    "cancellation");
        } else {
            const auto &event = cpuOne(terminal, oldSerial, trace::CpuStage::Returned);
            require((cancelled == 0 || one(terminal, oldSerial, Kind::Cancelled).sequence >
                                           one(terminal, oldSerial, Kind::GateReleased).sequence) &&
                        event.sequence > one(terminal, oldSerial, Kind::GateReleased).sequence &&
                        event.sequence <
                            one(terminal, oldSerial, Kind::WorkerResultReady).sequence &&
                        event.width == 256 && event.height == 192,
                    "RACE007 actual returned OLD render dimensions/order invalid");
            if (one(terminal, oldSerial, Kind::WorkerResultReady).successfulPixels) {
                const QFileInfo oldCache(cachePath(c, old));
                require(oldCache.isFile() && !oldCache.isSymLink() &&
                            pixels(QImage(oldCache.filePath())) == old.jpegPixels,
                        "RACE007 actual completed OLD cache differs from immutable revision0");
            } else {
                // Cancellation can legitimately arrive after the renderer returned,
                // during original source-hash/JPEG/cache stages. Do not force timing.
                require(cancelled == 1 &&
                            one(terminal, oldSerial, Kind::Cancelled).sequence <
                                one(terminal, oldSerial, Kind::WorkerResultReady).sequence &&
                            count(terminal, oldSerial, Kind::TextureFactoryCalled) == 0 &&
                            !QFileInfo::exists(cachePath(c, old)),
                        "RACE007 OLD unsuccessful post-render terminal lacks actual cancellation");
            }
        }
        if (cancelled)
            require(one(terminal, oldSerial, Kind::Cancelled).sequence <
                        one(terminal, oldSerial, Kind::FinishedSignalObserved).sequence,
                    "RACE007 OLD cancellation occurred after final observed completion");
        phase = "post-old-terminal-stability";
        for (int sample = 0; sample < 2; ++sample) {
            d.frame();
            require(viewportChanges->count() == 0 && visibleIDs(c) == visible &&
                        d.item("library.grid")->size() == viewSize &&
                        d.item("library.grid")->mapToScene(QPointF{}) == viewOrigin &&
                        d.item("library.grid")->property("contentY").toDouble() == contentY,
                    "RACE007 late OLD delivery changed current visible UUID/viewport geometry");
            for (std::size_t i = 0; i < 3; ++i)
                watches[i].requireUnchanged(c, applied[i]);
            for (std::size_t i = 0; i < 3; ++i)
                pixelsAfterRelease.append(
                    cardPixels(c, applied[i], generation(applied[i], 2), i < 2, i == 2,
                               QString("%1_NEW").arg(
                                   QChar::fromLatin1(static_cast<char>('A' + static_cast<int>(i)))),
                               "terminal-" + QString::number(sample), references));
            editorState(c, before[2], cPreview, selected);
            durable(c, baseline, applied, journalBefore, initialFacts);
        }
        for (std::size_t i = 0; i < 3; ++i) {
            watches[i].requireUnchanged(c, applied[i]);
            terminalBindings.append(watches[i].json());
        }
        require(viewportChanges->count() == 0,
                "RACE007 late OLD terminal changed current viewport");
        activeAudit->requireUnchanged();
        const auto afterTerminal = session->snapshot();
        checkNewResponse(afterTerminal, applied[0], 2, requestSize, beforeInput);
        checkNewResponse(afterTerminal, applied[1], 2, requestSize, beforeInput);
        require(resets->count() == 0 && opens->count() == 0 && loads->count() == 0 &&
                    saves.count() == 1,
                "RACE007 terminal OLD caused reset/open/save or changed active C");
        writeJson(c.root + "/race007-race-terminal.json",
                  {{"trace", traceJSON(afterTerminal)},
                   {"actual_sql", facts(c)},
                   {"expected_documents", docsJSON(applied)},
                   {"natural_cancel_branch", cancelled == 1},
                   {"pixels_before_release", pixelsBeforeRelease},
                   {"pixels_after_release", pixelsAfterRelease},
                   {"batch", batchAudit->json()},
                   {"invalidations", invalidations->json()}});
        phase = "single-real-batch-undo";
        d.click("batchUndo");
        batchIdle(c);
        batchAudit->requireOperations(2);
        activeAudit->requireUnchanged();
        invalidations->requireOperations(baseline, 2);
        require(!c.batch.canUndo() && c.batch.canRedo(),
                "RACE007 one batch Undo history flags invalid");
        durable(c, baseline, undo, journalBefore, initialFacts);
        editorState(c, before[2], cPreview, selected);
        for (std::size_t i = 0; i < 3; ++i) {
            waitReady(c, undo[i], 3);
            historyPixels.append(
                cardPixels(c, undo[i], generation(undo[i], 3), i < 2, i == 2,
                           i == 2 ? "C_NEW"
                                  : QString("%1_OLD").arg(QChar::fromLatin1(
                                        static_cast<char>('A' + static_cast<int>(i)))),
                           "undo", references));
        }
        writeJson(c.root + "/race007-single-batch-undo.json",
                  {{"trace", traceJSON(session->snapshot())},
                   {"actual_sql", facts(c)},
                   {"expected_documents", docsJSON(undo)},
                   {"pixels", historyPixels}});
        phase = "single-real-batch-redo";
        d.click("batchRedo");
        batchIdle(c);
        batchAudit->requireOperations(3);
        activeAudit->requireUnchanged();
        invalidations->requireOperations(baseline, 3);
        require(c.batch.canUndo() && !c.batch.canRedo(),
                "RACE007 one batch Redo history flags invalid");
        durable(c, baseline, redo, journalBefore, initialFacts);
        editorState(c, before[2], cPreview, selected);
        for (std::size_t i = 0; i < 3; ++i) {
            waitReady(c, redo[i], 4);
            historyPixels.append(cardPixels(c, redo[i], generation(redo[i], 4), i < 2, i == 2,
                                            QString("%1_NEW").arg(QChar::fromLatin1(
                                                static_cast<char>('A' + static_cast<int>(i)))),
                                            "redo", references));
        }
        require(resets->count() == 0 && opens->count() == 0 && loads->count() == 0 &&
                    saves.count() == 1 && saves.at(0).at(0).toString() == id(before[2]) &&
                    c.window->effectiveDevicePixelRatio() == dpr &&
                    !session->snapshot().diagnosticFailure,
                "RACE007 bounded batch Undo/Redo leaked editor saves/reset/open/DPR changes");
        writeJson(
            c.root + "/race007-partial-render-summary.json",
            {{"candidate", identifier},
             {"inventory_domain", identifier == officialID ? "specification" : "supplemental"},
             {"official_inventory_registration", identifier == officialID},
             {"full_release_qualified", false},
             {"original_baseline", docsJSON(baseline)},
             {"expected_before", docsJSON(before)},
             {"expected_applied", docsJSON(applied)},
             {"expected_undo", docsJSON(undo)},
             {"expected_redo", docsJSON(redo)},
             {"final_sql", facts(c)},
             {"completed_journal", journalBefore},
             {"race_terminal_trace", traceJSON(afterTerminal)},
             {"final_trace", traceJSON(session->snapshot())},
             {"before_release_pixels", pixelsBeforeRelease},
             {"after_release_pixels", pixelsAfterRelease},
             {"single_undo_redo_pixels", historyPixels},
             {"current_binding_terminal", terminalBindings},
             {"batch", batchAudit->json()},
             {"invalidations", invalidations->json()},
             {"active_c_signals", activeAudit->json()},
             {"editor_natural_saves", saves.count()},
             {"natural_cancel_branch", cancelled == 1},
             {"literal_old_row_rgba_hex", QString::fromLatin1(literalRow().toHex())},
             {"immutable_raw_row_references", rowsJSON(rowReferences)},
             {"partial_raw_row_controls", rowControls(afterTerminal, rowReferences)},
             {"boundary", "genuine OLD first row computed before actual UI batch commit; NEW Ready "
                          "before OLD explicit release"},
             {"active_cpu_instructions_overlap_claimed", false},
             {"scope", "Qt/QTest inputs, sampled visible Image-node pixels, partial render; no "
                       "physical scanout/CPU simultaneous instructions/native chooser claim"}});
        return true;
    } catch (...) {
        // Before pointer/Session cleanup; no endEdit/flush/source assignment/retry rescue.
        try {
            writeJson(
                c.root + "/race007-original-failure-before-cleanup.json",
                {{"phase", phase},
                 {"trace", session ? traceJSON(session->snapshot()) : QJsonObject()},
                 {"pointer_down", pointer.down},
                 {"pointer_x", pointer.last.x()},
                 {"pointer_y", pointer.last.y()},
                 {"pointer_cancel_button_on_cleanup", pointer.cancelButton},
                 {"actual_batch_paste_pressed",
                  d.object("batchPaste")->property("pressed").toBool()},
                 {"actual_editor_settings",
                  QJsonDocument::fromJson(luma::encodePreset({"RACE007 failure settings",
                                                              {},
                                                              c.editor.currentSettings(),
                                                              luma::allSettingsGroups}))
                      .object()},
                 {"actual_local_masks_serialized", QString::fromStdString(luma::encodeLocalMasks(
                                                       c.editor.currentSettings().localMasks))},
                 {"actual_history_gesture_active", c.editor.uxHistoryGestureActive()},
                 {"actual_exposure_pressed",
                  d.object("develop.exposureSlider")->property("pressed").toBool()},
                 {"actual_editor_uuid", c.editor.photoID()},
                 {"actual_editor_saving", c.editor.saving()},
                 {"actual_can_undo", c.editor.canUndo()},
                 {"actual_can_redo", c.editor.canRedo()},
                 {"actual_sql", facts(c)},
                 {"natural_saves", saves.count()},
                 {"batch", batchAudit ? batchAudit->json() : QJsonObject()},
                 {"invalidations", invalidations ? invalidations->json() : QJsonObject()},
                 {"active_c_signals", activeAudit ? activeAudit->json() : QJsonObject()},
                 {"pixels_before_release", pixelsBeforeRelease},
                 {"pixels_after_release", pixelsAfterRelease},
                 {"history_pixels", historyPixels},
                 {"current_binding_terminal", terminalBindings}});
        } catch (...) {
            std::fputs("RACE007 failure evidence failed; original exception retained\n", stderr);
        }
        throw;
    }
}
} // namespace ux
