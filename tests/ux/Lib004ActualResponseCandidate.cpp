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
#include <tuple>
#include <vector>

#ifndef LUMA_UX_THUMBNAIL_TRACE
#error "LIB004 candidate requires the real response observation seam"
#endif

// Shared strict contract for official LIB-004 and its historical supplemental candidate.
namespace ux {
namespace {
namespace trace = thumbnail_trace;
constexpr auto candidateID = "CANDIDATE-LIB004-ACTUAL-RESPONSE";
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
    require(file.open(QIODevice::ReadOnly), "Cannot hash private LIB004 original");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file), "Cannot read complete private LIB004 original");
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
        luma::encodePreset({"LIB004 complete baseline", {}, asset.edits, luma::allSettingsGroups}));
    require(encoded.isObject(), "Cannot encode complete pinned LIB004 document");
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
            "LIB004 requires small opaque generated PNGs");
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
    auto *image = card ? card->findChild<QQuickItem *>("photoThumbnail") : nullptr;
    require(image, "Actual PhotoCard Image is absent");
    return image;
}
const Pin &pinFor(const Pins &pins, const QString &asset) {
    const auto found =
        std::find_if(pins.begin(), pins.end(), [&](const auto &pin) { return id(pin) == asset; });
    require(found != pins.end(), "Visible card owns an unknown, unpinned UUID");
    return *found;
}
void identity(Context &c, QQuickItem *card, const Pin &pin, int imageStatus) {
    require(card && card->isVisible() && card->property("assetID").toString() == id(pin),
            "Current card does not own the pinned UUID");
    const auto *image = imageFor(card);
    const int row = card->property("index").toInt();
    require(
        row >= 0 && row < c.library.count() &&
            c.library.data(c.library.index(row), LibraryModel::AssetID).toString() == id(pin) &&
            c.library.data(c.library.index(row), LibraryModel::Path).toUrl().toLocalFile() ==
                path(pin) &&
            c.library.data(c.library.index(row), LibraryModel::Thumbnail).toUrl() == url(pin) &&
            card->property("thumbnailURL").toUrl() == url(pin) &&
            card->property("retryCount").toInt() == 0 && !card->property("selected").toBool() &&
            !card->property("activePhoto").toBool() &&
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
                pin.row.camera &&
            image->isVisible() && image->width() > 0 && image->height() > 0 &&
            image->property("source").toUrl() == url(pin) &&
            image->property("sourceSize").toSize() == QSize(384, 384) &&
            image->property("status").toInt() == imageStatus,
        "Actual Image/model full URL, UUID, path, retry or status is not the pinned current "
        "request");
}
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
double meanRGB(const QImage &first, const QImage &second, int border = 4) {
    require(first.size() == second.size() && first.width() > 2 * border &&
                first.height() > 2 * border,
            "Non-comparable LIB004 pixel regions");
    double total = 0;
    std::uint64_t samples = 0;
    for (int y = border; y < first.height() - border; ++y)
        for (int x = border; x < first.width() - border; ++x) {
            const auto a = first.pixelColor(x, y), b = second.pixelColor(x, y);
            total += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) +
                     std::abs(a.blue() - b.blue());
            samples += 3;
        }
    return total / static_cast<double>(samples);
}
QJsonObject cardPixels(Context &c, QQuickItem *card, const Pin &pin, const Pins &all,
                       const std::function<void()> &checkpoint = {}) {
    identity(c, card, pin, 1);
    QPointer<QQuickItem> cardGuard(card), imageGuard(imageFor(card));
    const auto geometry = imageGuard->size();
    const auto cached = QFileInfo(cachePath(c, pin));
    require(cached.isFile() && !cached.isSymLink() &&
                pixels(QImage(cached.filePath())) == pin.jpegPixels,
            "Actual provider cache differs from independent original/document JPEG pixels");
    const auto capture = readyItemGrab(c, *imageGuard, "LIB004 actual Image-node readback");
    require(cardGuard && imageGuard && imageFor(cardGuard.data()) == imageGuard.data() &&
                imageGuard->size() == geometry,
            "Card rebound/destroyed during Image-node capture");
    identity(c, cardGuard.data(), pin, 1);
    if (checkpoint)
        checkpoint();
    const auto actual = pixels(capture->image());
    const auto own =
        pixels(pin.jpegPixels.scaled(actual.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    const QRect painted((actual.width() - own.width()) / 2, (actual.height() - own.height()) / 2,
                        own.width(), own.height());
    const auto captured = actual.copy(painted);
    const double error = meanRGB(captured, own);
    double closestWrong = std::numeric_limits<double>::infinity();
    QString closestID;
    for (const auto &other : all) {
        if (id(other) == id(pin))
            continue;
        const auto wrong = pixels(
            other.jpegPixels.scaled(actual.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        require(wrong.size() == own.size(), "Competing fixture aspect ratios differ");
        const double separation = meanRGB(own, wrong);
        require(separation > 12,
                "Per-asset references are visually ambiguous at actual capture size");
        const double mismatch = meanRGB(captured, wrong);
        if (mismatch < closestWrong) {
            closestWrong = mismatch;
            closestID = id(other);
        }
    }
    require(error <= 4 && closestWrong > error + 8,
            "Actual visible thumbnail matches a competing asset or misses its own pixel reference");
    c.driver->log({{"action", "lib004_independent_visible_pixels"},
                   {"uuid", id(pin)},
                   {"mean_rgb_own_error", error},
                   {"closest_wrong_uuid", closestID},
                   {"mean_rgb_closest_wrong", closestWrong}});
    return {{"uuid", id(pin)},
            {"source", url(pin).toString(QUrl::FullyEncoded)},
            {"mean_rgb_own_error", error},
            {"mean_rgb_closest_wrong", closestWrong},
            {"closest_wrong_uuid", closestID}};
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
    writeJson(c.root + "/lib004-fixture-plan.json", result);
    require(selected != 0 && selected == count,
            "Fixture count must equal actual-grid minimal8/12/32 plan; select correct wrapper "
            "--fixture-count, no bulk fallback");
    return result;
}
Pins importHidden(Context &c, int count) {
    auto &d = *c.driver;
    require(c.library.count() == 0 && !c.editor.hasPhoto() &&
                luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 0,
            "LIB004 requires a new isolated catalog without activated photos");
    d.type("library.search", "lib004-no-such-filter-8ff23a");
    d.wait([&] { return !c.library.loading() && c.library.count() == 0; }, "actual empty search");
    const auto directory = c.root + "/owned-lib004-originals";
    require(QDir().mkpath(directory), "Cannot create own small original directory");
    const auto names = QDir(c.fixture).entryList({"Photo-*.png"}, QDir::Files, QDir::Name);
    require(names.size() == count, "Wrapper must generate exactly the planned bounded PNG count");
    QList<QUrl> sources;
    for (const auto &name : names) {
        const auto destination = directory + '/' + name;
        require(QFile::copy(c.fixture + '/' + name, destination), "Cannot copy private original");
        sources.push_back(QUrl::fromLocalFile(destination));
    }
    require(d.object("library.importDialog")->setProperty("sources", QVariant::fromValue(sources)),
            "Cannot supply declared file-chooser fixture result");
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
        "small Add import behind filter", 30000);
    d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
           "Import dialog fully closed");
    require(c.library.error().isEmpty(), c.library.error());
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto rows = catalog.libraryPage({}, 0, count);
    require(catalog.libraryCount() == count && static_cast<int>(rows.size()) == count,
            "Small Add import did not durably commit exact membership");
    Pins pins;
    for (const auto &row : rows) {
        const auto saved = catalog.assetSnapshot(row.id);
        require(
            saved && saved->asset.path == row.path && saved->asset.edits == luma::EditDocument{} &&
                saved->asset.metadata.width == 256 && saved->asset.metadata.height == 192 &&
                saved->asset.metadata.orientation == 0 && saved->revision >= 0 && row.rating == 0 &&
                row.flag == 0 && row.label.empty() && row.width == 256 && row.height == 192 &&
                row.orientation == 0 && row.camera == saved->asset.metadata.camera &&
                row.checksum == saved->asset.checksum &&
                row.filename ==
                    QFileInfo(QString::fromStdString(row.path.string())).fileName().toStdString() &&
                QString::fromStdString(row.path.string()).startsWith(directory + '/') &&
                digest(QString::fromStdString(row.path.string())).toHex().toStdString() ==
                    saved->asset.checksum,
            "Independent full-document/revision/source baseline is invalid");
        pins.push_back({*saved, row, expectedJPEG(*saved)});
        if (pins.size() % 4 == 0)
            d.frame(); // Own small fixture computation yields the GUI.
    }
    return pins;
}
void unchanged(Context &c, const Pins &pins, const luma::EditDocument &editorBefore) {
    require(
        !c.editor.hasPhoto() && c.editor.photoID().isEmpty() && c.editor.sourcePath().isEmpty() &&
            c.editor.currentSettings() == editorBefore && !c.editor.canUndo() &&
            !c.editor.canRedo() && !c.editor.saving() && !c.editor.busy() &&
            c.editor.error().isEmpty() && c.window->property("libraryMode").toBool() &&
            c.library.selectedCount() == 0 && !c.library.selecting() && !c.library.working() &&
            !c.library.loading() && c.library.error().isEmpty() &&
            c.library.count() == static_cast<int>(pins.size()) &&
            c.driver->object("library.search")->property("text").toString().isEmpty() &&
            c.driver->object("library.ratingFilter")->property("currentIndex").toInt() == 0 &&
            c.driver->object("libraryFlagFilter")->property("currentIndex").toInt() == 0 &&
            c.driver->object("libraryLabelFilter")->property("currentIndex").toInt() == 0 &&
            c.driver->object("library.pane")->property("currentCollection").toString().isEmpty(),
        "Thumbnail-only scroll changed editor/history/selection/Library state");
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto rows = catalog.libraryPage({}, 0, static_cast<int>(pins.size()));
    require(catalog.libraryCount() == static_cast<std::int64_t>(pins.size()) &&
                rows.size() == pins.size(),
            "Thumbnail scroll changed exact durable membership");
    const auto metadata = [](const luma::PhotoMetadata &m) {
        return std::tie(m.camera, m.lens, m.iso, m.shutterSeconds, m.aperture, m.focalLength,
                        m.captureTimestamp, m.width, m.height, m.orientation);
    };
    const auto rowFields = [](const luma::LibraryItem &row) {
        return std::tie(row.id, row.path, row.filename, row.checksum, row.width, row.height,
                        row.orientation, row.rating, row.flag, row.label, row.camera);
    };
    for (std::size_t index = 0; index < pins.size(); ++index) {
        const auto &before = pins[index].saved;
        const auto after = catalog.assetSnapshot(before.asset.id);
        require(
            after && rowFields(rows[index]) == rowFields(pins[index].row) &&
                rows[index].id == before.asset.id && after->asset.id == before.asset.id &&
                after->asset.path == before.asset.path &&
                after->asset.checksum == before.asset.checksum &&
                after->asset.edits == before.asset.edits && after->revision == before.revision &&
                metadata(after->asset.metadata) == metadata(before.asset.metadata) &&
                digest(path(pins[index])).toHex().toStdString() == before.asset.checksum,
            "Thumbnail response changed a pinned full document/revision/metadata/source identity");
    }
    catalog.verify();
}
struct ReleasePointer {
    Context &c;
    QPoint last;
    bool down = false;
    ~ReleasePointer() noexcept {
        if (down)
            try {
                QTest::mouseRelease(c.window, Qt::LeftButton, Qt::NoModifier, last);
            } catch (...) {
                std::fputs("LIB004 pointer cleanup failed\n", stderr);
            }
    }
};
} // namespace

bool lib004ActualResponseCandidate(Context &c, const QString &identifier) {
    if (identifier != candidateID && identifier != "UX-LIB-004")
        return false;
    auto &d = *c.driver;
    const auto names = QDir(c.fixture).entryList({"Photo-*.png"}, QDir::Files, QDir::Name);
    require(names.size() == 8 || names.size() == 12 || names.size() == 32,
            "Candidate supports only exact8/12/32 PNG fixtures");
    const auto geometryPlan = plan(c, static_cast<int>(names.size()));
    const auto editorBefore = c.editor.currentSettings();
    const auto pins = importHidden(c, static_cast<int>(names.size()));
    QJsonArray pinnedBaseline;
    for (const auto &pin : pins)
        pinnedBaseline.append(pinJSON(pin));
    writeJson(c.root + "/lib004-pinned-baseline.json", {{"assets", pinnedBaseline}});
    const auto &old = pins.front(), &fresh = pins.back();
    auto *provider = dynamic_cast<ThumbnailProvider *>(c.engine.imageProvider("thumbnail"));
    require(provider, "The actual engine ThumbnailProvider is not available");
    // Qt6.10.3 QQuickImageBase scales the declared sourceSize for scalable
    // image-provider URLs by its effective window DPR before requesting pixels.
    // Predict that exact size before the request; never relax the tuple or gate
    // to whichever response happens to arrive.
    const qreal devicePixelRatio = c.window->effectiveDevicePixelRatio();
    require(std::isfinite(devicePixelRatio) && devicePixelRatio > 0,
            "Actual window has no finite positive effective DPR");
    const QSize expectedRequestSize = QSize(384, 384) * devicePixelRatio;
    writeJson(c.root + "/lib004-dpr-plan.json",
              {{"effective_device_pixel_ratio", devicePixelRatio},
               {"logical_source_width", 384},
               {"logical_source_height", 384},
               {"expected_request_width", expectedRequestSize.width()},
               {"expected_request_height", expectedRequestSize.height()}});
    trace::Session session(
        provider->uxTraceSlot(),
        trace::GateSpec{url(old), expectedRequestSize, std::chrono::milliseconds(30000)});
    QPointer<QQuickItem> oldCard, oldImage;
    ReleasePointer pointer{c,
                           {}}; // Lifetime includes catch below; release precedes Session cleanup.
    QJsonArray newPixels, finalPixels;
    std::uint64_t pressSequence = 0, heldNewSequence = 0, newSerial = 0;
    QSignalSpy opens(&c.library, &LibraryModel::openRequested);
    QSignalSpy loads(&c.editor, &EditorController::photoLoaded);
    require(opens.isValid() && loads.isValid(), "Cannot observe absence of unrelated activation");
    try {
        d.type("library.search", "");
        d.wait(
            [&] {
                const auto value = session.snapshot();
                return value.gateEntered || value.diagnosticFailure || value.gateTimedOut;
            },
            "first actual OLD successful pending completion", 10000);
        d.wait(
            [&] {
                return !c.library.loading() && c.library.count() == static_cast<int>(pins.size());
            },
            "populated actual grid");
        oldCard = d.item(c.card(0));
        oldImage = imageFor(oldCard.data());
        require(oldCard && oldImage, "Current OLD card/Image vanished before input");
        QSignalSpy resets(&c.library, &QAbstractItemModel::modelReset);
        require(resets.isValid(), "Cannot observe unrelated model reset");
        const auto currentOld = [&] {
            require(oldCard && oldImage && imageFor(oldCard.data()) == oldImage.data() &&
                        !offscreen(c, oldCard),
                    "OLD is not a live current visible card before actual scroll");
            identity(c, oldCard.data(), old, 2);
            const auto value = held(session, old, expectedRequestSize, true);
            require(opens.count() == 0 && loads.count() == 0 && resets.count() == 0 &&
                        !c.library.loading() && !d.find("library.card." + id(fresh)) &&
                        requestsFor(value, url(fresh)).empty(),
                    "Activation/reset or prefetched NEW prevents the strict current-OLD fixture");
            return value;
        };
        currentOld();
        unchanged(c, pins, editorBefore);
        auto *grid = d.item("library.grid");
        auto *bar = d.object("library.scrollBar");
        const double startY = grid->property("contentY").toDouble();
        const double barSize = bar->property("size").toDouble();
        const double barPosition = bar->property("position").toDouble();
        require(barSize > 0 && barSize < 1 && startY == grid->property("originY").toDouble(),
                "Initial actual scrollbar is not at the first grid row");
        const QPointF from(.5, barPosition + barSize / 2), to(.5, 1 - barSize / 2);
        pointer.last = d.point("library.scrollBar", from);
        d.press("library.scrollBar", from);
        pointer.down = true;
        require(bar->property("pressed").toBool(), "Real scrollbar press was not acquired");
        const auto beforeMove = currentOld(); // Recheck AFTER press, BEFORE first actual move.
        pressSequence = lastSequence(beforeMove);
        writeJson(c.root + "/lib004-before-move.json", {{"trace", traceJSON(beforeMove)},
                                                        {"old_uuid", id(old)},
                                                        {"fresh_uuid", id(fresh)},
                                                        {"scrollbar_pressed", true},
                                                        {"old_loading", true},
                                                        {"fixture_plan", geometryPlan}});
        pointer.last = d.point("library.scrollBar", to);
        d.move("library.scrollBar", to); // One coarse genuine move is the bounded fast scroll.
        held(session, old, expectedRequestSize, false);
        d.release("library.scrollBar", to);
        pointer.down = false;
        require(!bar->property("pressed").toBool(),
                "Fast scrollbar release retained pointer input");
        d.wait([&] { return !grid->property("moving").toBool(); }, "fast scroll settled", 5000);
        const double settledY = grid->property("contentY").toDouble();
        require(settledY > startY + grid->height() &&
                    (!oldCard || oldCard->property("assetID").toString() != id(old) ||
                     offscreen(c, oldCard)),
                "Real fast input did not move beyond the original logical OLD card window");
        d.wait(
            [&] {
                held(session, old, expectedRequestSize, false);
                auto *card = d.find("library.card." + id(fresh));
                if (!card)
                    return false;
                auto *item = qobject_cast<QQuickItem *>(card);
                if (!item || offscreen(c, QPointer<QQuickItem>(item)))
                    return false;
                const auto value = session.snapshot();
                const auto requests = requestsFor(value, url(fresh));
                return imageFor(item)->property("status").toInt() == 1 && requests.size() == 1 &&
                       count(value, requests.front().serial, Kind::FinishedSignalObserved) == 1;
            },
            "NEW actual response finished and its newly visible Image became Ready while OLD held",
            10000);
        const auto newReady = held(session, old, expectedRequestSize, false);
        const auto freshRequests = requestsFor(newReady, url(fresh));
        require(freshRequests.size() == 1 &&
                    freshRequests.front().requestedSize == expectedRequestSize,
                "NEW actual response identity is missing or ambiguous");
        newSerial = freshRequests.front().serial;
        const auto &newCreated = one(newReady, newSerial, Kind::Created);
        const auto &newRead = one(newReady, newSerial, Kind::SnapshotRead);
        const auto &newResult = one(newReady, newSerial, Kind::WorkerResultReady);
        const auto &newFinished = one(newReady, newSerial, Kind::FinishedSignalObserved);
        require(
            newCreated.sequence > pressSequence && newCreated.sequence < newRead.sequence &&
                newRead.sequence < newResult.sequence &&
                newResult.sequence < newFinished.sequence &&
                newRead.revision == static_cast<std::uint64_t>(fresh.saved.revision) &&
                newResult.successfulPixels &&
                count(newReady, newSerial, Kind::TextureFactoryCalled) == 1 &&
                count(newReady, newSerial, Kind::Cancelled) == 0,
            "NEW Ready was only cached/pre-existing, failed, cancelled or from a wrong revision");
        const auto visible = visibleCards(c);
        require(!visible.empty(), "Fast scroll has no actual visible NEW cards");
        QSet<QString> checked;
        for (const auto &card : visible) {
            require(card, "Visible NEW delegate vanished before readback");
            const auto &pin = pinFor(pins, card->property("assetID").toString());
            require(id(pin) != id(old) && !checked.contains(id(pin)),
                    "Old/duplicate card remained in NEW window");
            d.wait(
                [&] {
                    held(session, old, expectedRequestSize, false);
                    return card && imageFor(card.data())->property("status").toInt() == 1;
                },
                "each visible NEW Image Ready", 5000);
            newPixels.append(cardPixels(c, card.data(), pin, pins,
                                        [&] { held(session, old, expectedRequestSize, false); }));
            checked.insert(id(pin));
        }
        require(checked.contains(id(fresh)),
                "Fresh actual response was not among pixel-verified visible cards");
        unchanged(c, pins, editorBefore);
        d.frame();
        const auto beforeRelease = held(session, old, expectedRequestSize, false);
        const auto releaseGridSize = grid->size();
        const auto releaseGridOrigin = grid->mapToScene(QPointF{});
        const double releaseContentY = grid->property("contentY").toDouble();
        const double releaseOriginY = grid->property("originY").toDouble();
        const double releaseContentHeight = grid->property("contentHeight").toDouble();
        const double releaseCellWidth = grid->property("cellWidth").toDouble();
        const double releaseCellHeight = grid->property("cellHeight").toDouble();
        const double releaseCacheBuffer = grid->property("cacheBuffer").toDouble();
        require(std::abs(releaseContentY - settledY) <= 1 && !grid->property("moving").toBool(),
                "NEW viewport did not settle before OLD release");
        const QJsonObject viewportBeforeRelease{
            {"content_y", releaseContentY},           {"origin_y", releaseOriginY},
            {"content_height", releaseContentHeight}, {"width", releaseGridSize.width()},
            {"height", releaseGridSize.height()},     {"scene_x", releaseGridOrigin.x()},
            {"scene_y", releaseGridOrigin.y()},       {"cell_width", releaseCellWidth},
            {"cell_height", releaseCellHeight},       {"cache_buffer", releaseCacheBuffer}};
        heldNewSequence = lastSequence(beforeRelease);
        writeJson(c.root + "/lib004-new-ready-old-held.json",
                  {{"trace", traceJSON(beforeRelease)},
                   {"new_actual_serial", QString::number(newSerial)},
                   {"visible_pixels", newPixels},
                   {"viewport", viewportBeforeRelease},
                   {"sampled_image_readbacks", true},
                   {"pending_completion_not_disk_io", true}});
        session
            .release(); // Only this explicit release satisfies acceptance; destructor is cleanup.
        d.wait(
            [&] {
                const auto value = session.snapshot();
                return value.diagnosticFailure ||
                       (count(value, value.gatedSerial, Kind::FinishedSignalObserved) == 1 &&
                        count(value, value.gatedSerial, Kind::Destroyed) == 1);
            },
            "OLD actual finished signal and QObject cleanup after explicit release", 10000);
        const auto terminal = session.snapshot();
        require(
            !terminal.diagnosticFailure && !terminal.gateTimedOut && terminal.releaseRequested &&
                !terminal.sessionClosing && terminal.gatedSerial == beforeRelease.gatedSerial &&
                one(terminal, terminal.gatedSerial, Kind::GateReleased).sequence >
                    heldNewSequence &&
                one(terminal, terminal.gatedSerial, Kind::FinishedSignalObserved).sequence >
                    one(terminal, terminal.gatedSerial, Kind::GateReleased).sequence &&
                one(terminal, terminal.gatedSerial, Kind::Destroyed).sequence >
                    one(terminal, terminal.gatedSerial, Kind::FinishedSignalObserved).sequence &&
                count(terminal, terminal.gatedSerial, Kind::GateTimedOut) == 0 &&
                count(terminal, terminal.gatedSerial, Kind::GateSkipped) == 0,
            "OLD cleanup is early, duplicated, timed out or from a different response");
        const int cancelled = count(terminal, terminal.gatedSerial, Kind::Cancelled);
        require(cancelled <= 1, "OLD actual cancellation was duplicated");
        if (cancelled) {
            require(one(terminal, terminal.gatedSerial, Kind::Cancelled).sequence <
                            one(terminal, terminal.gatedSerial, Kind::FinishedSignalObserved)
                                .sequence &&
                        count(terminal, terminal.gatedSerial, Kind::TextureFactoryCalled) == 0,
                    "Processed OLD cancel unexpectedly consumed a response texture");
        } else {
            require(count(terminal, terminal.gatedSerial, Kind::TextureFactoryCalled) == 1,
                    "Live uncancelled OLD completion lacks its actual Qt reader consumption");
            if (oldCard && oldCard->property("assetID").toString() == id(old)) {
                d.wait(
                    [&] {
                        return !oldCard ||
                               imageFor(oldCard.data())->property("status").toInt() == 1;
                    },
                    "surviving OLD binding becomes Ready", 5000);
                if (oldCard)
                    cardPixels(c, oldCard.data(), old, pins);
            }
        }
        d.frame();
        QSet<QString> finalIDs;
        for (const auto &card : visibleCards(c)) {
            require(card, "Current visible card vanished before final readback");
            const auto &pin = pinFor(pins, card->property("assetID").toString());
            require(!finalIDs.contains(id(pin)), "Final visible cards duplicate an asset identity");
            finalPixels.append(cardPixels(c, card.data(), pin, pins));
            finalIDs.insert(id(pin));
        }
        require(!finalPixels.isEmpty() && finalIDs == checked &&
                    std::abs(grid->property("contentY").toDouble() - releaseContentY) <= 1 &&
                    grid->size() == releaseGridSize &&
                    grid->mapToScene(QPointF{}) == releaseGridOrigin &&
                    grid->property("originY").toDouble() == releaseOriginY &&
                    grid->property("contentHeight").toDouble() == releaseContentHeight &&
                    grid->property("cellWidth").toDouble() == releaseCellWidth &&
                    grid->property("cellHeight").toDouble() == releaseCellHeight &&
                    grid->property("cacheBuffer").toDouble() == releaseCacheBuffer &&
                    !grid->property("moving").toBool() && opens.count() == 0 &&
                    loads.count() == 0 && resets.count() == 0,
                "Cleanup lost the NEW viewport/visible checks or unrelated activation/reset "
                "replaced scroll");
        const auto finalTrace = session.snapshot();
        require(!finalTrace.diagnosticFailure && !finalTrace.gateTimedOut &&
                    count(finalTrace, finalTrace.gatedSerial, Kind::FinishedSignalObserved) == 1 &&
                    count(finalTrace, finalTrace.gatedSerial, Kind::Destroyed) == 1 &&
                    requestsFor(finalTrace, url(fresh)).size() == 1 &&
                    count(finalTrace, newSerial, Kind::FinishedSignalObserved) == 1 &&
                    count(finalTrace, newSerial, Kind::TextureFactoryCalled) == 1 &&
                    count(finalTrace, newSerial, Kind::Cancelled) == 0,
                "Late trace/duplicate failure appeared after cleanup and final visible pixels");
        unchanged(c, pins, editorBefore);
        writeJson(
            c.root + "/lib004-actual-response-summary.json",
            {{"candidate", identifier},
             {"fixture_plan", geometryPlan},
             {"old_uuid", id(old)},
             {"new_uuid", id(fresh)},
             {"new_actual_serial", QString::number(newSerial)},
             {"old_processed_cancel", cancelled == 1},
             {"old_gui_reply_claimed", false},
             {"boundary",
              "successful provider result pending its original completion, not active disk I/O"},
             {"before_release_pixels", newPixels},
             {"after_release_pixels", finalPixels},
             {"unchanged_viewport", viewportBeforeRelease},
             {"trace", traceJSON(finalTrace)},
             {"scope",
              "sampled real Image-node frames, not exhaustive transient or physical scanout"}});
        return true;
    } catch (...) {
        // Both Session and pointer guards are still alive: preserve the original
        // held/pressed/native state before either destructor repairs it.
        try {
            writeJson(
                c.root + "/lib004-original-failure-before-cleanup.json",
                {{"trace", traceJSON(session.snapshot())},
                 {"pointer_down", pointer.down},
                 {"scrollbar_pressed", d.object("library.scrollBar")->property("pressed").toBool()},
                 {"old_card_present", !oldCard.isNull()},
                 {"old_image_present", !oldImage.isNull()},
                 {"old_card_current_uuid",
                  oldCard ? oldCard->property("assetID").toString() : QString()},
                 {"old_image_current_source",
                  oldImage ? oldImage->property("source").toUrl().toString(QUrl::FullyEncoded)
                           : QString()},
                 {"old_image_current_status", oldImage ? oldImage->property("status").toInt() : -1},
                 {"before_release_pixels", newPixels},
                 {"after_release_pixels", finalPixels},
                 {"press_sequence", QString::number(pressSequence)}});
        } catch (...) {
            std::fputs("LIB004 failure evidence failed; original exception retained\n", stderr);
        }
        throw;
    }
}
} // namespace ux
