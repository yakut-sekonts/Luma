#include "apps/shared/PresetStore.h"
#include "tests/ux/QueuedCallGate.h"
#include "tests/ux/UxContext.h"
#include <QColor>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSignalSpy>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// Genuine external disappearance of the private execution-input snapshot,
// never the original watermark or a controller/options mutation.
namespace ux {
namespace {
constexpr auto candidateID = "CANDIDATE-EXP028-WATERMARK-SNAPSHOT-MISSING";
constexpr auto officialID = "UX-EXP-028";
const QStringList photoNames{"A-owned.png", "B-owned.png", "C-owned.png"};
QColor literalPixel(int x, int y, int variant) {
    return QColor(35 + 65 * variant + x % 13, 55 + y % 17, 70 + (x + y) % 11);
}
QByteArray bytes(const QString &path) {
    QFile file(path);
    require(!QFileInfo(path).isSymLink() && file.open(QIODevice::ReadOnly),
            "Cannot read an owned EXP028 regular file: " + path);
    return file.readAll();
}
QString digest(const QString &path) {
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256).toHex());
}
QJsonObject documentJSON(const luma::EditDocument &document) {
    const auto encoded = QJsonDocument::fromJson(
        luma::encodePreset({"EXP028 full pinned document", {}, document, luma::allSettingsGroups}));
    require(encoded.isObject(), "Cannot serialize EXP028 pinned document");
    auto result = encoded.object();
    result["local_masks_serialized"] =
        QString::fromStdString(luma::encodeLocalMasks(document.localMasks));
    return result;
}
QJsonArray sqlRows(sqlite3 *database, const char *sql) {
    sqlite3_stmt *raw = nullptr;
    require(sqlite3_prepare_v2(database, sql, -1, &raw, nullptr) == SQLITE_OK,
            "Cannot prepare EXP028 durable row observation");
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw,
                                                                               sqlite3_finalize);
    QJsonArray rows;
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW) {
        QJsonArray row;
        for (int index = 0; index < sqlite3_column_count(statement.get()); ++index) {
            const auto type = sqlite3_column_type(statement.get(), index);
            QJsonValue value = QJsonValue::Null;
            if (type == SQLITE_INTEGER)
                value = QString::number(sqlite3_column_int64(statement.get(), index));
            else if (type == SQLITE_FLOAT)
                value = QString::number(sqlite3_column_double(statement.get(), index), 'g', 17);
            else if (type == SQLITE_TEXT)
                value = QString::fromUtf8(
                    reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), index)),
                    sqlite3_column_bytes(statement.get(), index));
            else if (type == SQLITE_BLOB)
                value =
                    QString::fromLatin1(QByteArray(static_cast<const char *>(
                                                       sqlite3_column_blob(statement.get(), index)),
                                                   sqlite3_column_bytes(statement.get(), index))
                                            .toHex());
            row.append(QJsonObject{{"sqlite_type", type}, {"value", value}});
        }
        rows.append(row);
    }
    require(step == SQLITE_DONE, "Cannot finish EXP028 durable row observation");
    return rows;
}
QJsonObject catalogFacts(Context &c) {
    sqlite3 *raw = nullptr;
    const int opened =
        sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &raw, SQLITE_OPEN_READONLY, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
    require(opened == SQLITE_OK &&
                sqlite3_exec(database.get(), "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK,
            "Cannot begin consistent EXP028 catalog observation");
    return {
        {"assets", sqlRows(database.get(), "SELECT * FROM assets ORDER BY id")},
        {"session", sqlRows(database.get(), "SELECT * FROM session ORDER BY slot")},
        {"imports", sqlRows(database.get(), "SELECT * FROM import_sessions ORDER BY id")},
        {"items",
         sqlRows(database.get(), "SELECT * FROM import_items ORDER BY session_id,position")},
        {"collections", sqlRows(database.get(), "SELECT * FROM collections ORDER BY id")},
        {"membership", sqlRows(database.get(),
                               "SELECT * FROM collection_assets ORDER BY collection_id,asset_id")}};
}
struct SourcePin {
    luma::AssetSnapshot saved;
    QString path, sha;
};
struct Baseline {
    QJsonObject facts;
    std::vector<SourcePin> sources;
    std::vector<std::string> selected, members;
    QString watermark, watermarkSHA;
};
void invariant(Context &c, const Baseline &before) {
    require(!c.editor.hasPhoto() && c.editor.photoID().isEmpty() &&
                c.editor.sourcePath().isEmpty() &&
                c.editor.currentSettings() == luma::EditDocument{} && !c.editor.canUndo() &&
                !c.editor.canRedo() && !c.editor.saving() && !c.editor.busy() &&
                c.editor.error().isEmpty() && !c.library.loading() && c.library.error().isEmpty() &&
                !c.batch.working() && c.library.selectionIDs() == before.selected &&
                luma::Catalog(c.catalogFile.toStdString()).libraryIDs({}) == before.members &&
                catalogFacts(c) == before.facts && digest(before.watermark) == before.watermarkSHA,
            "EXP028 changed active editor/history/catalog metadata/imports/selection/original "
            "watermark");
    luma::Catalog catalog(c.catalogFile.toStdString());
    for (const auto &pin : before.sources) {
        const auto current = catalog.assetSnapshot(pin.saved.asset.id);
        require(current && current->revision == pin.saved.revision &&
                    current->asset.edits == pin.saved.asset.edits &&
                    current->asset.id == pin.saved.asset.id &&
                    current->asset.path == pin.saved.asset.path &&
                    current->asset.checksum == pin.saved.asset.checksum &&
                    digest(pin.path) == pin.sha,
                "EXP028 changed a full durable document/revision/identity/original SHA");
    }
}
void importOwned(Context &c) {
    auto &d = *c.driver;
    QList<QUrl> sources;
    for (const auto &name : photoNames)
        sources.append(QUrl::fromLocalFile(c.fixture + '/' + name));
    require(d.object("library.importDialog")->setProperty("sources", QVariant::fromValue(sources)),
            "EXP028 chooser-result fixture rejected sources");
    d.log({{"action", "fixture_file_chooser_result"},
           {"photo_count", 3},
           {"native_chooser_exercised", false}});
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "EXP028 actual Import dialog opened");
    d.choose("library.importMode", 0);
    if (d.object("library.importDuplicates")->property("checked").toBool())
        d.click("library.importDuplicates");
    d.click("library.importStart");
    d.wait([&] { return !c.library.working() && !c.library.loading() && c.library.count() == 3; },
           "EXP028 three actual source imports completed");
    d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
           "EXP028 import exit transition completed");
    d.click("library.selectAll");
    d.wait([&] { return !c.library.selecting() && c.library.selectedCount() == 3; },
           "EXP028 exactly three owned photos actually selected");
}
void reveal(Context &c, const QString &id) {
    auto *view = qobject_cast<QQuickItem *>(
        qvariant_cast<QObject *>(c.driver->object("export.scroll")->property("contentItem")));
    auto *target = c.driver->item(id);
    require(view && view->property("contentY").isValid() && target->isVisible() &&
                target->isEnabled(),
            "EXP028 actual export control unavailable: " + id);
    for (int step = 0; step < 100; ++step) {
        c.driver->wait([&] { return !view->property("moving").toBool(); },
                       "EXP028 form wheel settled");
        c.driver->frame();
        const auto bounds = target->mapRectToItem(view, target->boundingRect());
        if (bounds.top() >= 0 && bounds.bottom() <= view->height())
            return;
        c.driver->wheel("export.scroll", bounds.top() < 0 ? 120 : -120);
    }
    throw std::runtime_error("EXP028 real form input cannot reveal " + id.toStdString());
}
void text(Context &c, const QString &id, const QString &value, bool spin = false) {
    reveal(c, id);
    c.driver->type(id, value);
    if (spin)
        c.driver->key(Qt::Key_Return);
    c.driver->key(Qt::Key_Tab);
}
void choose(Context &c, const QString &id, int index) {
    reveal(c, id);
    c.driver->choose(id, index);
}
void configure(Context &c, const QString &folder, const QString &watermark) {
    c.driver->click("exportButton");
    c.driver->wait([&] { return c.driver->object("exportPanel")->property("opened").toBool(); },
                   "EXP028 actual export settings opened");
    text(c, "export.destination", folder);
    text(c, "exportNaming", "{filename}");
    choose(c, "export.conflict", 0);
    choose(c, "exportFormat", 1);
    choose(c, "export.colorSpace", 0);
    choose(c, "export.resize", 0);
    choose(c, "export.outputMedium", 0);
    choose(c, "export.metadata", 0);
    choose(c, "export.watermarkKind", 2);
    text(c, "export.watermarkImage", watermark);
    text(c, "export.watermarkOpacity", "100", true);
    text(c, "export.watermarkSize", "20", true);
    text(c, "export.watermarkMargin", "3", true);
    choose(c, "export.watermarkAnchor", 8);
}
struct FileIdentity {
    dev_t device{};
    ino_t inode{};
    off_t size{};
};
FileIdentity identity(const QString &path, bool directory = false) {
    struct stat status{};
    require(::lstat(path.toUtf8().constData(), &status) == 0 && status.st_uid == ::getuid() &&
                (directory ? S_ISDIR(status.st_mode) : S_ISREG(status.st_mode)) &&
                (directory || status.st_nlink == 1),
            "EXP028 owned path is not an unaliased own regular file/directory: " + path);
    return {status.st_dev, status.st_ino, status.st_size};
}
bool sameIdentity(const FileIdentity &first, const FileIdentity &second) {
    return first.device == second.device && first.inode == second.inode &&
           first.size == second.size;
}
struct OwnedRuntime {
    QString root;
    QJsonObject ownership;
};
OwnedRuntime ownedTemp(Context &c, const QString &id) {
    const auto expected = qEnvironmentVariable("LUMA_UX_EXP028_TMP_ROOT");
    require(qEnvironmentVariable("LUMA_UX_EXP028_CASE_ROOT") == c.root &&
                QFileInfo(c.root).canonicalFilePath() == c.root &&
                expected == c.root + "-owned-runtime-temp" &&
                qEnvironmentVariable("TMPDIR") == expected &&
                QFileInfo(expected).canonicalFilePath() == expected && QDir::tempPath() == expected,
            "EXP028 child did not inherit its exact canonical own TMPDIR before Qt startup");
    identity(expected, true);
    struct stat tempStatus{};
    require(::lstat(expected.toUtf8().constData(), &tempStatus) == 0 &&
                (tempStatus.st_mode & 0777) == 0700,
            "EXP028 owned temp directory is not private mode0700");
    const auto ownerPath = expected + "/wrapper-owner.json";
    identity(ownerPath);
    const auto owner = QJsonDocument::fromJson(bytes(ownerPath)).object();
    require(owner["case"].toString() == id && owner["case_root"].toString() == c.root &&
                owner["temp_root"].toString() == expected &&
                owner["wrapper_pid"].toInteger() == ::getppid() &&
                !owner["nonce"].toString().isEmpty(),
            "EXP028 temp ownership marker does not match this exact child/parent/run root");
    const auto childMarker = expected + "/child-pid-" + QString::number(::getpid()) + ".json";
    require(!QFileInfo::exists(childMarker), "EXP028 child PID claim must be fresh");
    const QJsonObject claim{{"child_pid", QString::number(::getpid())},
                            {"parent_pid", QString::number(::getppid())},
                            {"case_root", c.root},
                            {"temp_root", expected},
                            {"wrapper_nonce", owner["nonce"]}};
    writeJson(childMarker, claim);
    identity(childMarker);
    require(QJsonDocument::fromJson(bytes(childMarker)).object() == claim,
            "EXP028 fresh child ownership claim readback differs");
    return {expected,
            {{"wrapper_owner", owner},
             {"wrapper_owner_sha256", digest(ownerPath)},
             {"child_claim", claim},
             {"child_claim_sha256", digest(childMarker)},
             {"actual_uid", QString::number(static_cast<qulonglong>(tempStatus.st_uid))},
             {"actual_mode_octal", QString::number(tempStatus.st_mode & 0777, 8)},
             {"device", QString::number(static_cast<qulonglong>(tempStatus.st_dev))},
             {"inode", QString::number(static_cast<qulonglong>(tempStatus.st_ino))}}};
}
QJsonObject outputA(const QString &path) {
    auto image = QImage(path);
    require(image.size() == QSize(64, 48) && image.colorSpace().isValid(),
            "EXP028 real A output has wrong literal dimensions/profile");
    image.convertToColorSpace(QColorSpace::SRgb);
    const QRect watermarkBounds(50, 34, 13, 13); // lround(64*.20)=13, lround(48*.03)=1.
    int maximumOutside = 0, maximumInside = 0, outsideCount = 0, insideCount = 0;
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 64; ++x) {
            const auto actual = image.pixelColor(x, y);
            require(actual.alpha() == 255, "EXP028 opaque output gained transparency");
            if (QRect(52, 36, 9, 9).contains(x, y)) {
                maximumInside =
                    std::max({maximumInside, 255 - actual.red(), actual.green(), actual.blue()});
                ++insideCount;
            } else if (!watermarkBounds.adjusted(-1, -1, 1, 1).contains(x, y)) {
                const auto expected = literalPixel(x, y, 0);
                maximumOutside = std::max({maximumOutside, std::abs(actual.red() - expected.red()),
                                           std::abs(actual.green() - expected.green()),
                                           std::abs(actual.blue() - expected.blue())});
                ++outsideCount;
            }
        }
    require(maximumInside <= 1 && maximumOutside <= 1 && insideCount == 81 && outsideCount > 2800,
            "EXP028 A does not contain independent literal red watermark/source pixels");
    return {{"path", path},
            {"sha256", digest(path)},
            {"red_samples", insideCount},
            {"source_samples", outsideCount},
            {"maximum_red_rgb_delta", maximumInside},
            {"maximum_source_rgb_delta", maximumOutside}};
}
QJsonObject queue(Context &c) {
    return {{"current_file", c.exporter.currentFile()},
            {"working", c.exporter.working()},
            {"total", c.exporter.total()},
            {"completed", c.exporter.completed()},
            {"succeeded", c.exporter.succeeded()},
            {"skipped", c.exporter.skipped()},
            {"progress", c.exporter.progress()},
            {"error", c.exporter.error()},
            {"failures", QJsonArray::fromStringList(c.exporter.failures())}};
}
void run(Context &c, const QString &id) {
    auto &d = *c.driver;
    const auto runtime = ownedTemp(c, id);
    const auto &temporary = runtime.root;
    require(c.library.count() == 0 && !c.editor.hasPhoto() && !c.exporter.working(),
            "EXP028 requires a fresh private Context");
    Baseline before;
    before.watermark = c.fixture + "/Watermark-original.png";
    before.watermarkSHA = digest(before.watermark);
    const QImage originalMark(before.watermark);
    require(originalMark.size() == QSize(32, 32), "EXP028 watermark fixture dimensions changed");
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            require(originalMark.pixelColor(x, y) == QColor(255, 0, 0),
                    "EXP028 original watermark is not literal opaque red");
    QMap<QString, QString> originalHashes;
    for (int variant = 0; variant < 3; ++variant) {
        const auto path = c.fixture + '/' + photoNames[variant];
        require(QFileInfo(path).canonicalFilePath() == path, "EXP028 source is not canonical");
        const QImage image(path);
        require(image.size() == QSize(64, 48), "EXP028 requires three small64x48 RGB sources");
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 64; ++x)
                require(image.pixelColor(x, y) == literalPixel(x, y, variant),
                        "EXP028 source literal-pixel recipe differs");
        originalHashes.insert(path, digest(path));
    }
    require(QDir(c.fixture).entryList(QDir::Files | QDir::Hidden).size() == 4,
            "EXP028 requires exactly three source PNGs plus one original mark");
    importOwned(c);
    before.facts = catalogFacts(c);
    before.selected = c.library.selectionIDs();
    before.members = luma::Catalog(c.catalogFile.toStdString()).libraryIDs({});
    require(before.selected.size() == 3 && before.members.size() == 3,
            "EXP028 selection/membership mismatch");
    auto sortedSelected = before.selected, sortedMembers = before.members;
    std::sort(sortedSelected.begin(), sortedSelected.end());
    std::sort(sortedMembers.begin(), sortedMembers.end());
    require(sortedSelected == sortedMembers,
            "EXP028 selected UUID set differs from all three owned members");
    QJsonArray pins;
    luma::Catalog catalog(c.catalogFile.toStdString());
    for (const auto &member : before.members) {
        const auto saved = catalog.assetSnapshot(member);
        require(saved && saved->revision == 0 && saved->asset.edits == luma::EditDocument{},
                "EXP028 source did not import a neutral full document/revision0");
        const auto path = QString::fromStdString(saved->asset.path.string());
        require(originalHashes.contains(path) &&
                    originalHashes[path].toStdString() == saved->asset.checksum,
                "EXP028 durable source identity differs from pre-import SHA");
        before.sources.push_back({*saved, path, originalHashes[path]});
        pins.append(QJsonObject{{"uuid", QString::fromStdString(member)},
                                {"path", path},
                                {"sha256", originalHashes[path]},
                                {"revision", QString::number(saved->revision)},
                                {"full_document", documentJSON(saved->asset.edits)}});
    }
    invariant(c, before);
    const auto folder = c.root + "/exports";
    require(!QFileInfo::exists(folder) && QDir().mkpath(folder),
            "EXP028 needs a fresh destination");
    configure(c, folder, before.watermark);
    QSignalSpy clicked(d.object("export.selection"), SIGNAL(clicked()));
    require(clicked.isValid(), "EXP028 selected-photo button clicked observation absent");
    QJsonArray states;
    QStringList started;
    QObject observer;
    QObject::connect(&c.exporter, &ExportController::changed, &observer, [&] {
        states.append(queue(c));
        const auto name = c.exporter.currentFile();
        if (c.exporter.working() && !name.isEmpty() &&
            (started.isEmpty() || started.last() != name))
            started.append(name);
    });
    QJsonObject evidence{{"scenario", id},
                         {"inventory_domain", id == officialID ? "specification" : "supplemental"},
                         {"owned_tmpdir", temporary},
                         {"runtime_temp_ownership", runtime.ownership},
                         {"child_pid", QString::number(::getpid())},
                         {"parent_pid", QString::number(::getppid())},
                         {"sources", pins},
                         {"before_catalog_facts", before.facts},
                         {"original_watermark", before.watermark},
                         {"original_watermark_sha256", before.watermarkSHA},
                         {"production_options_or_queue_mutated", false},
                         {"original_watermark_deleted", false},
                         {"per_job_snapshot_path_trace_claimed", false}};
    auto persist = [&] {
        evidence["queue_states"] = states;
        evidence["terminal_queue"] = queue(c);
        evidence["started_filenames"] = QJsonArray::fromStringList(started);
        writeJson(c.root + "/exp028-evidence.json", evidence);
    };
    persist();
    QJsonObject positive;
    QJsonObject liveSettings;
    QueuedCallGate gate(
        c.exporter,
        [&] {
            return c.exporter.working() && !c.exporter.waitingForSave() && !c.exporter.paused() &&
                   c.exporter.total() == 3 && c.exporter.completed() == 0 &&
                   c.exporter.progress() == 1 && c.exporter.currentFile() == photoNames[0] &&
                   !c.editor.editingLocked();
        },
        [&] {
            require(started == QStringList{photoNames[0]} && c.exporter.failures().isEmpty() &&
                        c.exporter.error().isEmpty() &&
                        !QFileInfo::exists(folder + '/' + photoNames[1]) &&
                        !QFileInfo::exists(folder + '/' + photoNames[2]),
                    "EXP028 missing-copy boundary is not after real committed A/before B/C");
            positive = outputA(folder + '/' + photoNames[0]);
            liveSettings = QJsonObject::fromVariantMap(c.exporter.settings());
            require(liveSettings["destination"].toString() == folder &&
                        liveSettings["naming"].toString() == "{filename}" &&
                        liveSettings["format"].toInt() == 1 &&
                        liveSettings["conflict"].toInt() == 0 &&
                        liveSettings["colorSpace"].toInt() == 0 &&
                        liveSettings["resize"].toInt() == 0 &&
                        liveSettings["outputMedium"].toInt() == 0 &&
                        liveSettings["metadata"].toInt() == 0 &&
                        liveSettings["watermarkKind"].toInt() == 2 &&
                        liveSettings["watermarkImage"].toString() == before.watermark &&
                        liveSettings["watermarkOpacity"].toDouble() == 100 &&
                        liveSettings["watermarkSize"].toDouble() == 20 &&
                        liveSettings["watermarkMargin"].toDouble() == 3 &&
                        liveSettings["watermarkAnchor"].toInt() == 8,
                    "EXP028 real configured settings differ from independent PNG/watermark recipe");
            evidence["live_settings_original_path"] = liveSettings;
            QStringList candidates;
            QDirIterator iterator(temporary, QStringList{"watermark"}, QDir::Files | QDir::Hidden,
                                  QDirIterator::Subdirectories);
            while (iterator.hasNext())
                candidates.append(iterator.next());
            require(candidates.size() == 1,
                    "EXP028 private snapshot discovery is not exactly one owned copy");
            const auto copy = candidates[0];
            require(QFileInfo(copy).canonicalFilePath() == copy &&
                        QFileInfo(copy).fileName() == "watermark" &&
                        QFileInfo(copy).dir().absolutePath().startsWith(temporary + '/') &&
                        QFileInfo(QFileInfo(copy).dir().absolutePath()).dir().absolutePath() ==
                            temporary &&
                        digest(copy) == before.watermarkSHA,
                    "EXP028 private watermark path/SHA/one-level ownership differs");
            identity(QFileInfo(copy).dir().absolutePath(), true);
            const auto originalIdentity = identity(before.watermark), copyIdentity = identity(copy);
            require(!(originalIdentity.device == copyIdentity.device &&
                      originalIdentity.inode == copyIdentity.inode),
                    "EXP028 snapshot is an alias of the protected original mark");
            invariant(c, before);
            evidence["committed_A_positive_control"] = positive;
            evidence["snapshot_copy"] = QJsonObject{
                {"path", copy},
                {"sha256", digest(copy)},
                {"device", QString::number(static_cast<qulonglong>(copyIdentity.device))},
                {"inode", QString::number(static_cast<qulonglong>(copyIdentity.inode))},
                {"bytes", QString::number(static_cast<qlonglong>(copyIdentity.size))}};
            persist();
            require(sameIdentity(identity(copy), copyIdentity) &&
                        digest(copy) == before.watermarkSHA,
                    "EXP028 snapshot identity changed immediately before unlink");
            require(::unlink(copy.toUtf8().constData()) == 0 && !QFileInfo::exists(copy),
                    "Cannot remove only the exact owned private watermark snapshot");
            evidence["owned_snapshot_unlinked_after_A_before_B"] = true;
            invariant(c, before);
            require(digest(folder + '/' + photoNames[0]) == positive["sha256"].toString(),
                    "EXP028 deleting snapshot changed already committed A");
            persist();
        });
    try {
        d.click("export.selection");
        d.wait([&] { return gate.caught || !c.exporter.working(); },
               "EXP028 real A completion gate reached");
        require(gate.caught, "EXP028 queue never reached real committed A boundary");
        if (gate.failure)
            std::rethrow_exception(gate.failure);
        d.wait([&] { return !c.exporter.working(); }, "EXP028 missing-copy queue terminal state",
               30000);
        invariant(c, before);
        require(QJsonObject::fromVariantMap(c.exporter.settings()) == liveSettings,
                "EXP028 external snapshot loss changed live original-path options");
        const QString missing = "Watermark image is missing or exceeds 64 MB.";
        require(clicked.count() == 1 && started == photoNames && c.exporter.total() == 3 &&
                    c.exporter.completed() == 3 && c.exporter.succeeded() == 1 &&
                    c.exporter.skipped() == 0 && c.exporter.error().isEmpty() &&
                    c.exporter.failures() ==
                        QStringList{photoNames[1] + ": " + missing, photoNames[2] + ": " + missing},
                "EXP028 missing snapshot did not yield exact two named per-file failures and "
                "continuation");
        require(
            QDir(folder).entryList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot,
                                   QDir::Name) == QStringList{photoNames[0]} &&
                outputA(folder + '/' + photoNames[0]) == positive,
            "EXP028 failure leaked partial/temp output or changed committed A");
        require(d.item("export.failures")->isVisible() &&
                    d.object("export.failures")->property("text").toString() ==
                        c.exporter.failures().join('\n'),
                "EXP028 exact per-file errors did not reach actual visible report");
        evidence["after_catalog_facts"] = catalogFacts(c);
        evidence["full_source_document_metadata_journal_guard"] = true;
        evidence["real_enqueue_clicks"] = clicked.count();
        evidence["status"] = "PASS";
        evidence["full_release_qualified"] = false;
        persist();
        d.screenshot(c.root + "/two-missing-watermark-job-failures.png");
    } catch (const std::exception &error) {
        const auto original = std::current_exception();
        evidence["status"] = "FAIL";
        evidence["failure"] = QString::fromUtf8(error.what());
        try {
            persist();
        } catch (const std::exception &saveError) {
            std::fprintf(stderr, "EXP028 failure evidence write failed: %s\n", saveError.what());
        }
        std::rethrow_exception(original);
    }
}
} // namespace
bool exportWatermarkSnapshotMissingCandidate(Context &c, const QString &id) {
    if (id != candidateID && id != officialID)
        return false;
    run(c, id);
    return true;
}
} // namespace ux
