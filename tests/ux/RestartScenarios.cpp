#include "platform/macos/FileHasher.h"
#include "tests/ux/CrashScenarios.h"
#include "tests/ux/PersistenceProbe.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxTestIds.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>

namespace ux {
bool xmpAtomicScenario(Context &, const QString &, const QString &);
bool catalogCommitCrashScenario(Context &, const QString &, const QString &);
bool metadataBatchCrashScenario(Context &, const QString &, const QString &);
bool backupCrashScenario(Context &, const QString &, const QString &);
bool backupDailyRestartScenario(Context &, const QString &, const QString &);
bool relinkCrashScenario(Context &, const QString &, const QString &);
bool mergeCrashScenario(Context &, const QString &, const QString &);
bool localeRestartScenario(Context &, const QString &, const QString &);
bool exportSettingsRestartScenario(Context &, const QString &, const QString &);
bool copyRestartScenario(Context &, const QString &, const QString &);
bool presetRemainingRestartScenario(Context &, const QString &, const QString &);
bool thumbnailRestartScenario(Context &, const QString &, const QString &);
bool whiteBalanceRestartScenario(Context &, const QString &, const QString &);
bool appMissingSourceRestartScenario(Context &, const QString &, const QString &);
bool catalogScaleRestartScenario(Context &, const QString &, const QString &);
bool mergeRestartScenario(Context &, const QString &, const QString &);
namespace {
void quit(Context &c) {
    c.driver->quitShortcut();
    c.driver->wait([&] { return !c.window->isVisible() && !c.editor.saving() && !c.editor.busy(); },
                   "clean shutdown before process exit");
    c.checkData();
}
} // namespace
void restartScenario(Context &c, const QString &id, const QString &stage) {
    if (catalogCommitCrashScenario(c, id, stage) || metadataBatchCrashScenario(c, id, stage) ||
        backupCrashScenario(c, id, stage) || backupDailyRestartScenario(c, id, stage) ||
        relinkCrashScenario(c, id, stage) || mergeCrashScenario(c, id, stage) ||
        xmpAtomicScenario(c, id, stage) || catalogScaleRestartScenario(c, id, stage) ||
        mergeRestartScenario(c, id, stage))
        return;
    if (exportSettingsRestartScenario(c, id, stage) || whiteBalanceRestartScenario(c, id, stage) ||
        appMissingSourceRestartScenario(c, id, stage) ||
        presetRemainingRestartScenario(c, id, stage) || thumbnailRestartScenario(c, id, stage) ||
        localeRestartScenario(c, id, stage) || copyRestartScenario(c, id, stage))
        return;
    auto &d = *c.driver;
    const bool mask = id == "UX-MSK-008" || id == "UX-MSK-040";
    const bool dailyKill = id == "UX-P10-DAY-007";
    const bool committedKill = id == "UX-UND-014" || dailyKill;
    const bool killed = committedKill || id == "UX-UND-015" || id == "UX-REC-007";
    const bool detail = id == "UX-DTL-012" || id == "UX-SLD-020";
    const bool crop = id == "UX-CRP-018";
    const bool transform = id == "UX-TRN-015";
    const bool lens = id == "UX-LNS-010";
    if (id == "UX-APP-001" || id == "UX-APP-005") {
        require(stage == "cycle", "Invalid launch-cycle stage");
        c.editor.restore();
        d.wait([&] { return !c.editor.busy(); }, "empty session restored");
        require(!c.editor.hasPhoto() && c.editor.error().isEmpty(),
                "Repeated launch failed to restore empty catalog");
        quit(c);
        return;
    }
    require(mask || killed || detail || crop || lens || transform || id == "UX-APP-003" ||
                id == "UX-APP-007" || id == "UX-UND-013",
            "Unknown restart test");
    if (stage == "prepare") {
        if (lens)
            openKnownLens(c);
        else
            c.openFirst();
        if (killed) {
            c.saved();
            c.reveal(ids::exposure);
            if (committedKill) {
                d.drag(ids::exposure, {.5, .5}, {.65, .5});
                if (dailyKill) {
                    c.addMask(2);
                    d.drag("localMaskGestureArea", {.2, .3}, {.8, .7});
                    d.wait([&] { return c.editor.localCanvasReady(); },
                           "Daily-use brush gesture rendered");
                    c.reveal("local_exposure");
                    d.drag("local_exposure", d.sliderThumb("local_exposure"), {.65, .5});
                }
                c.saved();
            }
            const auto document = c.editor.currentSettings();
            {
                luma::Catalog expected((c.stateRoot + "/expected.sqlite").toStdString());
                const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
                if (!active)
                    throw std::runtime_error("Kill fixture has no active asset");
                const auto asset = expected.addAndSelect(c.editor.sourcePath().toStdString(),
                                                         active->checksum, active->metadata);
                expected.saveEdits(asset.id, document);
                const auto stored = expected.activeAsset();
                if (!stored)
                    throw std::runtime_error("Expected document was not written");
                require(stored->edits == document,
                        "Expected document serializer lost in-memory fields");
            }
            const auto snapshot = luma::Catalog(c.catalogFile.toStdString())
                                      .assetSnapshot(c.editor.photoID().toStdString());
            require(snapshot.has_value(), "Kill fixture lost its committed asset snapshot");
            writeJson(c.stateRoot + "/expected.json",
                      {{"source", c.editor.sourcePath()},
                       {"id", c.editor.photoID()},
                       {"revision", qint64(snapshot->revision)},
                       {"prepare_pid", QCoreApplication::applicationPid()}});
            if (dailyKill) {
                require(snapshot->asset.edits == document && document.exposure > 1 &&
                            document.localMasks.size() == 1 &&
                            !document.localMasks.front().strokes.empty() &&
                            document.localMasks.front().adjustments[0] > 0,
                        "Daily-use kill needs a complete committed global/local document");
                killCheckpoint(c, id, stage, "daily.committed-global-and-local-edits");
            }
            struct Unblock {
                SqliteFault &fault;
                ~Unblock() {
                    fault.holdWrites(false);
                }
            };
            [[maybe_unused]] const Unblock unblock{c.storageFault};
            if (id == "UX-REC-007")
                c.storageFault.holdWrites(true);
            if (!committedKill) {
                d.drag(ids::exposure, {.5, .5}, {.7, .5});
                require(c.editor.saving(), "Kill fixture missed pending save");
                if (id == "UX-REC-007")
                    d.wait([&] { return c.storageFault.writeBlocked(); },
                           "SQLite WAL write entered");
            }
            writeJson(c.root + "/metrics.json", c.metrics.json());
            writeJson(c.root + "/kill-ready.json",
                      {{"id", id},
                       {"stage", stage},
                       {"pid", QCoreApplication::applicationPid()},
                       {"utc_ms", QDateTime::currentMSecsSinceEpoch()}});
            // Keep the main event loop live until the parent delivers the intentional SIGKILL.
            d.wait([] { return false; }, "parent SIGKILL was not delivered", 10000);
            throw std::runtime_error("Kill stage unexpectedly returned");
        }
        if (mask) {
            c.addMask(2);
            d.drag("localMaskGestureArea", {.2, .3}, {.8, .7});
            d.wait([&] { return c.editor.localCanvasReady(); }, "brush preview ready");
            c.reveal("local_exposure");
            d.drag("local_exposure", {.5, .5}, {.65, .5});
        } else if (lens) {
            selectKnownLensProfile(c);
            c.reveal("develop.lens.distortionSlider");
            d.drag("develop.lens.distortionSlider", d.sliderThumb("develop.lens.distortionSlider"),
                   {.35, .5});
        } else if (transform) {
            editTransformControls(c);
        } else if (detail) {
            editDetailControls(c, id == "UX-SLD-020");
        } else if (crop) {
            c.reveal("cropTool");
            d.click("cropTool");
            d.wait([&] { return c.editor.cropCanvasReady(); }, "Crop restart canvas ready");
            d.drag("cropDragArea", {.001, .001}, {.15, .15});
            c.reveal("cropAngleSlider");
            d.drag("cropAngleSlider", d.sliderThumb("cropAngleSlider"), {.6, .5});
            c.reveal("cropFlipHorizontal");
            d.click("cropFlipHorizontal");
            c.reveal("cropRotateRight");
            d.click("cropRotateRight");
            c.reveal("cropTool");
            d.click("cropTool");
            require(c.editor.currentSettings().crop != luma::CropSettings{},
                    "Crop restart fixture has no geometry");
        } else {
            c.reveal(ids::exposure);
            d.drag(ids::exposure, {.5, .5}, {.65, .5});
        }
        const auto document = c.editor.currentSettings();
        require(document != luma::EditDocument{}, "Restart fixture has no edit");
        {
            // Independent expected full document; includes every mask component/point.
            luma::Catalog expected((c.stateRoot + "/expected.sqlite").toStdString());
            const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
            if (!active)
                throw std::runtime_error("Restart fixture has no active asset");
            const auto asset = expected.addAndSelect(c.editor.sourcePath().toStdString(),
                                                     active->checksum, active->metadata);
            expected.saveEdits(asset.id, document);
            const auto stored = expected.activeAsset();
            if (!stored)
                throw std::runtime_error("Expected document was not written");
            require(stored->edits == document,
                    "Expected document serializer lost in-memory fields");
        }
        QString preview;
        if (id == "UX-MSK-008")
            preview = c.exportPNG("before-restart");
        if (id == "UX-MSK-040" || id == "UX-APP-007" || id == "UX-SLD-020")
            require(c.editor.saving(), "Immediate quit missed pending autosave window");
        writeJson(c.stateRoot + "/expected.json",
                  {{"source", c.editor.sourcePath()}, {"preview", preview}});
        quit(c);
        return;
    }
    require(stage == "verify", "Invalid restart verification stage");
    QFile file(c.stateRoot + "/expected.json");
    require(file.open(QIODevice::ReadOnly), "Missing persisted restart oracle");
    const auto expected = QJsonDocument::fromJson(file.readAll()).object();
    const auto document =
        luma::Catalog((c.stateRoot + "/expected.sqlite").toStdString()).activeAsset();
    if (!document)
        throw std::runtime_error("Missing complete expected document");
    c.editor.restore();
    c.loaded();
    require(c.editor.sourcePath() == expected["source"].toString() &&
                c.editor.currentSettings() == document->edits,
            "Process restart lost/crossed the saved document or mask geometry");
    require(!c.editor.canUndo() && !c.editor.canRedo(),
            "Session-local history crossed a process restart");
    if (dailyKill) {
        require(expected["prepare_pid"].toInteger() != QCoreApplication::applicationPid(),
                "Daily-use recovery did not start a new process");
        const auto snapshot = luma::Catalog(c.catalogFile.toStdString())
                                  .assetSnapshot(c.editor.photoID().toStdString());
        require(snapshot && c.editor.photoID() == expected["id"].toString() &&
                    snapshot->revision == expected["revision"].toInteger() &&
                    snapshot->asset.edits == document->edits &&
                    luma::macos::sha256File(snapshot->asset.path) == snapshot->asset.checksum,
                "Daily-use restart changed identity, revision, committed edits or original");
        c.reveal(ids::exposure);
        d.drag(ids::exposure, d.sliderThumb(ids::exposure), {.3, .5});
        c.saved();
        require(c.editor.currentSettings().exposure < -1 && c.editor.canUndo(),
                "Recovered daily-use session cannot accept another edit");
        d.click(ids::undo);
        c.saved();
        require(c.editor.currentSettings() == document->edits,
                "New-session Undo did not return to the complete recovered document");
    }
    if (mask) {
        require(!c.editor.currentSettings().localMasks.empty() && !c.editor.localMode() &&
                    !c.editor.localGestureActive(),
                "Restart lost masks or restored a transient tool gesture");
        if (id == "UX-MSK-008") {
            const auto exported = c.exportPNG("after-restart");
            require(QImage(exported) == QImage(expected["preview"].toString()),
                    "Mask effect changed after process restart");
        }
    }
    quit(c);
}
bool catalogCommitCrashScenario(Context &c, const QString &id, const QString &stage) {
    if (id != "UX-P09-FLT-001")
        return false;
    auto &d = *c.driver;
    const bool committed = stage.endsWith("-committed");
    const auto expectedPath = (c.stateRoot + "/expected-commit.sqlite").toStdString();
    luma::Catalog catalog(c.catalogFile.toStdString());
    if (stage.startsWith("prepare")) {
        c.import(1);
        d.doubleClick(c.card(0));
        c.loaded();
        c.saved();
        c.reveal("develop.exposureSlider");
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.6, .5});
        c.saved();
        catalog.backupTo(expectedPath);
        const auto photo = c.editor.photoID().toStdString();
        const auto old = catalog.assetSnapshot(photo).value();
        const std::string point = committed ? "catalog.after-commit" : "catalog.before-commit";
        PersistenceGate gate(c.catalogFile.toStdString(), {point});
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.75, .5});
        const auto desired = c.editor.currentSettings();
        require(desired != old.asset.edits,
                "Commit crash fixture needs distinct complete documents");
        {
            luma::Catalog expected(expectedPath);
            expected.saveEdits(photo, desired);
        }
        d.wait([&] { return gate.snapshot().held; }, "Actual catalog commit checkpoint reached");
        const auto visible = catalog.assetSnapshot(photo).value();
        require(!gate.snapshot().hookFailed &&
                    visible.asset.edits == (committed ? desired : old.asset.edits) &&
                    visible.revision == old.revision + (committed ? 1 : 0),
                "Independent connection saw incorrect catalog transaction state");
        // Keep both complete documents, without interpreting individual edit fields.
        luma::Catalog oldState((c.stateRoot + "/old-commit.sqlite").toStdString());
        auto oldAsset =
            oldState.addAndSelect(old.asset.path, old.asset.checksum, old.asset.metadata);
        oldState.saveEdits(oldAsset.id, old.asset.edits);
        writeJson(c.stateRoot + "/commit-expected.json",
                  {{"prepare_pid", QCoreApplication::applicationPid()},
                   {"photo", QString::fromStdString(photo)},
                   {"old_revision", static_cast<qint64>(old.revision)}});
        killCheckpoint(c, id, stage, QString::fromStdString(point));
    }
    require(stage == "verify" || stage == "verify-committed", "Invalid commit recovery stage");
    const auto saved = crashExpectation(c, "commit-expected.json");
    luma::Catalog expected(expectedPath);
    const auto photo = saved["photo"].toString().toStdString();
    const auto newState = expected.assetSnapshot(photo).value();
    const auto oldState =
        luma::Catalog((c.stateRoot + "/old-commit.sqlite").toStdString()).activeAsset().value();
    const auto wanted = committed ? newState.asset.edits : oldState.edits;
    c.editor.restore();
    c.loaded();
    c.saved();
    const auto current = catalog.assetSnapshot(photo).value();
    require(
        catalog.libraryIDs({}) == expected.libraryIDs({}) && current.asset.edits == wanted &&
            current.revision == saved["old_revision"].toInteger() + (committed ? 1 : 0) &&
            current.asset.path == newState.asset.path &&
            current.asset.checksum == newState.asset.checksum &&
            luma::macos::sha256File(current.asset.path) == current.asset.checksum &&
            c.editor.photoID().toStdString() == photo && c.editor.currentSettings() == wanted &&
            !c.editor.canUndo() && !c.editor.canRedo(),
        "Killed catalog transaction lost atomicity, identity, original or complete edit document");
    c.reveal("develop.contrastSlider");
    d.drag("develop.contrastSlider", d.sliderThumb("develop.contrastSlider"), {.7, .5});
    c.saved();
    d.click("main.undoButton");
    c.saved();
    require(c.editor.currentSettings() == wanted && c.editor.canRedo(),
            "Recovered catalog could not save a new edit and exact Undo");
    d.log({{"action", "catalog_commit_kill_recovered"}, {"committed", committed}});
    return true;
}

} // namespace ux
