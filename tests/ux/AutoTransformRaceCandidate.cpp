#include "apps/shared/PresetStore.h"
#include "tests/ux/AutoTransformProbe.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QUrl>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef LUMA_UX_AUTO_TRANSFORM_PROBE
#error "RACE005 candidate requires the luma_ux_tests-only actual result probe"
#endif

// Bounded successful-result regression for Cancel and photo-switch ordering.
namespace ux {
namespace {
using Document = luma::EditDocument;
constexpr auto identifier = "CANDIDATE-RACE005-AUTO-COMPLETION";
QString hash(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read bounded Auto source: " + path);
    QCryptographicHash value(QCryptographicHash::Sha256);
    require(value.addData(&file), "Cannot hash complete Auto source: " + path);
    return QString::fromLatin1(value.result().toHex());
}
QJsonObject documentJSON(const Document &value) {
    const auto encoded = QJsonDocument::fromJson(
        luma::encodePreset({"RACE005 full document", {}, value, luma::allSettingsGroups}));
    require(encoded.isObject(), "Cannot encode complete Auto document evidence");
    auto result = encoded.object();
    result.insert("local_masks_serialized",
                  QString::fromStdString(luma::encodeLocalMasks(value.localMasks)));
    return result;
}
QJsonObject operationJSON(const AutoTransformOperation &operation) {
    return {
        {"controller", QString::number(reinterpret_cast<std::uintptr_t>(operation.controller), 16)},
        {"job", QString::number(reinterpret_cast<std::uintptr_t>(operation.job.get()), 16)},
        {"auto_revision", QString::number(operation.revision)},
        {"edit_revision", QString::number(operation.editRevision)},
        {"load_revision", QString::number(operation.loadRevision)},
        {"uuid", QString::fromStdString(operation.assetID)},
        {"source", QString::fromStdString(operation.sourcePath)}};
}
QJsonObject probeJSON(const AutoTransformProbeSnapshot &snapshot) {
    QJsonObject value{{"successful_arrivals", static_cast<int>(snapshot.successfulArrivals)},
                      {"deliveries", static_cast<int>(snapshot.deliveries)},
                      {"rejections", static_cast<int>(snapshot.rejections)},
                      {"mismatched_deliveries", static_cast<int>(snapshot.mismatchedDeliveries)},
                      {"released", snapshot.released},
                      {"left_hold", snapshot.leftHold},
                      {"timed_out", snapshot.timedOut},
                      {"hook_failed", snapshot.hookFailed},
                      {"delivery_before_release", snapshot.deliveryBeforeRelease}};
    if (snapshot.held) {
        value["held_operation"] = operationJSON(snapshot.held->operation);
        Document solved;
        solved.transform = snapshot.held->solved;
        solved.setEnabled(luma::Correction::transform, true);
        value["actual_solved_transform_in_neutral_document"] = documentJSON(solved);
    }
    if (snapshot.lastDelivered)
        value["delivered_operation"] = operationJSON(*snapshot.lastDelivered);
    if (snapshot.rejection) {
        const auto &rejection = *snapshot.rejection;
        value["rejected_operation"] = operationJSON(rejection.operation);
        value["controller_present_at_rejection"] = rejection.controllerPresent;
        value["current_auto_revision_at_rejection"] = QString::number(rejection.currentRevision);
        value["current_edit_revision_at_rejection"] =
            QString::number(rejection.currentEditRevision);
        value["current_load_revision_at_rejection"] =
            QString::number(rejection.currentLoadRevision);
    }
    return value;
}
luma::AssetSnapshot snapshot(Context &c, const QString &id) {
    const auto saved = luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(id.toStdString());
    require(saved.has_value(), "Pinned RACE005 UUID disappeared from its own catalog");
    return *saved;
}
void full(Context &c, const QString &id, const QString &path, const Document &expected) {
    require(c.editor.photoID() == id && c.editor.sourcePath() == path &&
                c.editor.currentSettings() == expected && c.editor.error().isEmpty(),
            "Late Auto changed current UUID/source/full document or introduced an error");
}
void rendered(Context &c, const QString &id, const QString &path, const Document &expected,
              const QImage &pixels) {
    const auto ready = [&] {
        const auto *image = c.driver->item("develop.preview");
        return !c.editor.busy() && c.editor.hasPhoto() && image->isVisible() &&
               image->property("status").toInt() == 1 && !c.editor.previewUrl().isEmpty() &&
               image->property("source").toUrl().toString() == c.editor.previewUrl() &&
               c.provider->requestImage({}, nullptr, {}) == pixels;
    };
    c.driver->wait(ready, "RACE005 actual Image Ready/current source/exact expected pixels", 10000);
    c.driver->frame();
    require(ready(), "RACE005 accepted preview did not remain current through polish");
    full(c, id, path, expected);
}
void naturalSave(Context &c, const QString &id, const Document &expected) {
    // No Context::saved()/endEdit/flushSave/retrySave: ordinary input must finish itself.
    c.driver->wait(
        [&] {
            const auto saved =
                luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(id.toStdString());
            return !c.editor.saving() && !c.editor.busy() && saved &&
                   saved->asset.edits == expected;
        },
        "RACE005 natural full-document autosave", 10000);
    require(c.editor.error().isEmpty(), c.editor.error());
}
void idle(Context &c, bool undo, bool redo) {
    require(!c.editor.transformAutoBusy() && !c.editor.busy() && !c.editor.saving() &&
                !c.editor.guidedMode() && !c.editor.cropMode() && !c.editor.localMode() &&
                !c.editor.localGestureActive() && !c.editor.localSampling() &&
                !c.editor.editingLocked() && !c.editor.presetPreview() && !c.editor.original() &&
                c.editor.error().isEmpty() && c.editor.canUndo() == undo &&
                c.editor.canRedo() == redo,
            "Late Auto leaked busy/error/transient state or changed history availability");
}
QString card(Context &c, const QString &id, bool filmstrip) {
    c.driver->wait([&] { return !c.library.loading() && c.library.count() == 2; },
                   "RACE005 exact two-row Library model");
    int row = -1;
    for (int index = 0; index < c.library.count(); ++index) {
        c.driver->wait(
            [&] {
                return !c.library.data(c.library.index(index), LibraryModel::AssetID)
                            .toString()
                            .isEmpty();
            },
            "RACE005 populated metadata row");
        if (c.library.data(c.library.index(index), LibraryModel::AssetID).toString() == id)
            row = index;
    }
    require(row >= 0, "RACE005 actual Library model has no pinned UUID");
    const auto result = c.card(row, filmstrip);
    require(c.driver->object(result)->property("assetID").toString() == id,
            "RACE005 delegate identity disagrees with catalog UUID");
    return result;
}
void switchPhoto(Context &c, const QString &id, const QString &path, const Document &expected,
                 const QImage &pixels) {
    QSignalSpy opens(&c.library, &LibraryModel::openRequested);
    QSignalSpy loads(&c.editor, &EditorController::photoLoaded);
    require(opens.isValid() && loads.isValid(), "Cannot observe real photo-switch boundaries");
    c.driver->click(card(c, id, true));
    c.driver->wait([&] { return loads.count() == 1 && !c.editor.busy(); },
                   "RACE005 actual filmstrip switch completed", 10000);
    require(opens.count() == 1 && opens.at(0).at(0).toUrl().toLocalFile() == path &&
                loads.at(0).at(0).toString() == id,
            "Filmstrip input did not open/load the exact requested asset once");
    rendered(c, id, path, expected, pixels);
    idle(c, false, false); // Load creates a fresh history at persisted edits.
}
// Signal observations are passive and bounded. Disconnect first during destruction.
class DocumentAudit final {
  public:
    DocumentAudit(Context &context, QString a, Document aDocument, QString b, Document bDocument)
        : c(context), aID(std::move(a)), bID(std::move(b)), aExpected(std::move(aDocument)),
          bExpected(std::move(bDocument)) {
        connection = QObject::connect(
            &c.editor, &EditorController::stateChanged, &lifetime, [this]() noexcept {
                try {
                    ++observations;
                    const auto id = c.editor.photoID();
                    const bool valid = (id == aID && c.editor.currentSettings() == aExpected) ||
                                       (id == bID && c.editor.currentSettings() == bExpected);
                    if (!valid || !c.editor.error().isEmpty()) {
                        ++invalid;
                        if (failures.size() < 8)
                            failures.append(
                                QJsonObject{{"uuid", id},
                                            {"document", documentJSON(c.editor.currentSettings())},
                                            {"error", c.editor.error()}});
                    }
                } catch (...) {
                    observationFailed = true;
                }
            });
    }
    ~DocumentAudit() {
        QObject::disconnect(connection);
    }
    QJsonObject json() const {
        return {{"observations", observations},
                {"invalid", invalid},
                {"observation_failed", observationFailed},
                {"first_failures", failures}};
    }
    void assertClean() const {
        require(observations > 0 && invalid == 0 && !observationFailed,
                "Old Auto transiently changed an observed full document or error state");
    }

