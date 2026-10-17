#include "apps/shared/PhotoDecoder.h"
#include "core/catalog/Catalog.h"
#include "core/render/CpuRenderer.h"
#include "tests/ux/QueuedCallGate.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSignalSpy>
#include <filesystem>

namespace ux {
namespace {
struct ResetFull {
    SqliteFault &fault;
    ~ResetFull() {
        fault.diskFull(false);
    }
};
QByteArray digest(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read owned export evidence: " + path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file), "Cannot hash complete export evidence: " + path);
    return hash.result();
}
QImage samples(QImage image) {
    image = image.convertToFormat(QImage::Format_RGBA8888);
    image.setDevicePixelRatio(1);
    image.setColorSpace({});
    return image;
}
QImage reference(const QString &path, const luma::EditDocument &document) {
    const auto decoded = luma::PhotoDecoder().decode(path.toStdString());
    require(decoded.image.width <= 512 && decoded.image.height <= 512 &&
                decoded.image.alpha.empty(),
            "Export pixel oracle requires a small opaque public-derived fixture");
    const auto pixels =
        luma::renderCPU(decoded.image, document, decoded.metadata.orientation, {}, {},
                        decoded.calibration, luma::DetailDiagnostic::none, 1, decoded.metadata);
    return QImage(pixels.rgba.data(), pixels.width, pixels.height, pixels.width * 4,
                  QImage::Format_RGBA8888)
        .copy();
}
void visibleDocument(Context &c) {
    const auto expected = fixtureReference(c);
    c.driver->wait(
        [&] {
            auto *image = c.driver->item("develop.preview");
            return !c.editor.busy() && !c.editor.previewUrl().isEmpty() && image->isVisible() &&
                   image->property("status").toInt() == 1 &&
                   image->property("source").toUrl().toString() == c.editor.previewUrl() &&
                   c.provider->requestImage({}, nullptr, {}) == expected;
        },
        "live Develop document has its own exact preview");
    c.driver->frame();
}
void noTemporaryFiles(Context &c) {
    QDirIterator iterator(c.root + "/exports", QDir::Files | QDir::Hidden,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const auto path = iterator.next();
        const auto name = QFileInfo(path).fileName();
        require(!name.startsWith(".luma-") && !name.endsWith(".tmp"),
                "Export leaked an incomplete temporary output: " + path);
    }
}
void settings(Context &c, const QString &name = "{filename}", int conflict = 1) {
    auto &d = *c.driver;
    require(QDir().mkpath(c.root + "/exports"), "Cannot prepare owned export directory");
    d.click("exportButton");
    d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
           "extra export settings opened");
    d.type("export.destination", c.root + "/exports/./");
    d.type("exportNaming", name);
    d.choose("export.conflict", conflict);
    d.choose("exportFormat", 1);
}
void defaultPNG(Context &c) {
    const auto options = c.exporter.settings();
    require(
        options.value("format").toInt() == 1 && options.value("colorSpace").toInt() == 0 &&
            options.value("resize").toInt() == 0 && options.value("outputMedium").toInt() == 0 &&
            options.value("watermarkKind").toInt() == 0 && options.value("metadata").toInt() == 0,
        "Independent PNG oracle does not match the actual export processing options");
}
void success(Context &c, int count) {
    c.driver->wait([&] { return !c.exporter.working(); }, "extra export completed", 60000);
    require(c.exporter.total() == count && c.exporter.completed() == count &&
                c.exporter.succeeded() == count && c.exporter.skipped() == 0 &&
                c.exporter.failures().isEmpty() && c.exporter.error().isEmpty(),
            "Export queue did not complete its exact immutable item set");
    defaultPNG(c);
    noTemporaryFiles(c);
}
void selectAll(Context &c, int count) {
    if (!c.window->property("libraryMode").toBool())
        c.driver->click("main.libraryButton");
    c.driver->click("library.selectAll");
    c.driver->wait([&] { return !c.library.selecting() && c.library.selectedCount() == count; },
                   "extra export selection prepared");
}
bool firstCompletion(Context &c, int count, const QString &filename) {
    // PNG writeImage emits its one final progress(1) after install+guard commit.
    // One export worker posts progress and then completion in FIFO order. The
    // next MetaCall at progress==1 is the real first-item completion; planning
    // has already installed immutable snapshots and released the live editor.
    return c.exporter.working() && !c.exporter.waitingForSave() && !c.exporter.paused() &&
           c.exporter.total() == count && c.exporter.completed() == 0 &&
           c.exporter.progress() == 1 && c.exporter.currentFile() == filename &&
           !c.editor.editingLocked();
}
QString sourceAt(Context &c, int row) {
    c.driver->wait(
        [&] {
            return !c.library.loading() && !c.library.data(c.library.index(row), LibraryModel::Path)
                                                .toUrl()
                                                .toLocalFile()
                                                .isEmpty();
        },
        "export reference source metadata is ready");
    return c.library.data(c.library.index(row), LibraryModel::Path).toUrl().toLocalFile();
}
void repeatedCancel(Context &c) {
    auto &d = *c.driver;
    c.import(10);
    selectAll(c, 10);
    const auto selected = c.library.selectionIDs();
    const auto firstPath = sourceAt(c, 0);
    const auto firstName = QFileInfo(firstPath).fileName();
    const auto firstOutput = c.root + "/exports/" + firstName;
    const auto expected = reference(firstPath, {});
    settings(c);
    require(
        !QFileInfo::exists(firstOutput) &&
            QDir(c.root + "/exports")
                .entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)
                .isEmpty(),
        "Repeated Cancel requires a fresh output directory and its unsuffixed first checkpoint");
    QSignalSpy clicked(d.object("export.cancel"), SIGNAL(clicked()));
    QSignalSpy closeReady(&c.exporter, &ExportController::closeReady);
    require(clicked.isValid(), "Export Cancel has no observable clicked signal");
    QObject observationLifetime;
    bool wasWorking = false;
    int idleTransitions = 0;
    QObject::connect(&c.exporter, &ExportController::changed, &observationLifetime, [&] {
        const bool now = c.exporter.working();
        if (wasWorking && !now)
            ++idleTransitions;
        wasWorking = now;
    });
    QByteArray committedHash;
    QueuedCallGate gate(
        c.exporter, [&] { return firstCompletion(c, 10, firstName); },
        [&] {
            defaultPNG(c);
            require(samples(QImage(firstOutput)) == expected,
                    "Cancel boundary lacks the exact first committed PNG checkpoint");
            committedHash = digest(firstOutput);
            for (int press = 0; press < 20; ++press) {
                d.click("export.cancel");
                require(c.exporter.working() && c.exporter.completed() == 0 &&
                            c.exporter.succeeded() == 0 && c.exporter.total() == 10,
                        "Repeated Cancel changed the held queue before first completion delivery");
            }
            require(clicked.count() == 20 && idleTransitions == 0,
                    "Repeated Cancel was not accepted exactly twenty times by one active queue");
        });
    d.click("export.selection");
    d.wait([&] { return gate.caught; },
           "first committed export completion held for repeated Cancel");
    if (gate.failure)
        std::rethrow_exception(gate.failure);
    d.wait([&] { return !c.exporter.working(); }, "repeated Cancel settled once");
    require(
        c.exporter.total() == 10 && c.exporter.completed() == 1 && c.exporter.succeeded() == 1 &&
            c.exporter.skipped() == 0 && c.exporter.failures().isEmpty() &&
            c.exporter.error().isEmpty() && c.exporter.status().startsWith("Cancelled") &&
            idleTransitions == 1 && closeReady.isEmpty() && c.window->isVisible(),
        "Repeated Cancel lost a committed checkpoint, resumed jobs or completed more than once");
    require(QDir(c.root + "/exports").entryList({"*.png"}, QDir::Files).size() == 1 &&
                digest(firstOutput) == committedHash && c.library.selectionIDs() == selected &&
                !c.library.working() && !c.editor.editingLocked(),
            "Cancelled queue leaked later outputs or retained controller/selection locks");
    noTemporaryFiles(c);
}
void immutableDevelopQueue(Context &c) {
    auto &d = *c.driver;
    c.import(2);
    d.doubleClick(c.card(1));
    c.loaded();
    c.saved();
    const auto photo = c.editor.photoID();
    const auto source = c.editor.sourcePath();
    const auto oldDocument = c.editor.currentSettings();
    require(source.endsWith("Photo-0001.png") && oldDocument == luma::EditDocument{},
            "Immutable queue needs a pristine B sorted after A");
    const auto firstSource = sourceAt(c, 0);
    const auto firstName = QFileInfo(firstSource).fileName();
    const auto firstOutput = c.root + "/exports/" + firstName;
    const auto secondOutput = c.root + "/exports/" + QFileInfo(source).fileName();
    const auto firstPixels = reference(firstSource, {});
    const auto oldPixels = reference(source, oldDocument);
    selectAll(c, 2);
    const auto selected = c.library.selectionIDs();
    d.click("main.developButton");
    c.reveal("develop.exposureSlider");
    settings(c);
    require(!QFileInfo::exists(firstOutput) && !QFileInfo::exists(secondOutput),
            "Snapshot export outputs must not exist before the real queue starts");
    luma::EditDocument newDocument;
    QImage newPixels;
    QueuedCallGate gate(
        c.exporter, [&] { return firstCompletion(c, 2, firstName); },
        [&] {
            defaultPNG(c);
            require(samples(QImage(firstOutput)) == firstPixels &&
                        !QFileInfo::exists(secondOutput) && c.editor.photoID() == photo &&
                        c.editor.currentSettings() == oldDocument &&
                        !c.window->property("libraryMode").toBool(),
                    "Snapshot-edit boundary is not between completed A and unstarted queued B");
            c.reveal("develop.exposureSlider");
            d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
            newDocument = c.editor.currentSettings();
            auto expected = oldDocument;
            expected.exposure = newDocument.exposure;
            require(newDocument != oldDocument && newDocument == expected &&
                        c.editor.photoID() == photo && c.exporter.working(),
                    "Real Develop input was refused or changed another queued asset/group");
            c.saved();
            visibleDocument(c);
            const auto stored =
                luma::Catalog(c.catalogFile.toStdString()).assetByID(photo.toStdString());
            require(stored && stored->edits == newDocument && firstCompletion(c, 2, firstName) &&
                        !QFileInfo::exists(secondOutput),
                    "B did not save its live edit before the queued B worker started");
            newPixels = reference(source, newDocument);
            require(newPixels != oldPixels,
                    "Queued B's old/new documents have indistinguishable pixels");
            d.log({{"action", "live_develop_edit_after_immutable_plan_before_queued_file"},
                   {"asset", photo},
                   {"completed_notifications_held", 1},
                   {"second_output_absent", true}});
        });
    d.click("export.selection");
    d.wait([&] { return gate.caught; }, "queued A completion held across live B edit and save");
    if (gate.failure)
        std::rethrow_exception(gate.failure);
    success(c, 2);
    require(!newPixels.isNull() && samples(QImage(firstOutput)) == firstPixels &&
                samples(QImage(secondOutput)) == oldPixels &&
                samples(QImage(secondOutput)) != newPixels &&
                c.editor.currentSettings() == newDocument && c.editor.photoID() == photo &&
                c.library.selectionIDs() == selected,
            "Live Develop changes altered an already-planned queue snapshot or were discarded");
    const auto stored = luma::Catalog(c.catalogFile.toStdString()).assetByID(photo.toStdString());
    require(stored && stored->edits == newDocument,
            "Completing the immutable queue overwrote the newer saved editor document");
    d.click("main.undoButton");
    require(c.editor.currentSettings() == oldDocument && !c.editor.canUndo(),
            "Concurrent export polluted the one live-edit Undo entry");
    d.click("main.redoButton");
    require(c.editor.currentSettings() == newDocument, "Concurrent export damaged live-edit Redo");
    c.saved();
}
void saveFailureBeforeExport(Context &c) {
    auto &d = *c.driver;
    c.openFirst();
    c.saved();
    const auto photo = c.editor.photoID();
    const auto source = c.editor.sourcePath();
    const auto before = c.editor.currentSettings();
    const auto oldPixels = reference(source, before);
    const auto initial = c.exportPNG("save-retry");
    require(samples(QImage(initial)) == oldPixels,
            "Save/retry export PNG positive control does not match its document");
    const auto initialHash = digest(initial);
    const auto output = c.root + "/exports/save-retry-2.png";
    defaultPNG(c);
    require(c.exporter.settings().value("conflict").toInt() == 1 && !QFileInfo::exists(output),
            "Retry fixture requires Unique and an absent second output before autosave fails");
    const ResetFull reset{c.storageFault};
    c.storageFault.diskFull(true);
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
    const auto pending = c.editor.currentSettings();
    auto expected = before;
    expected.exposure = pending.exposure;
    require(pending != before && pending == expected,
            "Export-save-failure fixture did not accept one isolated live edit");
    d.wait([&] { return !c.editor.error().isEmpty() && c.storageFault.rejectedWrites() > 0; },
           "actual autosave WAL write failed with SQLITE_FULL");
    const auto oldStored =
        luma::Catalog(c.catalogFile.toStdString()).assetByID(photo.toStdString());
    require(oldStored && oldStored->edits == before && c.editor.saving(),
            "Autosave failure either persisted a partial edit or discarded its pending state");
    d.click("exportButton");
    d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
           "saved export settings reopened after autosave failure");
    d.click("export.current");
    d.wait(
        [&] {
            return c.exporter.waitingForSave() && c.editor.saving() &&
                   d.item("export.retrySave")->isVisible();
        },
        "export visibly waits for failed autosave and offers Retry");
    const auto sample = c.metrics.json()["rss_samples"].toInteger();
    d.wait([&] { return c.metrics.json()["rss_samples"].toInteger() >= sample + 2; },
           "live UI remains in the failed-save export wait boundary");
    require(c.exporter.working() && c.exporter.waitingForSave() && c.exporter.total() == 0 &&
                c.exporter.completed() == 0 && c.exporter.succeeded() == 0 &&
                !QFileInfo::exists(output) &&
                QDir(c.root + "/exports").entryList({"*.png"}, QDir::Files).size() == 1 &&
                c.editor.currentSettings() == pending && c.editor.editingLocked() &&
                d.item("export.retrySave")->isEnabled(),
            "Export ran with stale edits while autosave was still failed");
    c.storageFault.diskFull(false);
    QSignalSpy retried(d.object("export.retrySave"), SIGNAL(clicked()));
    require(retried.isValid(), "Export Retry has no observable clicked signal");
    d.click("export.retrySave");
    success(c, 1);
    const auto newPixels = reference(source, pending);
    const auto stored = luma::Catalog(c.catalogFile.toStdString()).assetByID(photo.toStdString());
    require(retried.count() == 1 && newPixels != oldPixels &&
                samples(QImage(output)) == newPixels && samples(QImage(output)) != oldPixels &&
                digest(initial) == initialHash && stored && stored->edits == pending &&
                c.editor.currentSettings() == pending && !c.editor.saving() &&
                !c.editor.editingLocked() && c.editor.error().isEmpty(),
            "Retry did not unblock one export of the exact newest saved state");
    visibleDocument(c);
}
void protectedAliases(Context &c) {
    auto &d = *c.driver;
    const auto owned = c.root + "/owned-originals";
    require(QDir().mkpath(owned) &&
                QFile::copy(c.fixture + "/Photo-0000.png", owned + "/Owned.png"),
            "Cannot prepare a private original for negative overwrite tests");
    const auto publicFixtures = c.fixture;
    c.fixture = owned;
    c.import(1);
    c.fixture = publicFixtures;
    d.doubleClick(c.card(0));
    c.loaded();
    // Keep a real catalog connection open across the actual save below so WAL
    // and SHM exist and cannot disappear when a short-lived worker closes.
    luma::Catalog keeper(c.catalogFile.toStdString());
    require(keeper.libraryCount() == 1, "Private catalog keeper cannot read its asset");
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.6, .5});
    c.saved();
    const auto document = c.editor.currentSettings();
    const auto photo = c.editor.photoID();
    const auto original = c.editor.sourcePath();
    const auto originalHash = digest(original);
    const auto positive = c.exportPNG("protection-positive-control");
    require(samples(QImage(positive)) == reference(original, document),
            "Protection fixture cannot successfully export an ordinary owned output");
    const auto positiveHash = digest(positive);
    const QStringList targets{original, c.catalogFile, c.catalogFile + "-wal",
                              c.catalogFile + "-shm"};
    const QStringList names{"original", "catalog", "wal", "shm"};
    const QStringList reasons{
        "Export cannot replace the original image.", "Export cannot replace the catalog.",
        "Export cannot replace application state.", "Export cannot replace application state."};
    for (int targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
        const auto target = targets[targetIndex];
        require(target.startsWith(c.root + '/') && QFileInfo(target).isFile() &&
                    QFileInfo(target).size() > 0,
                "Protected alias target is not a live owned original/catalog sidecar");
        for (const bool symlink : {true, false}) {
            const auto name =
                "protected-" + names[targetIndex] + (symlink ? "-symlink" : "-hardlink");
            const auto alias = c.root + "/exports/" + name + ".png";
            const auto targetPath = std::filesystem::path(target.toStdString());
            const auto aliasPath = std::filesystem::path(alias.toStdString());
            if (symlink)
                std::filesystem::create_symlink(targetPath, aliasPath);
            else
                std::filesystem::create_hard_link(targetPath, aliasPath);
            require(std::filesystem::equivalent(aliasPath, targetPath),
                    "Negative export alias does not resolve to its protected target");
            const auto before = digest(target);
            settings(c, name, 2);
            d.click("export.current");
            d.wait([&] { return !c.exporter.working(); },
                   "Replace through protected alias refused");
            require(c.exporter.total() == 1 && c.exporter.completed() == 1 &&
                        c.exporter.succeeded() == 0 && c.exporter.skipped() == 0 &&
                        c.exporter.error().isEmpty() && c.exporter.failures().size() == 1 &&
                        c.exporter.failures().front() == name + ".png: " + reasons[targetIndex],
                    "Replace alias was accepted or failed for an unrelated reason");
            require(std::filesystem::equivalent(aliasPath, targetPath) &&
                        std::filesystem::is_symlink(aliasPath) == symlink &&
                        (!symlink || std::filesystem::read_symlink(aliasPath) == targetPath),
                    "Refused Replace nevertheless replaced or retargeted the protected alias");
            // SHM read marks can change under legitimate concurrent readers;
            // never pretend byte-exact SHM stability is a product invariant.
            const auto after = digest(target);
            if (targetIndex != 3)
                require(after == before,
                        "Refused Replace changed protected original/catalog/WAL bytes");
            d.log({{"action", "protected_replace_alias_refused"},
                   {"target", names[targetIndex]},
                   {"kind", symlink ? "symlink" : "hardlink"},
                   {"before_sha256", QString::fromLatin1(before.toHex())},
                   {"after_sha256", QString::fromLatin1(after.toHex())},
                   {"byte_equality_required", targetIndex != 3}});
            const auto stored = keeper.assetByID(photo.toStdString());
            require(
                stored && stored->edits == document && keeper.libraryCount() == 1 &&
                    c.editor.currentSettings() == document && digest(original) == originalHash &&
                    digest(positive) == positiveHash,
                "Protected Replace damaged catalog state, original bytes or the positive output");
            keeper.verify();
            noTemporaryFiles(c);
        }
    }
}
void libraryScrollDuringExport(Context &c) {
    auto &d = *c.driver;
    c.import(1000);
    selectAll(c, 1000);
    const auto selected = c.library.selectionIDs();
    settings(c);
    d.click("export.selection");
    d.wait(
        [&] {
            return c.exporter.working() && c.exporter.total() == 1000 &&
                   c.exporter.completed() >= 1 && c.exporter.completed() < 1000 &&
                   !c.exporter.waitingForSave() && !c.exporter.paused() && !c.library.working();
        },
        "real non-paused export queue has committed work before simultaneous scroll");
    auto *grid = d.item("library.grid");
    const int before = c.exporter.completed();
    int wheelInputs = 0;
    for (int cycle = 0; cycle < 3; ++cycle) {
        for (const int delta : {-1200, -1200, 1200, 1200}) {
            require(c.exporter.working() && c.exporter.completed() < 1000 && !c.exporter.paused() &&
                        !c.exporter.pausePending(),
                    "Scroll fixture missed its actual running export overlap");
            const double y = grid->property("contentY").toDouble();
            d.wheel("library.grid", delta);
            ++wheelInputs;
            d.frame();
            d.wait([&] { return !grid->property("moving").toBool(); },
                   "Library wheel input settled while export keeps running");
            const double next = grid->property("contentY").toDouble();
            require(
                (delta < 0 ? next > y + 1 : next < y - 1) && c.exporter.working() &&
                    c.exporter.completed() < 1000 && c.library.selectionIDs() == selected &&
                    !c.library.loading() && c.library.error().isEmpty(),
                "Actual Library wheel input was swallowed, lost selection or outlived the queue");
            d.log({{"action", "library_wheel_while_export_running"},
                   {"delta", delta},
                   {"content_y", next},
                   {"completed", c.exporter.completed()},
                   {"total", c.exporter.total()}});
        }
        if (c.exporter.completed() > before)
            break;
    }
    require(c.exporter.completed() > before && c.exporter.working() && wheelInputs >= 4,
            "The real export worker made no progress during the actual scroll sequence");
    const int atCancel = c.exporter.completed();
    d.click("export.cancel");
    d.wait([&] { return !c.exporter.working(); }, "scroll stress export cancelled and cleaned up",
           60000);
    require(c.exporter.total() == 1000 && c.exporter.completed() >= atCancel &&
                c.exporter.completed() < 1000 && c.exporter.succeeded() == c.exporter.completed() &&
                c.exporter.skipped() == 0 && c.exporter.failures().isEmpty() &&
                c.exporter.error().isEmpty() && c.library.selectionIDs() == selected &&
                QDir(c.root + "/exports").entryList({"*.png"}, QDir::Files).size() ==
                    c.exporter.succeeded(),
            "Export/scroll overlap lost committed files, leaked selection or failed cleanup");
    noTemporaryFiles(c);
    require(luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 1000,
            "Concurrent Library scrolling/export changed durable catalog membership");
    c.checkData();
}
} // namespace

bool exportExtraScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-EXP-009", "UX-EXP-014", "UX-EXP-016",
                                         "UX-EXP-019", "UX-EXP-021", "UX-EXP-022"};
    if (!supported.contains(id))
        return false;
    if (id == "UX-EXP-009")
        repeatedCancel(c);
    else if (id == "UX-EXP-014")
        protectedAliases(c);
    else if (id == "UX-EXP-016" || id == "UX-EXP-022")
        immutableDevelopQueue(c);
    else if (id == "UX-EXP-019")
        saveFailureBeforeExport(c);
    else
        libraryScrollDuringExport(c);
    return true;
}
} // namespace ux
