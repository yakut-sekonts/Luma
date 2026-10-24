#include "apps/shared/PhotoDecoder.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/FileHasher.h"
#include "tests/ux/CrashScenarios.h"
#include "tests/ux/PersistenceProbe.h"
#include "tests/ux/UxContext.h"
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <algorithm>
#include <map>

namespace ux {
namespace {
using Backups = std::map<std::filesystem::path, std::string>;
Backups savedBackups(const QUrl &folder) {
    Backups result;
    const QDir directory(folder.toLocalFile());
    for (const auto &name : directory.entryList({"auto-*.sqlite3"}, QDir::Files, QDir::Name)) {
        const auto path = std::filesystem::path(directory.filePath(name).toStdString());
        result.emplace(path, luma::macos::sha256File(path));
    }
    return result;
}
void openMaintenance(Context &c) {
    auto &d = *c.driver;
    d.click("settingsButton");
    d.wait([&] { return d.object("preferencesPanel")->property("opened").toBool(); },
           "Backup preferences opened");
    d.click("settings.catalogMaintenance");
    d.wait([&] { return d.object("catalogMaintenance")->property("opened").toBool(); },
           "Catalog maintenance opened through the actual settings button");
}
void closeMaintenance(Context &c) {
    auto &d = *c.driver;
    for (const auto *name : {"catalogMaintenance", "preferencesPanel"}) {
        d.key(Qt::Key_Escape);
        d.wait([&] { return !d.object(name)->property("visible").toBool(); },
               "Catalog preferences dismissed");
    }
}
} // namespace
bool catalogMaintenanceScenario(Context &c, const QString &id) {
    const bool daily = id == "UX-P10-DAY-010" || id == "UX-P10-DAT-005";
    const bool restore = id == "UX-P09-BKP-002" || daily;
    const bool corrupt = id == "UX-P09-BKP-006";
    const bool active = id == "UX-P09-BKP-007";
    if (id != "UX-P09-BKP-001" && !restore && !corrupt && !active)
        return false;
    auto &d = *c.driver;
    c.import(1);
    d.doubleClick(c.card(0));
    c.loaded();
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {1., .5});
    c.saved();
    require(c.editor.currentSettings().exposure == 5 && c.editor.canUndo() && !c.editor.canRedo(),
            "Backup fixture needs a saved real UI edit and one Undo entry");
    if (daily) {
        c.addMask(2);
        d.drag("localMaskGestureArea", {.2, .3}, {.8, .7});
        d.wait([&] { return c.editor.localCanvasReady(); }, "Backup brush rendered");
        c.reveal("local_exposure");
        d.drag("local_exposure", d.sliderThumb("local_exposure"), {.6, .5});
        c.saved();
        const auto document = c.editor.currentSettings();
        require(document.localMasks.size() == 1 && !document.localMasks[0].strokes.empty() &&
                    document.localMasks[0].adjustments[0] > 0,
                "Daily backup requires committed local geometry and adjustments");
    }
    const auto photo = c.editor.photoID().toStdString();
    luma::Catalog catalog(c.catalogFile.toStdString());
    auto expected = catalog.assetSnapshot(photo);
    require(expected.has_value(), "Backup fixture has no saved asset");
    const auto originalHash = luma::macos::sha256File(expected->asset.path);
    const auto members = catalog.libraryIDs({});
    const auto selected = c.library.selectionIDs();
    QSignalSpy saves(&c.editor, &EditorController::editsSaved);
    require(saves.isValid(), "Cannot observe unexpected edits during manual backup");

