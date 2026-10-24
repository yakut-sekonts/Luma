#include "apps/shared/PhotoDecoder.h"
#include "apps/shared/PresetStore.h"
#include "core/render/CpuRenderer.h"
#include "tests/ux/OwnedVolume.h"
#include "tests/ux/QueuedCallGate.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <exception>
#include <memory>
#include <set>
#include <sqlite3.h>
#include <stdexcept>
#include <sys/stat.h>
#include <tuple>
#include <vector>

// Export variant: two files, one destination loss/recovery; functional only.
// The real queue is paused between files. No mid-write/physical USB claim,
// Runner override, synthetic filesystem errors, volume callbacks or controller calls.
namespace ux {
namespace {
QString sha(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read owned destination evidence: " + path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file), "Cannot hash complete owned destination evidence: " + path);
    return QString::fromLatin1(hash.result().toHex());
}
bool absent(const QString &path) {
    struct stat status{};
    errno = 0;
    return ::lstat(QFile::encodeName(path).constData(), &status) != 0 && errno == ENOENT;
}
bool sameMetadata(const luma::PhotoMetadata &a, const luma::PhotoMetadata &b) {
    return std::tie(a.camera, a.lens, a.iso, a.shutterSeconds, a.aperture, a.focalLength,
                    a.captureTimestamp, a.width, a.height, a.orientation) ==
           std::tie(b.camera, b.lens, b.iso, b.shutterSeconds, b.aperture, b.focalLength,
                    b.captureTimestamp, b.width, b.height, b.orientation);
}
luma::EditDocument landscape() {
    luma::EditDocument value;
    value.curves[0].points = {{0, 0}, {.2, .17}, {.5, .52}, {.8, .84}, {1, 1}};
    value.hsl = {{{0, 4, 0}, {0, 4, 0}, {-3, 8, 2}, {-4, 12, 0}, {0, 8, -2}, {-2, 12, -4}, {}, {}}};
    return value;
}
QJsonObject document(const luma::EditDocument &value) {
    const auto encoded = QJsonDocument::fromJson(luma::encodePreset(
        {"Destination frozen full document", {}, value, luma::allSettingsGroups}));
    require(encoded.isObject(), "Cannot retain full destination document evidence");
    return encoded.object();
}
QJsonObject ui(Context &c) {
    return {{"uuid", c.editor.photoID()},
            {"source", c.editor.sourcePath()},
            {"filename", c.editor.filename()},
            {"camera", c.editor.camera()},
            {"lens", c.editor.lens()},
            {"iso", c.editor.iso()},
            {"shutter", c.editor.shutter()},
            {"aperture", c.editor.aperture()},
            {"focal_length", c.editor.focalLength()},
            {"dimensions", c.editor.dimensions()}};
}
QJsonObject queue(Context &c) {
    return {{"working", c.exporter.working()},
            {"paused", c.exporter.paused()},
            {"pause_pending", c.exporter.pausePending()},
            {"waiting_for_save", c.exporter.waitingForSave()},
            {"completed", c.exporter.completed()},
            {"total", c.exporter.total()},
            {"succeeded", c.exporter.succeeded()},
            {"skipped", c.exporter.skipped()},
            {"progress", c.exporter.progress()},
            {"current_file", c.exporter.currentFile()},
            {"status", c.exporter.status()},
            {"error", c.exporter.error()},
            {"failures", QJsonArray::fromStringList(c.exporter.failures())}};
}
QJsonArray rows(sqlite3 *db, const char *sql) {
    sqlite3_stmt *raw = nullptr;
    const int prepared = sqlite3_prepare_v2(db, sql, -1, &raw, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw,
                                                                               sqlite3_finalize);
    require(prepared == SQLITE_OK, "Cannot prepare destination read-only journal audit");
    QJsonArray result;
    int status;
    while ((status = sqlite3_step(raw)) == SQLITE_ROW) {
        QJsonObject row;
        for (int column = 0; column < sqlite3_column_count(raw); ++column) {
            const auto name = QString::fromUtf8(sqlite3_column_name(raw, column));
            const int type = sqlite3_column_type(raw, column);
            if (type == SQLITE_NULL)
                row[name] = QJsonValue(QJsonValue::Null);
            else if (type == SQLITE_INTEGER)
                row[name] = static_cast<qint64>(sqlite3_column_int64(raw, column));
            else if (type == SQLITE_FLOAT)
                row[name] = sqlite3_column_double(raw, column);
            else if (type == SQLITE_TEXT)
                row[name] = QString::fromUtf8(
                    reinterpret_cast<const char *>(sqlite3_column_text(raw, column)),
                    sqlite3_column_bytes(raw, column));
            else
                require(false, "Unexpected BLOB in destination import-journal audit");
        }
        result.append(row);
    }
    require(status == SQLITE_DONE, "Destination journal audit did not finish");
    return result;
}
QJsonObject journal(Context &c) {
    sqlite3 *raw = nullptr;
    const int opened = sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &raw,
                                       SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
    require(opened == SQLITE_OK && sqlite3_busy_timeout(raw, 1000) == SQLITE_OK &&
                sqlite3_exec(raw, "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK,
            "Cannot begin consistent destination read-only journal audit");
    const auto sessions = rows(raw, "SELECT * FROM import_sessions ORDER BY id");
    const auto items = rows(raw, "SELECT * FROM import_items ORDER BY session_id,position");
    require(sqlite3_exec(raw, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK,
            "Cannot finish consistent destination journal audit");
    return {{"sessions", sessions}, {"items", items}};
}
luma::AssetSnapshot saved(Context &c, const std::string &id) {
    auto value = luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(id);
    if (!value)
        throw std::runtime_error("Destination export lost a saved source UUID");
    return *value;
}
QImage samples(QImage image) {
    require(!image.isNull(), "Destination output is not a complete decodable PNG");
    image = image.convertToFormat(QImage::Format_RGBA8888);
    image.setDevicePixelRatio(1);
    image.setColorSpace({});
    return image;
}
QImage reference(const QString &source, const luma::EditDocument &settings) {
    const auto decoded = luma::PhotoDecoder().decode(source.toStdString());
    require(decoded.image.width <= 512 && decoded.image.height <= 512 &&
                decoded.image.alpha.empty(),
            "Destination oracle needs two small opaque sources");
    const auto result =
        luma::renderCPU(decoded.image, settings, decoded.metadata.orientation, {}, {},
                        decoded.calibration, luma::DetailDiagnostic::none, 1, decoded.metadata);
    return QImage(result.rgba.data(), result.width, result.height,
                  static_cast<qsizetype>(result.width) * 4, QImage::Format_RGBA8888)
        .copy();
}
void png(const QString &path, const QImage &expected) {
    const QFileInfo file(path);
    require(file.isFile() && !file.isSymLink() && file.size() > 0 &&
                file.size() <= 8LL * 1024 * 1024,
            "Destination PNG is absent, nonregular or outside the small fixture budget");
    QImageReader reader(path);
    require(reader.format() == "png" && samples(reader.read()) == samples(expected),
            "Destination PNG crossed source/document identities or retained stale pixels");
}
void files(const QString &destination, const QStringList &expected) {
    auto actual =
        QDir(destination)
            .entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                       QDir::Name);
    auto sorted = expected;
    sorted.sort();
    require(actual == sorted,
            "Destination has unexpected output, directory, symlink or temporary residue");
    for (const auto &name : actual)
        require(QFileInfo(destination + '/' + name).isFile() &&
                    !QFileInfo(destination + '/' + name).isSymLink(),
                "Destination output is not its own regular file");
}
QJsonObject expectedSettings(const QString &destination) {
    return {{"format", 1},
            {"quality", 92},
            {"colorSpace", 0},
            {"compression", 0},
            {"resize", 0},
            {"resizeValue", 2048},
            {"outputMedium", 0},
            {"outputAmount", 1},
            {"metadata", 0},
            {"watermarkKind", 0},
            {"watermarkText", ""},
            {"watermarkImage", ""},
            {"watermarkOpacity", 70},
            {"watermarkSize", 15},
            {"watermarkMargin", 3},
            {"watermarkAnchor", 8},
            {"destination", destination},
            {"naming", "{filename}"},
            {"conflict", 0}};
}
void settings(Context &c, const QString &destination) {
    auto &d = *c.driver;
    d.click("exportButton");
    d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
           "destination export settings opened");
    d.type("export.destination", destination);
    d.type("exportNaming", "{filename}");
    d.choose("export.conflict", 0); // Skip protects the first committed checkpoint on recovery.
    d.choose("exportFormat", 1);
}
void visible(Context &c, const QImage &expected) {
    auto condition = [&] {
        auto *item = c.driver->item("develop.preview");
        return !c.editor.busy() && c.editor.hasPhoto() && item->isVisible() &&
               item->property("status").toInt() == 1 && !c.editor.previewUrl().isEmpty() &&
               item->property("source").toUrl().toString() == c.editor.previewUrl() &&
               c.provider->requestImage({}, nullptr, {}) == expected;
    };
    c.driver->wait(condition, "destination loss retained exact current-document Develop pixels");
    c.driver->frame();
    require(condition(), "Develop preview changed after the accepted destination boundary");
}
} // namespace

bool removableDestinationScenario(Context &c, const QString &id, const QString &variant) {
    const bool historicalCandidate =
        id == "CANDIDATE-MAC008-EXPORT-DESTINATION" && variant.isEmpty();
    const bool official = id == "UX-MAC-008" && variant == "export";
    if (!historicalCandidate && !official)
        return false;
    auto &d = *c.driver;
    require(c.library.count() == 0 && !c.editor.hasPhoto() && !c.exporter.working() &&
                luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 0 &&
                QFileInfo(c.catalogFile)
                    .canonicalFilePath()
                    .startsWith(QFileInfo(c.root).canonicalFilePath() + '/'),
            "Destination candidate needs a fresh isolated host catalog");
    const QString originals[]{c.fixture + "/Photo-0000.png", c.fixture + "/Photo-0001.png"};
    const auto sources = c.root + "/host-sources";
    require(!QFileInfo::exists(sources) && QDir().mkdir(sources) &&
                QFileInfo(sources).canonicalFilePath().startsWith(
                    QFileInfo(c.root).canonicalFilePath() + '/'),
            "Cannot create an exclusively owned host source directory");
    const QString paths[]{sources + "/Control-A.png", sources + "/Source-B.png"};
    const QString hashes[]{sha(originals[0]), sha(originals[1])};
    require(hashes[0] != hashes[1], "Destination sources must have distinct independent bytes");
    for (int index = 0; index != 2; ++index)
        require(QFile::copy(originals[index], paths[index]) &&
                    !QFileInfo(paths[index]).isSymLink() && sha(paths[index]) == hashes[index],
                "Cannot exclusively create intact private host sources");
    const auto metadataA = luma::PhotoDecoder().inspect(originals[0].toStdString());
    const auto metadataB = luma::PhotoDecoder().inspect(originals[1].toStdString());
    const luma::EditDocument defaultDocument;
    const auto editedB = landscape();
    QString phase = "before_volume_create", destination, outputA, outputB, retainedHash;
    OwnedVolume volume(d, c.root); // Catch/evidence and parent screen-lock lifetime enclose this.
    try {
        volume.createAndAttach();
        const auto volumeID = volume.volumeUUID(), mount = volume.mountPath();
        destination = mount + "/Exports"; // Never mount root: absent parent must stay absent.
        outputA = destination + "/Control-A.png";
        outputB = destination + "/Source-B.png";
        require(!volumeID.isEmpty() && !QFileInfo::exists(destination) &&
                    QDir().mkdir(destination) &&
                    QFileInfo(destination).canonicalFilePath() == destination,
                "Cannot create the private mounted export-only subdirectory");
        files(destination, {});
        phase = "actual_add_import";
        require(d.object("library.importDialog")
                    ->setProperty("sources",
                                  QVariant::fromValue(QList<QUrl>{QUrl::fromLocalFile(paths[0]),
                                                                  QUrl::fromLocalFile(paths[1])})),
                "Import pane rejected known host file-panel results");
        d.click("library.importButton");
        d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
               "two-source Add dialog opened");
        d.choose("library.importMode", 0);
        if (d.object("library.importDuplicates")->property("checked").toBool())
            d.click("library.importDuplicates");
        d.click("library.importStart");
        d.wait(
            [&] {
                return !c.library.working() && !c.library.loading() && !c.library.importing() &&
                       c.library.count() == 2;
            },
            "two-source Add completed", 60000);
        d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
               "Add modal fully closed");
        require(c.library.error().isEmpty() && c.library.importReport().isEmpty(),
                "Destination fixture Add reported failures");
        std::vector<luma::AssetSnapshot> pinned;
        for (const auto &assetID : luma::Catalog(c.catalogFile.toStdString()).libraryIDs({}))
            pinned.push_back(saved(c, assetID));
        require(pinned.size() == 2, "Destination fixture membership is not exactly two UUIDs");
        std::sort(pinned.begin(), pinned.end(), [](const auto &a, const auto &b) {
            return std::tie(a.asset.path, a.asset.id) < std::tie(b.asset.path, b.asset.id);
        });
        for (int index = 0; index != 2; ++index)
            require(!QUuid(QString::fromStdString(pinned[index].asset.id)).isNull() &&
                        pinned[index].asset.path.string() == paths[index].toStdString() &&
                        pinned[index].asset.checksum == hashes[index].toStdString() &&
                        pinned[index].asset.edits == defaultDocument &&
                        sameMetadata(pinned[index].asset.metadata, index ? metadataB : metadataA),
                    "Destination Add crossed UUID/path/SHA/full-document/metadata identity");
        const auto bID = QString::fromStdString(pinned[1].asset.id);
        // No Context::card()/manual model.data() page priming: actual QML must populate itself.
        d.wait([&] { return d.find("library.card." + bID) != nullptr; }, "actual host B card");
        d.doubleClick("library.card." + bID);
        c.loaded();
        require(c.editor.photoID() == bID && c.editor.sourcePath() == paths[1] &&
                    c.editor.currentSettings() == defaultDocument,
                "Actual B card opened the wrong host source/document");
        d.click("filter_landscape");
        d.move("main.filename", {.5, .5});
        require(c.editor.currentSettings() == editedB && !c.editor.presetPreview(),
                "Actual preset input missed its independent full document");
        d.wait(
            [&] {
                return !c.editor.saving() && !c.editor.busy() && c.editor.error().isEmpty() &&
                       saved(c, pinned[1].asset.id).asset.edits == editedB;
            },
            "B full document saved naturally before export");
        pinned[1] = saved(c, pinned[1].asset.id);
        const auto baselineJournal = journal(c);
        require(baselineJournal["sessions"].toArray().size() == 1 &&
                    baselineJournal["items"].toArray().size() == 2 &&
                    baselineJournal["sessions"].toArray()[0].toObject()["state"].toString() ==
                        "completed",
                "Destination source setup lacks one complete two-item Add journal");
        const auto session = baselineJournal["sessions"].toArray()[0].toObject();
        require(session["mode"].toString() == "add" && !session["finished_at"].isNull() &&
                    !QUuid(session["id"].toString()).isNull(),
                "Destination setup has no durable completed Add session UUID");
        std::set<int> positions;
        std::set<QString> itemIDs;
        for (const auto &entry : baselineJournal["items"].toArray()) {
            const auto item = entry.toObject();
            const auto source = item["source_path"].toString();
            const int index = source == paths[0] ? 0 : source == paths[1] ? 1 : -1;
            require(index >= 0, "Destination Add journal contains an unexpected host source");
            const auto assetID = QString::fromStdString(pinned[index].asset.id);
            const int position = item["position"].toInt(-1);
            require(item["session_id"] == session["id"] &&
                        item["state"].toString() == "committed" &&
                        item["asset_id"].toString() == assetID &&
                        item["target_path"].toString() == source &&
                        item["temporary_path"].toString().isEmpty() &&
                        item["checksum"].toString() == hashes[index] &&
                        item["filesize"].toInteger(-1) == QFileInfo(source).size() &&
                        item["error"].toString().isEmpty() && position >= 0 && position < 2 &&
                        positions.insert(position).second && itemIDs.insert(assetID).second,
                    "Destination Add journal is partial, duplicated or crosses source identities");
        }
        const auto baselineUI = ui(c);
        const auto baselineSettings = c.editor.currentSettings();
        const bool undo = c.editor.canUndo(), redo = c.editor.canRedo();
        require(undo && !redo && baselineSettings == editedB,
                "Destination fixture failed to establish independent edit/history state");
        const auto preview = fixtureReference(c, editedB, originals[1]);
        const auto expectedA = reference(originals[0], defaultDocument);
        const auto expectedB = reference(originals[1], editedB);
        require(samples(expectedA) != samples(expectedB) &&
                    samples(expectedB) != samples(reference(originals[1], defaultDocument)),
                "Output oracles cannot distinguish sources or edited/unedited B");
        visible(c, preview);
        d.click("main.libraryButton");
        d.click("library.selectAll");
        d.wait([&] { return !c.library.selecting() && c.library.selectedCount() == 2; },
               "two actual host source cards selected for export");
        d.click("main.developButton");
        const auto selected = c.library.selectionIDs();
        require(std::set<std::string>(selected.begin(), selected.end()) ==
                    std::set<std::string>{pinned[0].asset.id, pinned[1].asset.id},
                "Actual selection is not the exact two source UUIDs");
        auto unchanged = [&] {
            luma::Catalog catalog(c.catalogFile.toStdString());
            const auto active = catalog.activeAsset();
            require(catalog.libraryCount() == 2 && catalog.unfinishedImports().empty() &&
                        journal(c) == baselineJournal && ui(c) == baselineUI &&
                        c.editor.currentSettings() == baselineSettings &&
                        c.editor.canUndo() == undo && c.editor.canRedo() == redo &&
                        c.library.selectionIDs() == selected && c.editor.error().isEmpty() &&
                        !c.editor.saving() && !c.editor.busy() && active &&
                        active->id == pinned[1].asset.id,
                    "Destination lifecycle changed active UI/full document/history/import journal");
            for (int index = 0; index != 2; ++index) {
                const auto after = saved(c, pinned[index].asset.id);
                const auto &before = pinned[index];
                require(after.asset.id == before.asset.id &&
                            after.asset.path == before.asset.path &&
                            after.asset.checksum == before.asset.checksum &&
                            after.asset.edits == before.asset.edits &&
                            after.revision == before.revision &&
                            sameMetadata(after.asset.metadata, before.asset.metadata) &&
                            sha(paths[index]) == hashes[index] &&
                            sha(originals[index]) == hashes[index],
                        "Destination lifecycle damaged source "
                        "SHA/UUID/path/metadata/document/revision");
            }
        };
        QSignalSpy starts(d.object("export.selection"), SIGNAL(clicked()));
        QSignalSpy pauses(d.object("export.pause"), SIGNAL(clicked()));
        QSignalSpy resumes(d.object("export.resume"), SIGNAL(clicked()));
        require(starts.isValid() && pauses.isValid() && resumes.isValid(),
                "Cannot independently count genuine export UI inputs");
        phase = "first_queue_before_start";
        settings(c, destination);
        {
            QueuedCallGate gate(
                c.exporter,
                [&] {
                    return c.exporter.working() && !c.exporter.waitingForSave() &&
                           !c.exporter.paused() && !c.exporter.pausePending() &&
                           c.exporter.total() == 2 && c.exporter.completed() == 0 &&
                           c.exporter.succeeded() == 0 && c.exporter.skipped() == 0 &&
                           c.exporter.progress() == 1 &&
                           c.exporter.currentFile() == "Control-A.png" && !c.editor.editingLocked();
                },
                [&] {
                    // For default opaque PNG, progress(1) is posted only after guard install.
                    // Hold the real next MetaCall (completion), run actual UI Pause, then
                    // return false: production completion is delivered unchanged exactly once.
                    require(QJsonObject::fromVariantMap(c.exporter.settings()) ==
                                expectedSettings(destination),
                            "Unexpected complete PNG settings");
                    d.wait([&] { return !d.object("exportPanel")->property("visible").toBool(); },
                           "settings modal fully closed before actual Pause input");
                    png(outputA, expectedA);
                    files(destination, {"Control-A.png"});
                    retainedHash = sha(outputA);
                    d.click("export.pause");
                    require(pauses.count() == 1 && c.exporter.pausePending() &&
                                !c.exporter.paused() && c.exporter.completed() == 0,
                            "Actual Pause did not stop after the held real first output");
                    d.log({{"action", "destination_first_png_committed_real_pause_requested"},
                           {"queue", queue(c)},
                           {"output", outputA},
                           {"sha256", retainedHash}});
                });
            d.click("export.selection");
            d.wait([&] { return gate.caught; }, "real first PNG completion boundary");
            if (gate.failure)
                std::rethrow_exception(gate.failure);
            d.wait([&] { return c.exporter.paused() || !c.exporter.working(); },
                   "actual queue Pause settled after its real committed first file");
        }
        require(starts.count() == 1 && pauses.count() == 1 && resumes.count() == 0 &&
                    c.exporter.working() && c.exporter.paused() && c.exporter.completed() == 1 &&
                    c.exporter.total() == 2 && c.exporter.succeeded() == 1 &&
                    c.exporter.skipped() == 0 && c.exporter.failures().isEmpty() &&
                    c.exporter.error().isEmpty() && d.item("export.resume")->isVisible() &&
                    absent(outputB),
                "Queue was not genuinely paused between committed A and unstarted B");
        png(outputA, expectedA);
        require(sha(outputA) == retainedHash, "Pause damaged the actual first PNG");
        files(destination, {"Control-A.png"});
        unchanged();
        volume.pinOriginals({{"Exports/Control-A.png", retainedHash}});
        // Pin is preservation evidence; the independent CPU pixels above establish correctness.
        phase = "ordinary_detach_while_actual_queue_paused";
        volume.checkpoint("mac008_before_destination_detach",
                          {{"queue", queue(c)},
                           {"volume_uuid", volumeID},
                           {"destination", destination},
                           {"retained_png_sha256", retainedHash},
                           {"a_document", document(defaultDocument)},
                           {"b_document", document(editedB)},
                           {"production_import_journal", baselineJournal}});
        volume.detach(); // Outside Gate/eventFilter. No destination FD is held between files.
        require(volume.detached() && absent(mount) && absent(destination) && absent(outputA) &&
                    absent(outputB) && c.exporter.working() && c.exporter.paused() &&
                    c.exporter.completed() == 1,
                "Destination was not truly absent from the unchanged paused two-item queue");
        phase = "real_resume_on_absent_destination";
        volume.checkpoint("mac008_actual_destination_absent_before_resume", {{"queue", queue(c)}});
        d.click("export.resume");
        d.wait([&] { return !c.exporter.working(); }, "real missing destination queue settled",
               60000);
        require(
            resumes.count() == 1 && c.exporter.total() == 2 && c.exporter.completed() == 2 &&
                c.exporter.succeeded() == 1 && c.exporter.skipped() == 0 &&
                c.exporter.failures().size() == 1 && c.exporter.error().isEmpty() &&
                c.exporter.failures()[0].startsWith("Source-B.png: ") &&
                c.exporter.failures()[0].contains(destination) &&
                d.item("export.failures")->isVisible() &&
                d.object("export.failures")->property("text").toString() ==
                    c.exporter.failures().join('\n') &&
                absent(mount) && absent(destination) && absent(outputA) && absent(outputB),
            "Real missing destination failure was hidden, misattributed or wrote a host fallback");
        const auto actualFailure = c.exporter.failures()[0];
        const auto firstQueue = queue(c);
        unchanged();
        visible(c, preview);
        volume.checkpoint("mac008_real_destination_failure_before_remount",
                          {{"queue", queue(c)},
                           {"actual_failure", actualFailure},
                           {"mount_absent", true},
                           {"destination_absent", true},
                           {"full_documents_unchanged", true}});
        d.screenshot(c.root + "/destination-missing-error.png");
        phase = "same_image_destination_remount";
        volume.remount();
        require(volume.attached() && volume.volumeUUID() == volumeID &&
                    volume.mountPath() == mount &&
                    QFileInfo(destination).canonicalFilePath() == destination,
                "Recovery did not restore the exact own destination filesystem/path/UUID");
        png(outputA, expectedA);
        require(sha(outputA) == retainedHash && absent(outputB),
                "Missing destination destroyed A or silently committed B before remount");
        files(destination, {"Control-A.png"});
        unchanged();
        phase = "actual_ui_recovery_export_skip_retained_checkpoint";
        settings(c, destination);
        d.click("export.selection");
        d.wait([&] { return !c.exporter.working(); }, "two-source recovery export settled", 60000);
        require(starts.count() == 2 && pauses.count() == 1 && resumes.count() == 1 &&
                    c.exporter.total() == 2 && c.exporter.completed() == 2 &&
                    c.exporter.succeeded() == 1 && c.exporter.skipped() == 1 &&
                    c.exporter.failures().isEmpty() && c.exporter.error().isEmpty() &&
                    QJsonObject::fromVariantMap(c.exporter.settings()) ==
                        expectedSettings(destination) &&
                    d.object("export.failures")->property("text").toString().isEmpty() &&
                    !d.item("export.failuresScroll")->isVisible(),
                "Recovery did not Skip intact A and export exactly B with cleared UI failure");
        png(outputA, expectedA);
        png(outputB, expectedB);
        require(sha(outputA) == retainedHash, "Recovery overwrote the existing valid A checkpoint");
        const auto outputBHash = sha(outputB);
        files(destination, {"Control-A.png", "Source-B.png"});
        unchanged();
        visible(c, preview);
        volume.checkpoint("mac008_recovered_two_exact_outputs",
                          {{"queue", queue(c)},
                           {"a_png_sha256", retainedHash},
                           {"b_png_sha256", outputBHash},
                           {"checked_source_document_catalog_identity", true}});
        phase = "checked_finalize";
        volume.finalize();
        require(volume.detached() && absent(mount) && absent(destination),
                "Successful destination candidate requires explicit checked final detach");
        unchanged();
        c.checkData();
        writeJson(c.root + "/measurements.json",
                  {{"scenario", id},
                   {"variant", "export"},
                   {"scope", "EXPORT_QUEUE_BETWEEN_FILES_ONLY"},
                   {"copy_branch_covered", false},
                   {"physical_usb_exercised", false},
                   {"mid_open_write_exercised", false},
                   {"hardware_input_exercised", false},
                   {"input", "QT_QTEST_WINDOW_INPUT"},
                   {"assets", 2},
                   {"actual_export_starts", starts.count()},
                   {"actual_pause_clicks", pauses.count()},
                   {"actual_resume_clicks", resumes.count()},
                   {"actual_missing_destination_failure", actualFailure},
                   {"first_queue", firstQueue},
                   {"recovery_queue", queue(c)},
                   {"a_png_sha256", retainedHash},
                   {"b_png_sha256", outputBHash},
                   {"source_sha_documents_catalog_journal_unchanged", true},
                   {"no_host_fallback", true},
                   {"same_volume_uuid", volumeID},
                   {"checked_finalize_detached", true},
                   {"volume_evidence", volume.evidenceRoot()},
                   {"renderer_mathematical_correctness_claimed", false}});
        return true;
    } catch (...) {
        const auto originalFailure = std::current_exception();
        try {
            volume.checkpoint(
                "mac008_failure_before_cancel_or_volume_cleanup",
                {{"phase", phase},
                 {"queue", queue(c)},
                 {"active_ui", ui(c)},
                 {"editor_busy", c.editor.busy()},
                 {"editor_saving", c.editor.saving()},
                 {"can_undo", c.editor.canUndo()},
                 {"can_redo", c.editor.canRedo()},
                 {"full_document_matches_default", c.editor.currentSettings() == defaultDocument},
                 {"full_document_matches_landscape", c.editor.currentSettings() == editedB},
                 {"destination", destination},
                 {"destination_absent", !destination.isEmpty() && absent(destination)},
                 {"retained_png_sha256", retainedHash}});
        } catch (...) {
            std::fputs("MAC008 evidence failed; preserving original failure and owned image.\n",
                       stderr);
        }
        // Stop only this real queue through existing UI before own mount cleanup.
        // The actual failed state above is persisted first; failure never becomes PASS.
        try {
            if (c.exporter.working()) {
                d.click("export.cancel");
                d.wait([&] { return !c.exporter.working(); },
                       "failed destination case queue cancelled", 10000);
            }
        } catch (...) {
            std::fputs(
                "MAC008 queue cleanup failed; original failure retained, no forced detach.\n",
                stderr);
        }
        std::rethrow_exception(originalFailure);
    }
}
} // namespace ux
