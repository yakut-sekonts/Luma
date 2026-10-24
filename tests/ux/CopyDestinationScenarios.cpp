#include "apps/shared/PhotoDecoder.h"
#include "apps/shared/PresetStore.h"
#include "tests/ux/OwnedVolume.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QSignalSpy>
#include <QUrl>
#include <QUuid>
#include <cerrno>
#include <cstdio>
#include <exception>
#include <memory>
#include <sqlite3.h>
#include <stdexcept>
#include <sys/stat.h>
#include <tuple>
#include <utility>

// Copy variant: two generated sources and one ordinary destination volume loss.
// The existing hold-only VFS delays a genuine host WAL write; no synthetic error,
// production callback override, direct controller action, force detach or temp cleanup.
namespace ux {
namespace {
struct ReleaseWrites final {
    SqliteFault &fault;
    bool released = false;
    void release() noexcept {
        fault.holdWrites(false);
        released = true;
    }
    ~ReleaseWrites() {
        release();
    }
};
QString sha(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read private Copy evidence: " + path);
    QCryptographicHash value(QCryptographicHash::Sha256);
    require(value.addData(&file), "Cannot hash complete private Copy evidence: " + path);
    return QString::fromLatin1(value.result().toHex());
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
QJsonObject document(const luma::EditDocument &value) {
    const auto encoded = QJsonDocument::fromJson(luma::encodePreset(
        {"Copy destination complete document", {}, value, luma::allSettingsGroups}));
    require(encoded.isObject(), "Cannot retain full Copy document evidence");
    return encoded.object();
}
QJsonObject state(Context &c) {
    return {{"importing", c.library.importing()},
            {"working", c.library.working()},
            {"loading", c.library.loading()},
            {"count", c.library.count()},
            {"progress", c.library.progress()},
            {"status", c.library.status()},
            {"error", c.library.error()},
            {"report", c.library.importReport()},
            {"write_blocked", c.storageFault.writeBlocked()},
            {"rejected_writes", c.storageFault.rejectedWrites()},
            {"editor_has_photo", c.editor.hasPhoto()},
            {"editor_uuid", c.editor.photoID()},
            {"editor_source", c.editor.sourcePath()},
            {"editor_error", c.editor.error()},
            {"editor_busy", c.editor.busy()},
            {"editor_saving", c.editor.saving()}};
}
QJsonArray rows(sqlite3 *db, const char *sql) {
    sqlite3_stmt *raw = nullptr;
    const int prepared = sqlite3_prepare_v2(db, sql, -1, &raw, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw,
                                                                               sqlite3_finalize);
    require(prepared == SQLITE_OK, "Cannot prepare read-only Copy audit");
    QJsonArray result;
    int status;
    while ((status = sqlite3_step(raw)) == SQLITE_ROW) {
        QJsonObject row;
        for (int column = 0; column < sqlite3_column_count(raw); ++column) {
            const auto name = QString::fromUtf8(sqlite3_column_name(raw, column));
            switch (sqlite3_column_type(raw, column)) {
            case SQLITE_NULL:
                row[name] = QJsonValue(QJsonValue::Null);
                break;
            case SQLITE_INTEGER:
                row[name] = static_cast<qint64>(sqlite3_column_int64(raw, column));
                break;
            case SQLITE_FLOAT:
                row[name] = sqlite3_column_double(raw, column);
                break;
            case SQLITE_TEXT:
                row[name] = QString::fromUtf8(
                    reinterpret_cast<const char *>(sqlite3_column_text(raw, column)),
                    sqlite3_column_bytes(raw, column));
                break;
            default:
                require(false, "Unexpected BLOB in Copy audit");
            }
        }
        result.append(row);
    }
    require(status == SQLITE_DONE, "Read-only Copy audit stopped before its complete result");
    return result;
}
QJsonObject audit(Context &c) {
    sqlite3 *raw = nullptr;
    const int opened = sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &raw,
                                       SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
    require(opened == SQLITE_OK && sqlite3_busy_timeout(raw, 1000) == SQLITE_OK &&
                sqlite3_exec(raw, "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK,
            "Cannot begin bounded read-only Copy snapshot");
    QJsonObject result{
        {"sessions", rows(raw, "SELECT * FROM import_sessions ORDER BY id")},
        {"items", rows(raw, "SELECT * FROM import_items ORDER BY session_id,position")},
        {"assets", rows(raw, "SELECT * FROM assets ORDER BY id")},
        {"documents", rows(raw, "SELECT * FROM edit_documents ORDER BY asset_id")},
        {"active", rows(raw, "SELECT * FROM session ORDER BY slot")},
        {"foreign_key_errors", rows(raw, "PRAGMA foreign_key_check")}};
    require(sqlite3_exec(raw, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK,
            "Cannot finish read-only Copy snapshot");
    return result; // Statements, transaction and connection close before caller can detach.
}
QJsonObject proof(const QJsonObject &snapshot, const QString &id) {
    QJsonObject session;
    int matches = 0;
    for (const auto &value : snapshot["sessions"].toArray())
        if (value.toObject()["id"].toString() == id) {
            session = value.toObject();
            ++matches;
        }
    require(matches == 1, "Copy session UUID disappeared or is ambiguous");
    QJsonArray items;
    for (const auto &value : snapshot["items"].toArray())
        if (value.toObject()["session_id"].toString() == id)
            items.append(value);
    require(items.size() == 2, "Copy session does not retain exactly two journal items");
    return {{"session", session}, {"items", items}};
}
void sessionIdentity(const QJsonObject &value, const QString &destination, bool skip,
                     const QString &status) {
    const auto row = value["session"].toObject();
    require(!QUuid(row["id"].toString()).isNull() && row["mode"].toString() == "copy" &&
                row["destination"].toString() == destination &&
                row["owner_pid"].toInteger(-1) == QCoreApplication::applicationPid() &&
                row["skip_duplicates"].toInt(-1) == int(skip) &&
                row["state"].toString() == status &&
                (status == "running" ? row["finished_at"].isNull()
                                     : !row["finished_at"].toString().isEmpty()),
            "Copy session identity/mode/owner/destination/terminal timestamp mismatch");
}
QJsonObject item(const QJsonObject &value, int position, const QString &source,
                 const QString &status) {
    const auto row = value["items"].toArray()[position].toObject();
    require(row["position"].toInt(-1) == position &&
                row["session_id"] == value["session"].toObject()["id"] &&
                row["source_path"].toString() == source && row["state"].toString() == status,
            "Copy journal item crossed source/session/position/state identities");
    return row;
}
void unStaged(const QJsonObject &row) {
    for (const auto *key : {"target_path", "temporary_path", "checksum"})
        require(row[key].isString() && row[key].toString().isEmpty(),
                "Unstaged Copy item unexpectedly retained partial target/temp/checksum fields");
    require(row["filesize"].toInteger(-1) == 0, "Unstaged Copy item has a partial recorded size");
}
void staged(const QJsonObject &row, const QString &target, const QString &temporary,
            const QString &checksum, qint64 size) {
    require(row["target_path"].toString() == target &&
                row["temporary_path"].toString() == temporary &&
                row["checksum"].toString() == checksum && row["filesize"].toInteger(-1) == size,
            "Copy staged/committed fields crossed published-path/SHA/size identities");
}
QJsonObject published(const QString &target, const QString &temporary, const QString &mount,
                      const QString &expected, qint64 size) {
    struct stat a{}, b{}, volume{};
    require(::lstat(QFile::encodeName(target).constData(), &a) == 0 &&
                ::stat(QFile::encodeName(mount).constData(), &volume) == 0 && S_ISREG(a.st_mode) &&
                a.st_dev == volume.st_dev && a.st_nlink >= 1 && a.st_size == size &&
                QFileInfo(target).canonicalFilePath() == target && sha(target) == expected,
            "Held Copy has no complete independently verified real final-file publication");
    errno = 0;
    const bool temporaryPresent = ::lstat(QFile::encodeName(temporary).constData(), &b) == 0;
    require(temporaryPresent || errno == ENOENT,
            "Cannot establish actual optional temporary-path presence at Copy boundary");
    QJsonObject result{{"target", target},
                       {"temporary", temporary},
                       {"sha256", expected},
                       {"size", size},
                       {"device", QString::number(quint64(a.st_dev))},
                       {"inode", QString::number(quint64(a.st_ino))},
                       {"link_count", QString::number(quint64(a.st_nlink))},
                       {"temporary_present", temporaryPresent}};
    if (temporaryPresent) {
        require(S_ISREG(b.st_mode) && a.st_dev == b.st_dev && a.st_ino == b.st_ino &&
                    a.st_nlink >= 2 && b.st_nlink >= 2 && b.st_size == size &&
                    QFileInfo(temporary).canonicalFilePath() == temporary &&
                    sha(temporary) == expected,
                "Existing Copy temporary does not match the independently verified publication");
        result["temporary_device"] = QString::number(quint64(b.st_dev));
        result["temporary_inode"] = QString::number(quint64(b.st_ino));
        result["temporary_link_count"] = QString::number(quint64(b.st_nlink));
    }
    // A correct production implementation may unlink its temporary name before
    // catalog commit. Record 0/1 here; only the post-remount no-temp is acceptance.
    return result;
}
QJsonObject inventory(const QString &destination) {
    require(QFileInfo(destination).isDir() && !QFileInfo(destination).isSymLink(),
            "Cannot audit the actual remounted Copy destination");
    QJsonObject result;
    QDirIterator iterator(destination,
                          QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories); // Deliberately no FollowSymlinks.
    while (iterator.hasNext()) {
        const auto path = iterator.next();
        const auto info = iterator.fileInfo();
        const auto relative = QDir(destination).relativeFilePath(path);
        QJsonObject value{{"type", info.isSymLink() ? "symlink"
                                   : info.isDir()   ? "directory"
                                   : info.isFile()  ? "file"
                                                    : "other"},
                          {"size", info.size()}};
        if (!info.isSymLink() && info.isFile()) {
            require(info.size() <= 8LL * 1024 * 1024,
                    "Unexpected oversized file in small Copy fixture");
            value["sha256"] = sha(path);
        }
        result[relative] = value;
        require(result.size() <= 32,
                "Small Copy fixture unexpectedly created more than 32 entries");
    }
    return result;
}
void exactTree(const QJsonObject &tree, const QSet<QString> &directories,
               const QMap<QString, QString> &files) {
    require(
        tree.size() == directories.size() + files.size(),
        "Copy remount retained a temporary hard link, unregistered file or unexpected directory");
    for (auto it = tree.begin(); it != tree.end(); ++it) {
        const auto entry = it.value().toObject();
        require((directories.contains(it.key()) && entry["type"].toString() == "directory") ||
                    (files.contains(it.key()) && entry["type"].toString() == "file" &&
                     entry["sha256"].toString() == files[it.key()]),
                "Copy retained unexpected path/type/bytes after remount: " + it.key());
    }
}
luma::AssetSnapshot saved(Context &c, const QString &id) {
    const auto value = luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(id.toStdString());
    if (!value)
        throw std::runtime_error("Copy asset UUID disappeared from the host catalog");
    return *value;
}
void asset(const luma::AssetSnapshot &value, const QString &id, const QString &path,
           const QString &checksum, const luma::PhotoMetadata &metadata) {
    require(!QUuid(id).isNull() && value.asset.id == id.toStdString() &&
                value.asset.path.string() == path.toStdString() &&
                value.asset.checksum == checksum.toStdString() &&
                sameMetadata(value.asset.metadata, metadata) &&
                value.asset.edits == luma::EditDocument{} && value.revision == 0,
            "Copy asset lost complete default document/revision/UUID/path/SHA/metadata identity");
}
void opened(Context &c, const QString &id, const QString &path, const QImage &pixels) {
    auto &d = *c.driver;
    if (!c.window->property("libraryMode").toBool())
        d.click("main.libraryButton");
    // Genuine QML delegate population, without Context::card/model.data page priming.
    d.wait([&] { return d.find("library.card." + id) != nullptr; }, "actual remounted Copy card");
    d.doubleClick("library.card." + id);
    c.loaded();
    require(c.editor.photoID() == id && c.editor.sourcePath() == path &&
                c.editor.filename() == QFileInfo(path).fileName() &&
                c.editor.currentSettings() == luma::EditDocument{} && !c.editor.canUndo() &&
                !c.editor.canRedo(),
            "Actual Copy card loaded a wrong source/full document/history");
    auto condition = [&] {
        auto *image = d.item("develop.preview");
        return !c.editor.busy() && c.editor.hasPhoto() && image->isVisible() &&
               image->property("status").toInt() == 1 && !c.editor.previewUrl().isEmpty() &&
               image->property("source").toUrl().toString() == c.editor.previewUrl() &&
               c.provider->requestImage({}, nullptr, {}) == pixels;
    };
    d.wait(condition,
           "actual Copy card current URL/Image Ready/provider pixels match host original");
    d.frame();
    require(condition(), "Copy preview changed after its accepted source boundary");
}
void start(Context &c, const QList<QUrl> &sources, const QString &destination, bool skip) {
    auto &d = *c.driver;
    if (!c.window->property("libraryMode").toBool())
        d.click("main.libraryButton");
    require(QFileInfo(destination).canonicalFilePath() == destination &&
                d.object("library.importDialog")
                    ->setProperty("sources", QVariant::fromValue(sources)) &&
                d.object("library.importDialog")
                    ->setProperty("destination", QUrl::fromLocalFile(destination)),
            "Actual Copy pane rejected known existing file/folder-panel results");
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "actual two-source Copy dialog opened");
    d.choose("library.importMode", 1);
    for (const auto &setting : {std::pair<QString, bool>{"library.importRecursive", false},
                                {"library.importDuplicates", skip}})
        if (d.object(setting.first)->property("checked").toBool() != setting.second)
            d.click(setting.first);
    d.click("library.importStart");
    d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
           "actual Copy setup modal fully closed");
}
void terminal(Context &c) {
    c.driver->wait(
        [&] {
            return !c.library.importing() && !c.library.working() && !c.library.loading() &&
                   !c.storageFault.writeBlocked();
        },
        "actual Copy worker and normal model reload settled", 60000);
    require(c.library.error().isEmpty() && c.storageFault.rejectedWrites() == 0,
            "Copy stopped through an unrelated controller/catalog/injected error");
}
void summary(Context &c, const QString &id, int added, int duplicate, int failed) {
    luma::Catalog catalog(c.catalogFile.toStdString()); // Only after real hold release.
    const auto value = catalog.importSummary(id.toStdString());
    require(value.sessionID == id.toStdString() && value.added == added &&
                value.duplicates == duplicate && value.failed == failed && value.cancelled == 0 &&
                value.added + value.duplicates + value.failed == 2 &&
                catalog.unfinishedImports().empty(),
            "Copy terminal summary lost/misclassified an item or left an unfinished session");
}
} // namespace

bool copyDestinationScenario(Context &c, const QString &id, const QString &variant) {
    const bool historicalCandidate = id == "CANDIDATE-MAC008-COPY-DESTINATION" && variant.isEmpty();
    const bool official = id == "UX-MAC-008" && variant == "copy";
    if (!historicalCandidate && !official)
        return false;
    auto &d = *c.driver;
    require(!c.library.working() && !c.library.loading() && !c.library.importing() &&
                c.library.count() == 0 && !c.editor.hasPhoto() && !c.editor.busy() &&
                !c.editor.saving() && !c.editor.exporting() && !c.exporter.working() &&
                !c.batch.working() && c.storageFault.rejectedWrites() == 0 &&
                !c.storageFault.writeBlocked() &&
                QFileInfo(c.catalogFile)
                    .canonicalFilePath()
                    .startsWith(QFileInfo(c.root).canonicalFilePath() + '/'),
            "Copy destination requires a fresh settled host Context");
    const auto empty = audit(c);
    for (const auto *key :
         {"assets", "documents", "active", "sessions", "items", "foreign_key_errors"})
        require(empty[key].toArray().isEmpty(), "Copy fixture is not a pristine isolated catalog");
    const QString originals[]{c.fixture + "/Photo-0000.png", c.fixture + "/Photo-0001.png"};
    const QString hashes[]{sha(originals[0]), sha(originals[1])};
    require(hashes[0] != hashes[1], "Copy sources need distinct independent SHA identities");
    const luma::PhotoMetadata metadata[]{luma::PhotoDecoder().inspect(originals[0].toStdString()),
                                         luma::PhotoDecoder().inspect(originals[1].toStdString())};
    const QImage references[]{fixtureReference(c, luma::EditDocument{}, originals[0]),
                              fixtureReference(c, luma::EditDocument{}, originals[1])};
    require(references[0] != references[1],
            "Copy provider pixel oracles cannot distinguish the two source images");
    const auto sourceRoot = c.root + "/copy-host-sources";
    require(!QFileInfo::exists(sourceRoot) && QDir().mkdir(sourceRoot) &&
                QFileInfo(sourceRoot)
                    .canonicalFilePath()
                    .startsWith(QFileInfo(c.root).canonicalFilePath() + '/'),
            "Cannot exclusively create private Copy host-source directory");
    const QString sources[]{sourceRoot + "/Control-A.png", sourceRoot + "/Source-B.png"};
    for (int index = 0; index != 2; ++index)
        require(QFile::copy(originals[index], sources[index]) &&
                    QFileInfo(sources[index]).canonicalFilePath() == sources[index] &&
                    sha(sources[index]) == hashes[index] && metadata[index].width <= 512 &&
                    metadata[index].height <= 512 &&
                    QFileInfo(sources[index]).size() <= 8LL * 1024 * 1024,
                "Cannot prepare intact small private Copy source");
    auto originalHashes = [&] {
        QJsonObject result;
        for (int index = 0; index != 2; ++index) {
            const auto actual = sha(sources[index]), shared = sha(originals[index]);
            result[sources[index]] = actual;
            result[originals[index]] = shared;
            require(actual == hashes[index] && shared == hashes[index] &&
                        !QFileInfo(sources[index]).isSymLink(),
                    "Destination loss/recovery changed independent host source bytes");
        }
        return result;
    };
    const QList<QUrl> input{QUrl::fromLocalFile(sources[0]), QUrl::fromLocalFile(sources[1])};
    QString phase = "before_volume_create", destination, firstID, firstTarget, firstTemporary;
    OwnedVolume volume(d, c.root);
    ReleaseWrites release{
        c.storageFault}; // Destruction releases before volume/controller shutdown.
    try {
        volume.createAndAttach();
        const auto mount = volume.mountPath(), volumeID = volume.volumeUUID();
        destination = mount + "/Copies";
        require(!volumeID.isEmpty() && absent(destination) && QDir().mkdir(destination) &&
                    QFileInfo(destination).canonicalFilePath() == destination &&
                    inventory(destination).isEmpty(),
                "Copy hold root must be an exclusively owned new empty mounted subdirectory");
        QSignalSpy starts(d.object("library.importStart"), SIGNAL(clicked()));
        QSignalSpy opens(&c.library, &LibraryModel::openRequested);
        QSignalSpy loads(&c.editor, &EditorController::photoLoaded);
        require(starts.isValid() && opens.isValid() && loads.isValid(),
                "Cannot observe genuine Copy Start/card activation inputs");
        phase = "first_actual_copy_pending_catalog_write";
        c.storageFault.holdWritesAfterCopy(destination.toStdString(), 0);
        start(c, input, destination, false);
        d.wait([&] { return c.storageFault.writeBlocked() && c.library.importing(); },
               "real first Copy published and pending host catalog/WAL write held");
        const auto held = audit(c); // Raw READONLY only: no Catalog constructor/write/migration.
        volume.checkpoint("copy_raw_held_catalog_before_acceptance",
                          {{"raw_catalog", held}, {"actual_state", state(c)}});
        require(starts.count() == 1 && held["sessions"].toArray().size() == 1 &&
                    held["assets"].toArray().isEmpty() && held["documents"].toArray().isEmpty() &&
                    held["active"].toArray().isEmpty() &&
                    held["foreign_key_errors"].toArray().isEmpty(),
                "Copy hold missed its first uncommitted published asset boundary");
        firstID = held["sessions"].toArray()[0].toObject()["id"].toString();
        const auto heldProof = proof(held, firstID);
        sessionIdentity(heldProof, destination, false, "running");
        const auto first = item(heldProof, 0, sources[0], "staged");
        const auto pending = item(heldProof, 1, sources[1], "queued");
        const QString firstRelative = "Control-A.png";
        firstTarget = destination + '/' + firstRelative;
        firstTemporary = destination + "/.luma-copy-" + firstID + "-0.tmp";
        staged(first, firstTarget, firstTemporary, hashes[0], QFileInfo(sources[0]).size());
        unStaged(pending);
        require(first["asset_id"].isNull() && first["error"].toString().isEmpty() &&
                    pending["asset_id"].isNull() && pending["error"].toString().isEmpty(),
                "Held Copy journal already committed/failed the pending item");
        const auto publishedProof =
            published(firstTarget, firstTemporary, mount, hashes[0], QFileInfo(sources[0]).size());
        QMap<QString, QString> publishedFiles{{firstRelative, hashes[0]}};
        if (publishedProof["temporary_present"].toBool())
            publishedFiles[".luma-copy-" + firstID + "-0.tmp"] = hashes[0];
        const auto heldTree = inventory(destination);
        exactTree(heldTree, {}, publishedFiles);
        // published()/audit()/source hashing close all readers before detach.
        volume.pinOriginals({{"Copies/" + firstRelative, hashes[0]}});
        const auto beforeDetachHashes = originalHashes();
        require(c.storageFault.writeBlocked() && c.library.importing() &&
                    c.storageFault.rejectedWrites() == 0 && !c.editor.hasPhoto(),
                "Real pending Copy boundary advanced or injected an unrelated error");
        volume.checkpoint("copy_destination_before_ordinary_detach",
                          {{"actual_state", state(c)},
                           {"raw_catalog", held},
                           {"published_hardlink", publishedProof},
                           {"held_destination_inventory", heldTree},
                           {"independent_source_sha256", beforeDetachHashes}});
        phase = "ordinary_detach_while_real_copy_write_held";
        volume.detach();
        require(volume.detached() && absent(mount) && absent(destination) && absent(firstTarget) &&
                    absent(firstTemporary) && c.storageFault.writeBlocked() &&
                    c.library.importing(),
                "Copy destination did not truly disappear at its held real publication boundary");
        volume.checkpoint("copy_destination_absent_before_real_write_release",
                          {{"actual_state", state(c)},
                           {"mount_absent", true},
                           {"destination_absent", true},
                           {"independent_source_sha256", originalHashes()}});
        phase = "real_copy_write_released_destination_still_absent";
        release.release(); // Actual xWrite resumes unchanged; never Cancel on normal path.
        terminal(c);
        const auto failed = audit(c);
        volume.checkpoint("copy_actual_terminal_raw_before_acceptance",
                          {{"raw_catalog", failed}, {"actual_state", state(c)}});
        const auto initial = proof(failed, firstID);
        sessionIdentity(initial, destination, false, "completed");
        const auto committedA = item(initial, 0, sources[0], "committed");
        const auto failedB = item(initial, 1, sources[1], "failed");
        staged(committedA, firstTarget, firstTemporary, hashes[0], QFileInfo(sources[0]).size());
        unStaged(failedB);
        const auto aID = committedA["asset_id"].toString();
        const auto actualError = failedB["error"].toString();
        require(
            !QUuid(aID).isNull() && committedA["error"].toString().isEmpty() &&
                failedB["asset_id"].isNull() && !actualError.isEmpty() &&
                actualError.contains(mount) && failed["sessions"].toArray().size() == 1 &&
                failed["assets"].toArray().size() == 1 &&
                failed["documents"].toArray().size() == 1 && failed["active"].toArray().isEmpty() &&
                failed["foreign_key_errors"].toArray().isEmpty() && c.library.count() == 1 &&
                !c.editor.hasPhoto() && absent(mount) && absent(destination) &&
                absent(firstTarget) && absent(firstTemporary),
            "Missing Copy destination produced partial/wrong rows or a host filesystem fallback");
        summary(c, firstID, 1, 0, 1);
        const auto aBefore = saved(c, aID);
        asset(aBefore, aID, firstTarget, hashes[0], metadata[0]);
        require(
            c.library.importReport() == sources[1] + '\n' + actualError &&
                d.item("library.importReportButton")->isVisible(),
            "Real Copy per-file failure omitted exact failed source/destination from its report");
        d.click("library.importReportButton");
        d.wait([&] { return d.object("library.importReportDialog")->property("opened").toBool(); },
               "actual missing destination Import report opened");
        require(d.object("library.importReportText")->property("text").toString() ==
                        c.library.importReport() &&
                    d.item("library.importReportText")->isVisible(),
                "Visible Copy report does not show its exact real filesystem failure");
        volume.checkpoint("copy_destination_real_failure_before_remount",
                          {{"actual_state", state(c)},
                           {"raw_catalog", failed},
                           {"a_uuid", aID},
                           {"a_full_document", document(aBefore.asset.edits)},
                           {"actual_destination_error", actualError},
                           {"independent_source_sha256", originalHashes()}});
        d.screenshot(c.root + "/copy-destination-missing-report.png");
        d.key(Qt::Key_Escape);
        d.wait(
            [&] { return !d.object("library.importReportDialog")->property("visible").toBool(); },
            "actual Copy failure report closed");
        phase = "same_image_copy_destination_remount_before_strict_temp_oracle";
        volume.remount();
        require(volume.attached() && volume.volumeUUID() == volumeID &&
                    volume.mountPath() == mount &&
                    QFileInfo(destination).canonicalFilePath() == destination,
                "Copy remount did not restore the exact own image/path/VolumeUUID");
        const auto remountedTree = inventory(destination);
        const auto remountedAudit = audit(c);
        volume.checkpoint("copy_destination_raw_remount_before_any_residue_assertion",
                          {{"actual_state", state(c)},
                           {"destination_inventory", remountedTree},
                           {"raw_catalog", remountedAudit},
                           {"a_uuid", aID},
                           {"independent_source_sha256", originalHashes()}});
        // Strict original oracle: never remove or ignore an old .luma-copy-* link.
        // If it survived the unlink attempted while unmounted, this MUST FAIL here.
        phase = "strict_original_no_old_copy_temporary_hardlink";
        exactTree(remountedTree, {}, {{firstRelative, hashes[0]}});
        require(absent(firstTemporary) && proof(remountedAudit, firstID) == initial &&
                    sha(firstTarget) == hashes[0],
                "Copy remount changed its terminal journal/source or retained its temporary link");
        const auto aRemounted = saved(c, aID);
        asset(aRemounted, aID, firstTarget, hashes[0], metadata[0]);
        opened(c, aID, firstTarget, references[0]);
        require(opens.count() == 1 && loads.count() == 1 &&
                    opens[0][0].toUrl().toLocalFile() == firstTarget,
                "Remounted first Copy activation did not route exactly once");
        phase = "actual_duplicate_skipping_copy_retry";
        start(c, input, destination, true); // Hold remains released, never rearmed.
        terminal(c);
        const auto recovered = audit(c);
        require(starts.count() == 2 && recovered["sessions"].toArray().size() == 2 &&
                    recovered["items"].toArray().size() == 4 &&
                    recovered["assets"].toArray().size() == 2 &&
                    recovered["documents"].toArray().size() == 2 &&
                    recovered["foreign_key_errors"].toArray().isEmpty() && c.library.count() == 2 &&
                    proof(recovered, firstID) == initial && c.library.importReport().isEmpty() &&
                    !d.item("library.importReportButton")->isVisible(),
                "Copy retry damaged initial session/membership or failed to clear its UI report");
        QString retryID;
        for (const auto &value : recovered["sessions"].toArray())
            if (value.toObject()["id"].toString() != firstID)
                retryID = value.toObject()["id"].toString();
        const auto retry = proof(recovered, retryID);
        sessionIdentity(retry, destination, true, "completed");
        const auto duplicateA = item(retry, 0, sources[0], "duplicate");
        const auto committedB = item(retry, 1, sources[1], "committed");
        unStaged(duplicateA);
        require(duplicateA["asset_id"].toString() == aID &&
                    duplicateA["error"].toString().isEmpty(),
                "Copy retry duplicated A or changed its existing UUID");
        const QString bRelative = "Source-B.png";
        const auto bTarget = destination + '/' + bRelative;
        const auto bTemporary = destination + "/.luma-copy-" + retryID + "-1.tmp";
        staged(committedB, bTarget, bTemporary, hashes[1], QFileInfo(sources[1]).size());
        const auto bID = committedB["asset_id"].toString();
        require(!QUuid(bID).isNull() && bID != aID && committedB["error"].toString().isEmpty() &&
                    absent(bTemporary) && absent(firstTemporary),
                "Copy retry retained a temporary path or crossed A/B UUID identities");
        summary(c, retryID, 1, 1, 0);
        summary(c, firstID, 1, 0, 1);
        asset(saved(c, aID), aID, firstTarget, hashes[0], metadata[0]);
        asset(saved(c, bID), bID, bTarget, hashes[1], metadata[1]);
        const auto finalTree = inventory(destination);
        exactTree(finalTree, {}, {{firstRelative, hashes[0]}, {bRelative, hashes[1]}});
        opened(c, bID, bTarget, references[1]);
        const auto durableActive = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
        require(opens.count() == 2 && loads.count() == 2 &&
                    opens[1][0].toUrl().toLocalFile() == bTarget && durableActive &&
                    durableActive->id == bID.toStdString(),
                "Copy recovery did not activate and persist the exact second UUID/source");
        const auto beforeFinalize = audit(c);
        require(proof(beforeFinalize, firstID) == initial &&
                    proof(beforeFinalize, retryID) == retry,
                "Actual recovered card input changed either terminal Copy journal");
        volume.checkpoint("copy_destination_recovered_strict_two_source_state",
                          {{"actual_state", state(c)},
                           {"raw_catalog", beforeFinalize},
                           {"destination_inventory", finalTree},
                           {"a_full_document", document(saved(c, aID).asset.edits)},
                           {"b_full_document", document(saved(c, bID).asset.edits)},
                           {"independent_source_sha256", originalHashes()}});
        phase = "explicit_checked_copy_finalize";
        volume.finalize();
        require(volume.detached() && absent(mount) && absent(destination) &&
                    !c.storageFault.writeBlocked() && c.storageFault.rejectedWrites() == 0 &&
                    audit(c) == beforeFinalize && c.library.count() == 2 &&
                    c.library.error().isEmpty() && c.library.importReport().isEmpty() &&
                    c.editor.hasPhoto() && !c.editor.busy() && !c.editor.saving() &&
                    c.editor.error().isEmpty() && c.editor.photoID() == bID &&
                    c.editor.sourcePath() == bTarget &&
                    c.editor.currentSettings() == luma::EditDocument{} && !c.editor.canUndo() &&
                    !c.editor.canRedo(),
                "Copy candidate success requires checked final detach and unchanged host catalog");
        originalHashes();
        asset(saved(c, aID), aID, firstTarget, hashes[0], metadata[0]);
        asset(saved(c, bID), bID, bTarget, hashes[1], metadata[1]);
        c.checkData();
        writeJson(c.root + "/measurements.json",
                  {{"scenario", id},
                   {"variant", "copy"},
                   {"scope", "REAL_COPY_PUBLICATION_PENDING_HOST_WAL_WRITE"},
                   {"sources", 2},
                   {"actual_import_start_clicks", starts.count()},
                   {"actual_card_open_requests", opens.count()},
                   {"actual_successful_loads", loads.count()},
                   {"initial_session", initial},
                   {"retry_session", retry},
                   {"strict_remounted_no_old_temporary_link", true},
                   {"final_tree", finalTree},
                   {"same_volume_uuid", volumeID},
                   {"actual_destination_error", actualError},
                   {"checked_finalize_detached", true},
                   {"volume_evidence", volume.evidenceRoot()},
                   {"no_host_fallback", true},
                   {"hold_released", release.released},
                   {"injected_io_errors", c.storageFault.rejectedWrites()},
                   {"physical_usb_exercised", false},
                   {"mid_open_write_exercised", false},
                   {"hardware_input_exercised", false},
                   {"input", "QT_QTEST_WINDOW_INPUT"},
                   {"actual_scene_readback_claimed", false},
                   {"renderer_mathematical_correctness_claimed", false}});
        return true;
    } catch (...) {
        const auto originalFailure = std::current_exception();
        // Inside guard/volume lifetime: record the actual held/job/UI state first.
        try {
            volume.checkpoint("copy_destination_failure_before_hold_release_or_cleanup",
                              {{"phase", phase},
                               {"actual_state", state(c)},
                               {"destination", destination},
                               {"first_session", firstID},
                               {"first_target", firstTarget},
                               {"first_temporary", firstTemporary},
                               {"hold_released", release.released}});
            if (volume.attached() && destination == volume.mountPath() + "/Copies" &&
                QFileInfo(destination).canonicalFilePath() == destination)
                volume.checkpoint("copy_destination_failure_inventory",
                                  {{"destination_inventory", inventory(destination)}});
            volume.checkpoint("copy_destination_failure_readonly_catalog",
                              {{"phase", phase},
                               {"raw_catalog", audit(c)},
                               {"independent_source_sha256", originalHashes()}});
        } catch (...) {
            std::fputs("Copy destination evidence failed; original failure and image retained.\n",
                       stderr);
        }
        // Cancel is a genuine UI request; it cannot finish a held write by itself.
        // Never let a failed click skip the unconditional atomic release below.
        try {
            if (c.library.importing())
                d.click("library.cancelImport");
        } catch (...) {
            std::fputs("Copy destination UI Cancel failed; releasing held writer regardless.\n",
                       stderr);
        }
        release.release();
        try {
            d.wait(
                [&] {
                    return !c.library.importing() && !c.library.working() && !c.library.loading() &&
                           !c.storageFault.writeBlocked();
                },
                "failed Copy candidate real worker settled after unconditional release", 10000);
        } catch (...) {
            std::fputs("Copy destination worker cleanup timed out; no force detach or PASS.\n",
                       stderr);
        }
        std::rethrow_exception(originalFailure);
    }
}
} // namespace ux