    openMaintenance(c);
    std::filesystem::path backup;
    for (int repetition = 0; repetition < 2; ++repetition) {
        const auto before = savedBackups(c.library.backupFolder());
        require(!before.empty(), "Startup must already have a good backup to preserve");
        d.click("catalog.backupNow");
        d.wait([&] { return !c.library.working() && !c.library.loading(); },
               "Manual backup worker completed");
        require(c.library.error().isEmpty(), c.library.error());
        auto added = savedBackups(c.library.backupFolder());
        for (const auto &[path, hash] : before) {
            require(added.contains(path) && added.at(path) == hash,
                    "Manual backup removed or changed a previous good snapshot");
            added.erase(path);
        }
        require(added.size() == 1, "Back up now did not create exactly one new snapshot");
        const auto &path = added.begin()->first;
        backup = path;
        luma::Catalog::verifyBackup(path);
        luma::Catalog snapshot(path);
        const auto copied = snapshot.assetSnapshot(photo);
        require(copied && copied->asset.id == expected->asset.id &&
                    copied->asset.path == expected->asset.path &&
                    copied->asset.checksum == expected->asset.checksum &&
                    copied->asset.edits == expected->asset.edits &&
                    copied->revision == expected->revision && snapshot.libraryIDs({}) == members &&
                    snapshot.activeAsset() && snapshot.activeAsset()->id == photo,
                "Readable backup lost the current photo identity, saved edits or selection");
        require(c.library.maintenanceStatus().contains(QString::fromStdString(path.string())) &&
                    d.object("catalog.status")->property("text").toString() ==
                        c.library.maintenanceStatus() &&
                    d.object("catalog.backupNow")->property("enabled").toBool(),
                "Completed backup did not show its destination or re-enable its control");
        // A real source-directory event must not replace a completed operation's
        // destination. The extra file is owned, non-image and removed by RAII.
        QSignalSpy notifications(&c.library, &LibraryModel::stateChanged);
        QTemporaryFile notice(c.fixture + "/.backup-notice-XXXXXX");
        require(notifications.isValid() && notice.open(), "Cannot create source notice fixture");
        notice.close();
        d.wait(
            [&] {
                return !notifications.empty() &&
                       c.library.status().startsWith("Source folder changed.");
            },
            "Actual source watcher notification delivered after backup");
        require(d.object("catalog.status")->property("text").toString() ==
                        c.library.maintenanceStatus() &&
                    c.library.maintenanceStatus().contains(QString::fromStdString(path.string())) &&
                    d.object("catalog.notice")->property("visible").toBool() &&
                    d.object("catalog.notice")->property("text").toString() == c.library.status(),
                "Background folder notice replaced the backup result or disappeared");
        d.screenshot(c.root + "/backup-result-" + QString::number(repetition + 1) + ".png");
        d.log({{"action", "manual_catalog_backup"},
               {"repetition", repetition + 1},
               {"snapshot", QString::fromStdString(path.string())},
               {"revision", static_cast<qint64>(copied->revision)},
               {"previous_snapshots_preserved", static_cast<int>(before.size())}});
    }
    if (restore || corrupt || active) {
        const auto backedUp = expected;
        // Different current and backed-up edits prove restore reads the selected
        // snapshot rather than silently copying the live catalog.
        closeMaintenance(c);
        c.reveal("develop.exposureSlider");
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.6, .5});
        c.saved();
        expected = catalog.assetSnapshot(photo);
        require(expected && expected->asset.edits != backedUp->asset.edits &&
                    expected->revision > backedUp->revision,
                "Restore control needs distinct live and snapshot edits");
        saves.clear();
        openMaintenance(c);
        const auto snapshots = savedBackups(c.library.backupFolder());
        auto source = backup;
        if (corrupt) {
            source = (c.root + "/corrupt-backup.sqlite3").toStdString();
            QFile damaged(QString::fromStdString(source.string()));
            const QByteArray bytes("This is not a SQLite database\n");
            require(damaged.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                        damaged.write(bytes) == bytes.size(),
                    "Cannot create owned corrupt-backup fixture");
            damaged.close();
        }
        const auto sourceHash = luma::macos::sha256File(source);
        if (daily)
            require(QDir().mkpath(c.stateRoot + "/restored"),
                    "Cannot create owned restored-catalog folder");
        const auto destination = active  ? c.catalogFile
                                 : daily ? c.stateRoot + "/restored/catalog.sqlite"
                                         : c.root + "/restored-catalog.sqlite3";
        auto *dialog = d.object("catalog.restoreDestination");
        require(!dialog->property("visible").toBool() &&
                    dialog->property("title").toString() == "Restore to a new catalog file" &&
                    d.object("catalogMaintenance")
                        ->setProperty("restoreSource", QUrl::fromLocalFile(QString::fromStdString(
                                                           source.string()))) &&
                    dialog->setProperty("selectedFile", QUrl::fromLocalFile(destination)) &&
                    dialog->property("selectedFile").toUrl().toLocalFile() == destination,
                "Restore chooser fixture rejected its owned source/destination");
        d.log({{"action", "file_dialog_selection_fixture_boundary"},
               {"id", "catalog.restoreDestination"},
               {"path", destination},
               {"native_selection_exercised", false}});
        require(QMetaObject::invokeMethod(dialog, "accepted", Qt::DirectConnection),
                "Restore FileDialog has no production accepted handler");
        d.wait([&] { return !c.library.working() && !c.library.loading(); },
               "Restore worker completed");
        if (restore) {
            require(
                c.library.error().isEmpty() &&
                    c.library.maintenanceStatus().contains(destination) &&
                    d.object("catalog.status")->property("text").toString().contains(destination),
                "Restore did not report its successful new destination");
            luma::Catalog::verifyBackup(destination.toStdString());
            luma::Catalog restored(destination.toStdString());
            const auto copied = restored.assetSnapshot(photo);
            require(
                copied && copied->asset.id == backedUp->asset.id &&
                    copied->asset.path == backedUp->asset.path &&
                    copied->asset.checksum == backedUp->asset.checksum &&
                    copied->asset.edits == backedUp->asset.edits &&
                    copied->revision == backedUp->revision && restored.libraryIDs({}) == members &&
                    restored.activeAsset() && restored.activeAsset()->id == photo,
                "Restored catalog did not reproduce the selected snapshot's edits and identity");
            if (daily) {
                writeJson(c.stateRoot + "/restored/restore-expected.json",
                          {{"prepare_pid", QCoreApplication::applicationPid()},
                           {"backup", QString::fromStdString(source.string())},
                           {"backup_sha256", QString::fromStdString(sourceHash)},
                           {"original_live_catalog", c.catalogFile},
                           {"live_revision", qint64(expected->revision)},
                           {"live_exposure", expected->asset.edits.exposure}});
            }
        } else {
            require(!c.library.error().isEmpty() &&
                        d.object("catalog.error")->property("visible").toBool() &&
                        d.object("catalog.error")->property("text").toString() ==
                            c.library.error() &&
                        !c.library.status().contains("Catalog restored to a new file"),
                    "Rejected restore did not show an error or falsely claimed success");
            if (active)
                require(c.library.error() == "Backup destination already exists.",
                        "Active catalog overwrite was not explicitly refused");
            else
                require(!QFile::exists(destination), "Corrupt backup published a destination");
        }
        require(luma::macos::sha256File(source) == sourceHash &&
                    savedBackups(c.library.backupFolder()) == snapshots &&
                    QDir(c.root).entryList({".luma-backup-*"}, QDir::Files | QDir::Hidden).empty(),
                "Restore changed its source/backups or left temporary files");
    }
    closeMaintenance(c);
    const auto after = catalog.assetSnapshot(photo);
    require(after && after->asset.edits == expected->asset.edits &&
                after->revision == expected->revision && catalog.libraryIDs({}) == members &&
                c.library.selectionIDs() == selected && c.editor.photoID().toStdString() == photo &&
                c.editor.currentSettings() == expected->asset.edits && saves.isEmpty() &&
                c.editor.canUndo() && !c.editor.canRedo() &&
                luma::macos::sha256File(expected->asset.path) == originalHash,
            "Manual backup changed originals, saved/live edits, selection or history");
    require(c.warnings.isEmpty(), "Manual backup produced QML warnings: " + c.warnings.join('\n'));
    return true;
}
bool backupDailyRestartScenario(Context &c, const QString &id, const QString &stage) {
    if (id != "UX-P10-DAY-010" && id != "UX-P10-DAT-005")
        return false;
    auto &d = *c.driver;
    if (stage == "prepare") {
        require(catalogMaintenanceScenario(c, id), "Daily backup preparation unavailable");
        d.quitShortcut();
        d.wait([&] { return !c.window->isVisible() && !c.editor.saving(); },
               "Original catalog closed before opening its restored copy");
        c.checkData();
        return true;
    }
    require(stage == "verify", "Invalid restored daily-work stage");
    const auto expected = crashExpectation(c, "restore-expected.json");
    const auto backupPath = expected["backup"].toString().toStdString();
    require(luma::macos::sha256File(backupPath) ==
                expected["backup_sha256"].toString().toStdString(),
            "Opening restored catalog changed its backup source");
    luma::Catalog::verifyBackup(c.catalogFile.toStdString());
    luma::Catalog backup(backupPath, luma::CatalogOpenMode::existingOnly);
    luma::Catalog restored(c.catalogFile.toStdString(), luma::CatalogOpenMode::existingOnly);
    const auto original = backup.activeAsset();
    require(original.has_value(), "Backup has no active photo");
    const auto before = backup.assetSnapshot(original->id).value();
    const auto after = restored.assetSnapshot(original->id);
    require(after && restored.libraryIDs({}) == backup.libraryIDs({}) && restored.activeAsset() &&
                restored.activeAsset()->id == original->id && after->revision == before.revision &&
                after->asset.edits == before.asset.edits &&
                after->asset.path == before.asset.path &&
                after->asset.checksum == before.asset.checksum,
            "Restored startup changed membership, active identity, revision or full document");
    c.editor.restore();
    c.loaded();
    c.saved();
    require(c.editor.photoID().toStdString() == original->id &&
                c.editor.currentSettings() == before.asset.edits && !c.editor.canUndo() &&
                !c.editor.canRedo(),
            "Fresh process did not load the exact backup document with empty session history");
    c.reveal("develop.contrastSlider");
    d.drag("develop.contrastSlider", d.sliderThumb("develop.contrastSlider"), {.7, .5});
    c.saved();
    const auto edited = c.editor.currentSettings();
    require(edited != before.asset.edits && edited.contrast > 1 && c.editor.canUndo(),
            "Restored project rejected a new real edit");
    d.click("main.undoButton");
    c.saved();
    require(c.editor.currentSettings() == before.asset.edits,
            "Restored-project Undo lost original backup geometry/adjustments");
    d.click("main.redoButton");
    c.saved();
    const auto committed = restored.assetSnapshot(original->id);
    require(committed && committed->revision > before.revision &&
                committed->asset.edits == edited && c.editor.currentSettings() == edited,
            "Restored-project Redo did not commit the exact new document");
    const auto decoded = luma::PhotoDecoder().decode(before.asset.path);
    // Export targets sRGB, not the Cocoa display profile used by preview oracles.
    const auto reference =
        luma::renderCPU(decoded.image, edited, decoded.metadata.orientation, {}, {},
                        decoded.calibration, luma::DetailDiagnostic::none, 1, decoded.metadata);
    const QImage output(c.exportPNG("Restored-project"));
    const auto pixels = output.convertToFormat(QImage::Format_RGBA8888);
    require(!pixels.isNull() && pixels.size() == QSize(reference.width, reference.height) &&
                output.colorSpace() == QColorSpace(QColorSpace::SRgb),
            "Restored-project export has wrong dimensions or color space");
    for (int y = 0; y < pixels.height(); ++y)
        require(std::equal(pixels.constScanLine(y), pixels.constScanLine(y) + pixels.width() * 4,
                           reference.rgba.data() + y * reference.width * 4),
                "Restored-project export differs from the independent CPU document");
    luma::Catalog live(expected["original_live_catalog"].toString().toStdString(),
                       luma::CatalogOpenMode::existingOnly);
    const auto unaffected = live.assetSnapshot(original->id);
    require(unaffected && unaffected->revision == expected["live_revision"].toInteger() &&
                unaffected->asset.edits.exposure == expected["live_exposure"].toDouble() &&
                unaffected->asset.edits != edited &&
                luma::macos::sha256File(before.asset.path) == before.asset.checksum &&
                luma::macos::sha256File(backupPath) ==
                    expected["backup_sha256"].toString().toStdString(),
            "Continued restored-project work changed its original catalog, backup or original");
    if (id == "UX-P10-DAT-005") {
        QJsonArray assets;
        int versions = 0;
        for (const auto &assetID : restored.libraryIDs({})) {
            restored.assetSnapshot(assetID).value().asset.edits.validate();
            for (const auto &version : restored.editVersions(assetID)) {
                restored.editVersion(assetID, version.id, version.revision).validate();
                ++versions;
            }
            assets.append(QString::fromStdString(assetID));
        }
        writeJson(c.root + "/serialized-audit.json", {{"status", "PASS"},
                                                      {"pid", QCoreApplication::applicationPid()},
                                                      {"asset_ids", assets},
                                                      {"versions_validated", versions}});
        d.quitShortcut();
        d.wait([&] { return !c.window->isVisible() && c.editor.readyForUpdate(); },
               "Restored data audit clean shutdown and worker drain");
    }
    d.log({{"action", "backup_restore_continue_work"},
           {"catalog", c.catalogFile},
           {"prepare_pid", expected["prepare_pid"]},
           {"verify_pid", QCoreApplication::applicationPid()},
           {"exact_document", true},
           {"export_cpu_pixels_exact", true},
           {"native_picker_navigation", false}});
    c.checkData();
    return true;
}
bool backupCrashScenario(Context &c, const QString &id, const QString &stage) {
    if (id != "UX-P09-FLT-003")
        return false;
    auto &d = *c.driver;
    const auto expectedPath = (c.stateRoot + "/expected-backup.sqlite").toStdString();
    luma::Catalog catalog(c.catalogFile.toStdString());
    if (stage == "prepare") {
        c.import(1);
        d.doubleClick(c.card(0));
        c.loaded();
        c.saved();
        c.reveal("develop.exposureSlider");
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.6, .5});
        c.saved();
        catalog.backupTo(expectedPath);
        const auto before = savedBackups(c.library.backupFolder());
        require(!before.empty(), "Backup crash requires a prior valid snapshot");
        QJsonObject hashes;
        for (const auto &[path, hash] : before) {
            luma::Catalog::verifyBackup(path);
            hashes[QString::fromStdString(path.string())] = QString::fromStdString(hash);
        }
        openMaintenance(c);
        PersistenceGate gate(c.catalogFile.toStdString(), {"backup.after-step"});
        d.click("catalog.backupNow");
        d.wait([&] { return gate.snapshot().held; }, "Actual SQLite backup step reached");
        const auto observed = gate.snapshot();
        require(!observed.hookFailed && observed.events.size() == 1 && c.library.working(),
                "Backup kill did not retain the actual worker");
        const auto &event = observed.events.front();
        require(std::filesystem::is_regular_file(event.temporary) &&
                    !std::filesystem::exists(event.target) &&
                    savedBackups(c.library.backupFolder()) == before,
                "Incomplete backup was published or changed an earlier snapshot");
        writeJson(c.stateRoot + "/backup-crash-expected.json",
                  {{"prepare_pid", QCoreApplication::applicationPid()},
                   {"previous", hashes},
                   {"unpublished", QString::fromStdString(event.target.string())}});
        killCheckpoint(c, id, stage, "backup.after-step");
    }
    require(stage == "verify", "Invalid backup crash stage");
    const auto saved = crashExpectation(c, "backup-crash-expected.json");
    d.wait([&] { return !c.library.working() && !c.library.loading(); },
           "Backup recovery startup settled");
    require(!QFile::exists(saved["unpublished"].toString()),
            "Crash published an unfinished backup");
    const auto hashes = saved["previous"].toObject();
    for (auto it = hashes.begin(); it != hashes.end(); ++it) {
        const auto path = it.key().toStdString();
        require(luma::macos::sha256File(path) == it.value().toString().toStdString(),
                "Crash changed prior backup bytes");
        luma::Catalog::verifyBackup(path);
    }
    luma::Catalog expected(expectedPath);
    require(catalog.libraryIDs({}) == expected.libraryIDs({}),
            "Backup crash changed catalog membership");
    for (const auto &photo : expected.libraryIDs({})) {
        const auto before = expected.assetSnapshot(photo).value(),
                   now = catalog.assetSnapshot(photo).value();
        require(now.asset.edits == before.asset.edits && now.revision == before.revision &&
                    now.asset.path == before.asset.path &&
                    now.asset.checksum == before.asset.checksum &&
                    luma::macos::sha256File(now.asset.path) == before.asset.checksum,
                "Backup crash changed originals or committed documents");
    }
    openMaintenance(c);
    const auto before = savedBackups(c.library.backupFolder());
    d.click("catalog.backupNow");
    d.wait([&] { return !c.library.working() && !c.library.loading(); },
           "Post-crash backup completed");
    require(c.library.error().isEmpty(), c.library.error());
    auto added = savedBackups(c.library.backupFolder());
    for (const auto &[path, hash] : before) {
        require(added.contains(path) && added.at(path) == hash,
                "Backup retry changed a prior snapshot");
        added.erase(path);
    }
    require(added.size() == 1, "Backup retry did not publish exactly one snapshot");
    luma::Catalog::verifyBackup(added.begin()->first);
    luma::Catalog retried(added.begin()->first);
    require(retried.libraryIDs({}) == expected.libraryIDs({}),
            "Retry backup lost catalog membership");
    for (const auto &photo : expected.libraryIDs({})) {
        const auto beforePhoto = expected.assetSnapshot(photo).value(),
                   after = retried.assetSnapshot(photo).value();
        require(after.asset.edits == beforePhoto.asset.edits &&
                    after.revision == beforePhoto.revision,
                "Retry snapshot lost complete saved edits");
    }
    require(QDir(c.library.backupFolder().toLocalFile())
                .entryList({".luma-backup-*"}, QDir::Files | QDir::Hidden)
                .empty(),
            "Backup retry left an owned crash staging file");
    d.log({{"action", "backup_kill_recovered"},
           {"previous_snapshots", hashes.size()},
           {"new_snapshots", 1}});
    closeMaintenance(c);
    return true;
}

} // namespace ux