  private:
    Context &c;
    QString aID, bID;
    Document aExpected, bExpected;
    int observations = 0, invalid = 0;
    bool observationFailed = false;
    QJsonArray failures;
    QMetaObject::Connection connection;
    QObject lifetime;
};
void seed(Context &c, const QString &id, const QString &path, const QString &selector,
          const Document &before, const Document &expected) {
    full(c, id, path, before);
    idle(c, false, false);
    c.reveal(selector);
    c.driver->drag(selector, c.driver->sliderThumb(selector), {1., .5}, 4);
    require(!c.driver->object(selector)->property("pressed").toBool(),
            "Seed drag left its real Slider pressed before any Auto/endEdit boundary");
    full(c, id, path, expected);
    require(c.editor.currentSettings() != before && c.editor.canUndo() && !c.editor.canRedo(),
            "Real seed drag did not create its exact single history entry");
    naturalSave(c, id, expected);
    rendered(c, id, path, expected, fixtureReference(c, expected, path));
    idle(c, true, false);
}
AutoTransformSuccess acquire(Context &c, AutoTransformResultGate &gate, const QString &id,
                             const QString &path, const Document &before,
                             const QImage &beforePixels) {
    full(c, id, path, before);
    idle(c, true, false);
    c.reveal("transformAuto");
    QSignalSpy starts(c.driver->object("transformAuto"), SIGNAL(clicked()));
    require(starts.isValid(), "No observable actual Auto button");
    c.driver->click("transformAuto");
    c.driver->wait(
        [&] {
            const auto observed = gate.snapshot();
            return observed.held.has_value() || observed.hookFailed ||
                   !c.editor.transformAutoBusy();
        },
        "actual calibrated successful OLD Auto result reached worker hold", 15000);
    const auto observed = gate.snapshot();
    require(starts.count() == 1 && observed.held && observed.successfulArrivals == 1 &&
                observed.deliveries == 0 && observed.rejections == 0 && !observed.released &&
                !observed.leftHold && !observed.timedOut && !observed.hookFailed &&
                !observed.deliveryBeforeRelease && observed.mismatchedDeliveries == 0 &&
                c.editor.transformAutoBusy() && !c.editor.busy() && !c.editor.saving(),
            "Auto hold is not one real successful, undelivered result of the active operation");
    const auto success = *observed.held;
    require(success.operation.controller == &c.editor && success.operation.job &&
                success.operation.assetID == id.toStdString() &&
                success.operation.sourcePath == path.toStdString(),
            "Held successful result has the wrong source/controller/job identity");
    success.solved.validate();
    auto wouldApply = before;
    wouldApply.transform = success.solved;
    wouldApply.setEnabled(luma::Correction::transform, true);
    wouldApply.validate();
    require(success.solved.guided && !success.solved.neutral() && wouldApply != before &&
                fixtureReference(c, wouldApply, path) != beforePixels,
            "Successful-result positive control is neutral, invalid or visually indistinguishable");
    const auto stillHeld = gate.snapshot();
    require(!stillHeld.released && !stillHeld.leftHold && !stillHeld.timedOut &&
                !stillHeld.hookFailed && stillHeld.deliveries == 0,
            "Successful-result positive control outlived the real worker hold");
    full(c, id, path, before);
    return success;
}
AutoTransformProbeSnapshot finalDelivery(AutoTransformResultGate &gate) {
    const auto observed = gate.snapshot();
    require(
        observed.successfulArrivals == 1 && observed.deliveries == 1 && observed.rejections == 1 &&
            observed.mismatchedDeliveries == 0 && observed.released && observed.leftHold &&
            !observed.timedOut && !observed.hookFailed && !observed.deliveryBeforeRelease,
        "Final worker/GUI quiescence has duplicate, mismatched or failed Auto delivery evidence");
    return observed;
}
AutoTransformProbeSnapshot releaseAndObserve(Context &c, AutoTransformResultGate &gate,
                                             const AutoTransformSuccess &held) {
    gate.release(); // Explicit acceptance release, not destructor cleanup.
    c.driver->wait(
        [&] {
            const auto observed = gate.snapshot();
            return observed.deliveries > 0 || observed.hookFailed || observed.timedOut;
        },
        "the same original OLD success callback was delivered", 10000);
    const auto observed = gate.snapshot();
    require(observed.held && observed.successfulArrivals == 1 && observed.deliveries == 1 &&
                observed.rejections == 1 && observed.lastDelivered && observed.rejection &&
                observed.released && observed.leftHold && !observed.timedOut &&
                !observed.hookFailed && !observed.deliveryBeforeRelease &&
                observed.mismatchedDeliveries == 0 &&
                observed.held->operation.sameAs(held.operation) &&
                observed.lastDelivered->sameAs(held.operation) &&
                observed.rejection->operation.sameAs(held.operation) &&
                observed.rejection->controllerPresent &&
                observed.rejection->currentRevision != held.operation.revision,
            "OLD success was not rejected exactly once by its original stale Auto revision guard");
    // State is only a terminal-work oracle; success was proved by the post-solve hook.
    c.driver->wait([&] { return held.operation.job->state.load() == luma::JobState::cancelled; },
                   "actual cancelled transform worker returned after original callback enqueue",
                   5000);
    return finalDelivery(gate);
}
void unchangedSaved(Context &c, const luma::AssetSnapshot &expected) {
    const auto actual = snapshot(c, QString::fromStdString(expected.asset.id));
    require(actual.asset.id == expected.asset.id && actual.asset.path == expected.asset.path &&
                actual.asset.checksum == expected.asset.checksum &&
                actual.asset.edits == expected.asset.edits && actual.revision == expected.revision,
            "Late Auto changed a saved full document, source identity or durable edit revision");
}
} // namespace

bool autoTransformRaceCandidate(Context &c, const QString &id) {
    if (id != identifier && id != "UX-RACE-005")
        return false;
    auto &d = *c.driver;
    const auto aPath = QFileInfo(c.fixture + "/stress/Building.DNG").canonicalFilePath();
    const auto bPath = QFileInfo(c.fixture + "/Photo-0000.png").canonicalFilePath();
    require(!aPath.isEmpty() && !bPath.isEmpty() && aPath != bPath &&
                luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 0,
            "RACE005 needs fresh catalog, calibrated 320x240 Building and one small public PNG");
    const auto aHash = hash(aPath), bHash = hash(bPath);
    // Declared file-chooser fixture seam only. Import processing/opening use actual controls.
    d.object("library.importDialog")
        ->setProperty("sources", QVariant::fromValue(QList<QUrl>{QUrl::fromLocalFile(aPath),
                                                                 QUrl::fromLocalFile(bPath)}));
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "RACE005 actual Import dialog opened");
    d.choose("library.importMode", 0);
    if (d.object("library.importDuplicates")->property("checked").toBool())
        d.click("library.importDuplicates");
    d.click("library.importStart");
    d.wait(
        [&] {
            return !c.library.importing() && !c.library.working() && !c.library.loading() &&
                   c.library.count() == 2;
        },
        "RACE005 two actual imported assets", 30000);
    require(c.library.error().isEmpty(), c.library.error());
    const auto ids = luma::Catalog(c.catalogFile.toStdString()).libraryIDs({});
    require(ids.size() == 2, "RACE005 import has unexpected membership");
    QString aID, bID;
    for (const auto &assetID : ids) {
        const auto saved = snapshot(c, QString::fromStdString(assetID));
        if (QString::fromStdString(saved.asset.path.string()) == aPath)
            aID = QString::fromStdString(assetID);
        else if (QString::fromStdString(saved.asset.path.string()) == bPath)
            bID = QString::fromStdString(assetID);
    }
    require(!aID.isEmpty() && !bID.isEmpty() && aID != bID,
            "Imported bounded sources do not have two independent exact UUIDs");
    const Document neutral;
    require(snapshot(c, aID).asset.edits == neutral && snapshot(c, bID).asset.edits == neutral &&
                snapshot(c, aID).asset.metadata.width == 320 &&
                snapshot(c, aID).asset.metadata.height == 240,
            "Calibrated Auto fixture has unexpected initial documents or source dimensions");
    const auto aNeutralPixels = fixtureReference(c, neutral, aPath);
    const auto bNeutralPixels = fixtureReference(c, neutral, bPath);
    require(aNeutralPixels != bNeutralPixels, "RACE005 source pixels are indistinguishable");
    d.doubleClick(card(c, bID, false));
    rendered(c, bID, bPath, neutral, bNeutralPixels);
    Document bDocument = neutral;
    bDocument.exposure = 5;
    seed(c, bID, bPath, "develop.exposureSlider", neutral, bDocument);
    const auto bPixels = fixtureReference(c, bDocument, bPath);
    require(bPixels != bNeutralPixels, "B seed has no positive pixel effect");
    switchPhoto(c, aID, aPath, neutral, aNeutralPixels);
    Document aDocument = neutral;
    aDocument.transform.rotate = 45;
    seed(c, aID, aPath, "develop.transform.rotateSlider", neutral, aDocument);
    const auto aPixels = fixtureReference(c, aDocument, aPath);
    require(aPixels != aNeutralPixels, "A seed has no positive pixel effect");
    const auto bSaved = snapshot(c, bID);
    AutoTransformSuccess cancelResult;
    QJsonObject cancelEvidence, switchEvidence;
    {
        const auto aSaved = snapshot(c, aID);
        AutoTransformResultGate gate(c.editor);
        DocumentAudit audit(c, aID, aDocument, bID, bDocument);
        try {
            cancelResult = acquire(c, gate, aID, aPath, aDocument, aPixels);
            const auto oldURL = c.editor.previewUrl();
            QSignalSpy cancels(d.object("transformAutoCancel"), SIGNAL(clicked()));
            require(cancels.isValid(), "No observable actual Auto Cancel button");
            c.reveal("transformAutoCancel");
            d.click("transformAutoCancel");
            require(cancels.count() == 1 && gate.snapshot().deliveries == 0,
                    "Real Cancel did not precede original OLD success delivery");
            full(c, aID, aPath, aDocument);
            idle(c, true, false);
            const auto delivered = releaseAndObserve(c, gate, cancelResult);
            require(delivered.rejection->currentLoadRevision ==
                            cancelResult.operation.loadRevision &&
                        delivered.rejection->currentEditRevision ==
                            cancelResult.operation.editRevision &&
                        c.editor.previewUrl() == oldURL,
                    "Cancel changed load/edit identity or allowed an OLD rerender");
            rendered(c, aID, aPath, aDocument, aPixels);
            idle(c, true, false);
            unchangedSaved(c, aSaved);
            unchangedSaved(c, bSaved);
            audit.assertClean();
            cancelEvidence = {{"probe", probeJSON(finalDelivery(gate))},
                              {"document_audit", audit.json()},
                              {"a_document", documentJSON(aDocument)},
                              {"b_document", documentJSON(bDocument)}};
            writeJson(c.root + "/race005-cancel.json", cancelEvidence);
        } catch (...) {
            // Persist the original failed boundary before gate RAII can release the worker.
            try {
                writeJson(c.root + "/race005-cancel-failure.json",
                          {{"probe_before_cleanup", probeJSON(gate.snapshot())},
                           {"document_audit", audit.json()},
                           {"actual_document", documentJSON(c.editor.currentSettings())}});
            } catch (...) {
                std::fputs(
                    "RACE005 failure evidence could not be recorded; original exception retained\n",
                    stderr);
            }
            throw;
        }
    }
    // Exact one-entry history oracle for Cancel, including persistence of both endpoints.
    d.click("main.undoButton");
    rendered(c, aID, aPath, neutral, aNeutralPixels);
    naturalSave(c, aID, neutral);
    idle(c, false, true);
    d.click("main.redoButton");
    rendered(c, aID, aPath, aDocument, aPixels);
    naturalSave(c, aID, aDocument);
    idle(c, true, false);
    const auto aSavedAfterHistory = snapshot(c, aID);
    {
        AutoTransformResultGate gate(c.editor);
        DocumentAudit audit(c, aID, aDocument, bID, bDocument);
        try {
            const auto switchResult = acquire(c, gate, aID, aPath, aDocument, aPixels);
            require(switchResult.operation.revision != cancelResult.operation.revision &&
                        switchResult.operation.job.get() != cancelResult.operation.job.get() &&
                        switchResult.operation.loadRevision ==
                            cancelResult.operation.loadRevision &&
                        switchResult.operation.editRevision > cancelResult.operation.editRevision,
                    "Cancel/switch branches did not compute two distinct genuine OLD results");
            switchPhoto(c, bID, bPath, bDocument, bPixels);
            require(gate.snapshot().deliveries == 0,
                    "Original OLD success delivered before actual new-photo readiness");
            const auto bURL = c.editor.previewUrl();
            const auto delivered = releaseAndObserve(c, gate, switchResult);
            require(delivered.rejection->currentLoadRevision !=
                            switchResult.operation.loadRevision &&
                        c.editor.previewUrl() == bURL,
                    "Photo switch did not advance load identity or OLD result rerendered B");
            rendered(c, bID, bPath, bDocument, bPixels);
            idle(c, false, false);
            unchangedSaved(c, aSavedAfterHistory);
            unchangedSaved(c, bSaved);
            audit.assertClean();
            switchEvidence = {{"probe", probeJSON(finalDelivery(gate))},
                              {"document_audit", audit.json()},
                              {"a_document", documentJSON(aDocument)},
                              {"b_document", documentJSON(bDocument)}};
            writeJson(c.root + "/race005-switch.json", switchEvidence);
        } catch (...) {
            try {
                writeJson(c.root + "/race005-switch-failure.json",
                          {{"probe_before_cleanup", probeJSON(gate.snapshot())},
                           {"document_audit", audit.json()},
                           {"actual_document", documentJSON(c.editor.currentSettings())}});
            } catch (...) {
                std::fputs(
                    "RACE005 failure evidence could not be recorded; original exception retained\n",
                    stderr);
            }
            throw;
        }
    }
    // Actual A→B→A return must load exactly the retained A document, with fresh history.
    switchPhoto(c, aID, aPath, aDocument, aPixels);
    unchangedSaved(c, aSavedAfterHistory);
    unchangedSaved(c, bSaved);
    require(hash(aPath) == aHash && hash(bPath) == bHash &&
                luma::Catalog(c.catalogFile.toStdString()).libraryIDs({}) == ids &&
                snapshot(c, aID).asset.checksum == aHash.toStdString() &&
                snapshot(c, bID).asset.checksum == bHash.toStdString(),
            "RACE005 changed original source bytes/checksum/membership");
    luma::Catalog(c.catalogFile.toStdString()).verify();
    writeJson(
        c.root + "/race005-summary.json",
        {{"scope",
          "two actual successful Auto results held after solve before original enqueue/delivery"},
         {"fixture_boundary",
          "file chooser result only; actual Import/Auto/Cancel/filmstrip/history controls"},
         {"operations", 2},
         {"auto_stress_cycles", 0},
         {"cancel", cancelEvidence},
         {"switch", switchEvidence},
         {"a_source_sha256", aHash},
         {"b_source_sha256", bHash},
         {"final_document", documentJSON(c.editor.currentSettings())}});
    return true;
}
} // namespace ux
