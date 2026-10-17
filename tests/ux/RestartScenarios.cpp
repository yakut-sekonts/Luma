#include "tests/ux/UxContext.h"
#include "tests/ux/UxTestIds.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>

namespace ux {
bool localeRestartScenario(Context &, const QString &, const QString &);
bool copyRestartScenario(Context &, const QString &, const QString &);
bool presetRemainingRestartScenario(Context &, const QString &, const QString &);
bool thumbnailRestartScenario(Context &, const QString &, const QString &);
bool whiteBalanceRestartScenario(Context &, const QString &, const QString &);
bool appMissingSourceRestartScenario(Context &, const QString &, const QString &);
namespace {
void quit(Context &c) {
    c.driver->quitShortcut();
    c.driver->wait([&] { return !c.window->isVisible() && !c.editor.saving() && !c.editor.busy(); },
                   "clean shutdown before process exit");
    c.checkData();
}
} // namespace
void restartScenario(Context &c, const QString &id, const QString &stage) {
    if (whiteBalanceRestartScenario(c, id, stage) ||
        appMissingSourceRestartScenario(c, id, stage) ||
        presetRemainingRestartScenario(c, id, stage) || thumbnailRestartScenario(c, id, stage) ||
        localeRestartScenario(c, id, stage) || copyRestartScenario(c, id, stage))
        return;
    auto &d = *c.driver;
    const bool mask = id == "UX-MSK-008" || id == "UX-MSK-040";
    const bool killed = id == "UX-UND-014" || id == "UX-UND-015" || id == "UX-REC-007";
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
            if (id == "UX-UND-014") {
                d.drag(ids::exposure, {.5, .5}, {.65, .5});
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
            writeJson(c.stateRoot + "/expected.json", {{"source", c.editor.sourcePath()}});
            struct Unblock {
                SqliteFault &fault;
                ~Unblock() {
                    fault.holdWrites(false);
                }
            };
            [[maybe_unused]] const Unblock unblock{c.storageFault};
            if (id == "UX-REC-007")
                c.storageFault.holdWrites(true);
            if (id != "UX-UND-014") {
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
} // namespace ux
