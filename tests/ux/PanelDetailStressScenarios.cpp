#include "apps/shared/PhotoDecoder.h"
#include "core/render/CpuRenderer.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QSignalSpy>
#include <QStyleHints>
#include <memory>
#include <sqlite3.h>
#include <stdexcept>
#include <vector>

namespace ux {
namespace {
struct KeyboardNavigation {
    Qt::TabFocusBehavior previous = QGuiApplication::styleHints()->tabFocusBehavior();
    KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    }
    ~KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(previous);
    }
};
struct ReleaseWrites {
    SqliteFault &fault;
    ~ReleaseWrites() {
        fault.holdWrites(false);
    }
};
QByteArray read(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read stress evidence: " + path);
    return file.readAll();
}
QByteArray digest(const QString &path) {
    return QCryptographicHash::hash(read(path), QCryptographicHash::Sha256);
}
struct FileSnapshot {
    bool exists = false;
    QByteArray bytes;
    bool operator==(const FileSnapshot &) const = default;
};
FileSnapshot fileSnapshot(const QString &path) {
    return QFileInfo::exists(path) ? FileSnapshot{true, read(path)} : FileSnapshot{};
}
QVariant qmlValue(QObject *object, const char *property) {
    const auto value = object->property(property);
    return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant()
                                                               : value;
}
QJsonArray sqlRows(sqlite3 *database, const char *sql) {
    sqlite3_stmt *raw = nullptr;
    const int prepared = sqlite3_prepare_v2(database, sql, -1, &raw, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw,
                                                                               sqlite3_finalize);
    require(prepared == SQLITE_OK, "Cannot prepare stress journal snapshot");
    QJsonArray result;
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW) {
        QJsonArray row;
        for (int column = 0; column < sqlite3_column_count(statement.get()); ++column) {
            if (sqlite3_column_type(statement.get(), column) == SQLITE_NULL)
                row.append(QJsonValue::Null);
            else
                row.append(QString::fromUtf8(
                    reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), column))));
        }
        result.append(row);
    }
    require(step == SQLITE_DONE, "Cannot finish stress journal snapshot");
    return result;
}
QByteArray journal(Context &c) {
    sqlite3 *raw = nullptr;
    const int opened =
        sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &raw, SQLITE_OPEN_READONLY, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
    require(opened == SQLITE_OK, "Cannot open stress journal for observation");
    return QJsonDocument(
               QJsonObject{
                   {"sessions",
                    sqlRows(database.get(), "SELECT id,mode,destination,state,owner_pid,"
                                            "skip_duplicates,created_at,finished_at "
                                            "FROM import_sessions ORDER BY id")},
                   {"items",
                    sqlRows(database.get(), "SELECT session_id,position,source_path,target_path,"
                                            "temporary_path,checksum,filesize,state,asset_id,error "
                                            "FROM import_items ORDER BY session_id,position")}})
        .toJson(QJsonDocument::Compact);
}
QVariantMap queueState(Context &c) {
    return {{"working", c.exporter.working()},     {"waiting", c.exporter.waitingForSave()},
            {"paused", c.exporter.paused()},       {"completed", c.exporter.completed()},
            {"total", c.exporter.total()},         {"succeeded", c.exporter.succeeded()},
            {"skipped", c.exporter.skipped()},     {"progress", c.exporter.progress()},
            {"current", c.exporter.currentFile()}, {"status", c.exporter.status()},
            {"error", c.exporter.error()},         {"failures", c.exporter.failures()}};
}
QJsonArray libraryMetadata(const luma::Catalog &catalog) {
    QJsonArray rows;
    for (const auto &item : catalog.libraryPage({}, 0, 3))
        rows.append(QJsonObject{{"id", QString::fromStdString(item.id)},
                                {"path", QString::fromStdString(item.path.string())},
                                {"checksum", QString::fromStdString(item.checksum)},
                                {"width", item.width},
                                {"height", item.height},
                                {"orientation", item.orientation},
                                {"rating", item.rating},
                                {"flag", item.flag},
                                {"label", QString::fromStdString(item.label)},
                                {"camera", QString::fromStdString(item.camera)}});
    return rows;
}
void metadataReady(Context &c) {
    c.driver->wait(
        [&] {
            return !c.library.loading() && !c.library.working() && c.library.count() == 3 &&
                   !c.library.data(c.library.index(0), LibraryModel::AssetID)
                        .toString()
                        .isEmpty() &&
                   !c.library.data(c.library.index(2), LibraryModel::Path).toUrl().isEmpty();
        },
        "three private stress assets have their final Library metadata");
}
void ownImport(Context &c) {
    auto &d = *c.driver;
    const auto folder = c.root + "/owned-input";
    require(c.library.count() == 0 && QDir().mkpath(folder),
            "Stress fixture requires an empty private catalog and owned input directory");
    QList<QUrl> sources;
    for (int index = 0; index < 3; ++index) {
        const auto input = c.fixture + QString("/Photo-%1.png").arg(index, 4, 10, QChar('0'));
        const auto output = folder + QString("/Owned-%1.png").arg(index, 4, 10, QChar('0'));
        QFile file(output);
        const auto bytes = read(input);
        require(!bytes.isEmpty() && file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                    file.write(bytes) == bytes.size(),
                "Cannot create a fresh private stress original");
        file.close();
        require(digest(input) == digest(output), "Private stress copy changed source bytes");
        sources.append(QUrl::fromLocalFile(output));
    }
    // Explicit chooser RESULT fixture setup; the production Import button accepts it.
    // This does not exercise or qualify the OS file chooser itself.
    require(d.object("library.importDialog")->setProperty("sources", QVariant::fromValue(sources)),
            "Private chooser-result fixture rejected its source URLs");
    d.log({{"action", "fixture_private_chooser_result"}, {"source_count", 3}});
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "private stress Import dialog opened");
    d.choose("library.importMode", 0);
    if (d.object("library.importDuplicates")->property("checked").toBool())
        d.click("library.importDuplicates");
    d.click("library.importStart");
    metadataReady(c);
    require(!c.library.importing() && c.library.error().isEmpty() &&
                luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 3,
            "Private stress Import did not finish exactly three assets");
    // Small worker completion does not prove that the modal exit transition ended.
    d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
           "private setup Import dialog exit transition completed");
    d.doubleClick(c.card(0));
    c.loaded();
    metadataReady(c);
    require(c.editor.currentSettings() == luma::EditDocument{} && !c.editor.canUndo() &&
                !c.editor.canRedo() && !c.editor.original(),
            "Private stress photo did not start with a clean full document/history");
}
void naturalSave(Context &c, const luma::EditDocument &document) {
    // No endEdit()/Context::saved(): verify the UI input's own release/autosave path.
    c.driver->wait(
        [&] {
            const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
            return !c.editor.saving() && !c.editor.busy() && c.editor.error().isEmpty() && active &&
                   active->id == c.editor.photoID().toStdString() && active->edits == document;
        },
        "stress document naturally committed in full");
}
void pixels(Context &c, const QImage &expected) {
    c.driver->wait(
        [&] {
            auto *image = c.driver->item("develop.preview");
            return !c.window->property("libraryMode").toBool() && image->isVisible() &&
                   image->property("status").toInt() == 1 &&
                   image->property("source").toUrl().toString() == c.editor.previewUrl() &&
                   c.provider->requestImage({}, nullptr, {}) == expected;
        },
        "stress full document has its exact Ready preview");
    c.driver->frame();
}
struct AssetBaseline {
    QString path;
    QByteArray sha;
    luma::EditDocument document;
};
struct Snapshot {
    QString source, asset, settingsPath;
    luma::EditDocument document;
    std::vector<std::string> members, selected;
    QMap<QString, AssetBaseline> assets;
    QJsonArray metadata;
    QByteArray journalBytes;
    QVariantMap exportSettings, exportQueue;
    QVariantList exportProfiles;
    FileSnapshot exportStore;
    bool original = false, undo = false, redo = false, libraryMode = false;
};
Snapshot snapshot(Context &c) {
    const luma::Catalog catalog(c.catalogFile.toStdString());
    Snapshot result;
    result.source = c.editor.sourcePath();
    result.asset = c.editor.photoID();
    result.document = c.editor.currentSettings();
    result.members = catalog.libraryIDs({});
    result.selected = c.library.selectionIDs();
    result.metadata = libraryMetadata(catalog);
    result.journalBytes = journal(c);
    result.original = c.editor.original();
    result.undo = c.editor.canUndo();
    result.redo = c.editor.canRedo();
    result.libraryMode = c.window->property("libraryMode").toBool();
    result.exportSettings = c.exporter.settings();
    result.exportProfiles = c.exporter.profiles();
    result.exportQueue = queueState(c);
    result.settingsPath = QFileInfo(c.catalogFile).absolutePath() + "/export-settings.json";
    result.exportStore = fileSnapshot(result.settingsPath);
    for (const auto &id : result.members) {
        const auto asset = catalog.assetByID(id);
        if (!asset)
            throw std::runtime_error("Stress baseline member is missing");
        const auto path = QString::fromStdString(asset->path.string());
        require(path.startsWith(c.root + "/owned-input/") &&
                    QString::fromLatin1(digest(path).toHex()).toStdString() == asset->checksum,
                "Stress baseline asset is not an intact private original");
        result.assets.insert(QString::fromStdString(id), {path, digest(path), asset->edits});
    }
    require(result.assets.size() == 3 && result.assets.contains(result.asset),
            "Stress baseline has an ambiguous active asset or membership");
    return result;
}
void invariant(Context &c, const Snapshot &before, const luma::EditDocument &document, bool undo,
               bool redo, bool exportUnchanged = true) {
    const luma::Catalog catalog(c.catalogFile.toStdString());
    const auto active = catalog.activeAsset();
    require(
        c.editor.sourcePath() == before.source && c.editor.photoID() == before.asset &&
            c.editor.currentSettings() == document && c.editor.original() == before.original &&
            c.editor.canUndo() == undo && c.editor.canRedo() == redo &&
            c.window->property("libraryMode").toBool() == before.libraryMode &&
            catalog.libraryIDs({}) == before.members &&
            c.library.selectionIDs() == before.selected &&
            libraryMetadata(catalog) == before.metadata && journal(c) == before.journalBytes &&
            active && active->id == before.asset.toStdString() && active->edits == document &&
            c.editor.hasPhoto() && !c.editor.busy() && !c.editor.saving() &&
            c.library.count() == static_cast<int>(before.members.size()) && !c.library.loading() &&
            !c.library.working() && !c.library.importing() && !c.library.selecting() &&
            !c.batch.working() && !c.exporter.working() && c.editor.error().isEmpty() &&
            c.library.error().isEmpty(),
        "Stress changed identity, full document/history, membership, selection or durable journal");
    for (auto it = before.assets.cbegin(); it != before.assets.cend(); ++it) {
        const auto asset = catalog.assetByID(it.key().toStdString());
        require(asset && QString::fromStdString(asset->path.string()) == it->path &&
                    digest(it->path) == it->sha &&
                    QString::fromLatin1(it->sha.toHex()).toStdString() == asset->checksum &&
                    asset->edits == (it.key() == before.asset ? document : it->document),
                "Stress changed an original, an unrelated full document or durable asset");
    }
    if (exportUnchanged)
        require(c.exporter.settings() == before.exportSettings &&
                    c.exporter.profiles() == before.exportProfiles &&
                    queueState(c) == before.exportQueue &&
                    fileSnapshot(before.settingsPath) == before.exportStore,
                "Opening/closing panels created or changed export settings, profiles or a queue");
}
struct StepControl {
    QString parameter;
    double luma::EditDocument::*member;
    double minimum, maximum;
    int edits;
};
void focusSlider(Context &c, const StepControl &control) {
    const auto name = control.parameter + "Slider";
    c.reveal(name);
    auto *slider = c.driver->item(name);
    const auto before = c.editor.currentSettings();
    require(slider->isVisible() && slider->isEnabled() &&
                slider->property("stepSize").toDouble() == 1 &&
                slider->property("from").toDouble() == control.minimum &&
                slider->property("to").toDouble() == control.maximum,
            "Stress keyboard slider is unavailable or has unexpected range/step");
    for (int tab = 0; tab < 240 && !slider->hasActiveFocus(); ++tab)
        c.driver->key(Qt::Key_Tab);
    c.reveal(name);
    require(slider->hasActiveFocus() && c.window->activeFocusItem() == slider &&
                QGuiApplication::focusWindow() == c.window &&
                !slider->property("pressed").toBool() && c.editor.currentSettings() == before,
            "Real Tab navigation missed the stress slider or changed the document");
}
void historySentinel(Context &c) {
    const StepControl control{"sharpen.amount", &luma::EditDocument::sharpenAmount, 0, 150, 2};
    focusSlider(c, control);
    for (int index = 1; index <= 2; ++index) {
        auto expected = luma::EditDocument{};
        expected.sharpenAmount = index;
        c.driver->key(Qt::Key_Right);
        require(c.editor.currentSettings() == expected && !c.editor.canRedo() &&
                    !c.driver->object("sharpen.amountSlider")->property("pressed").toBool(),
                "Panel stress did not prepare two independent real UI edits");
    }
    c.driver->click("main.undoButton");
    auto expected = luma::EditDocument{};
    expected.sharpenAmount = 1;
    require(c.editor.currentSettings() == expected && c.editor.canUndo() && c.editor.canRedo(),
            "Panel stress failed to prepare both history directions");
    naturalSave(c, expected);
    pixels(c, fixtureReference(c, expected, c.editor.sourcePath()));
}
void proveSentinelHistory(Context &c, const Snapshot &before) {
    auto second = before.document;
    second.sharpenAmount = 2;
    const luma::EditDocument empty;
    const std::vector<luma::EditDocument> expected{second, before.document, empty, before.document,
                                                   second, before.document};
    const QStringList inputs{"main.redoButton", "main.undoButton", "main.undoButton",
                             "main.redoButton", "main.redoButton", "main.undoButton"};
    for (int index = 0; index < inputs.size(); ++index) {
        c.driver->click(inputs[index]);
        require(c.editor.currentSettings() == expected[static_cast<std::size_t>(index)],
                "Panel cycles changed the exact pre-existing Undo/Redo order");
        if (index == 2)
            require(!c.editor.canUndo() && c.editor.canRedo(),
                    "Panel cycles added spurious Undo entries");
        if (index == 4)
            require(c.editor.canUndo() && !c.editor.canRedo(),
                    "Panel cycles added spurious Redo entries");
    }
    naturalSave(c, before.document);
}
QJsonObject sample(Context &c, int completed) {
    const auto metrics = c.metrics.json();
    return {{"completed_cycles_or_edits", completed},
            {"duration_ms_including_setup", metrics.value("duration_ms")},
            {"rss_last_mb_raw", metrics.value("rss_last_mb")},
            {"rss_peak_mb_raw", metrics.value("rss_peak_mb")},
            {"rss_sample_count", metrics.value("rss_samples")},
            {"window_qobject_descendants_raw",
             static_cast<qint64>(c.window->findChildren<QObject *>().size())}};
}
QImage samples(QImage image);
QImage exportReference(const QString &path, const luma::EditDocument &document);
void noTemporaryFiles(const QString &folder);
void panelStress(Context &c, bool importing, const QString &id) {
    auto &d = *c.driver;
    historySentinel(c);
    const auto setup = snapshot(c);
    // A real setup export both proves useful output and persists a safe owned
    // destination before the stress cycles; never leave a latent export aimed
    // at the user's default Pictures directory.
    const auto expectedOutput = exportReference(setup.source, setup.document);
    const auto output = c.exportPNG("panel-stress-baseline");
    // Worker completion may precede Popup.closed. Late setup signals must not
    // enter stress spies or be interrupted by the first of the 200 reopens.
    d.wait([&] { return !d.object("exportPanel")->property("visible").toBool(); },
           "setup Export exit transition completed before stress observation");
    const auto outputHash = digest(output);
    const auto folder = c.root + "/exports";
    require(samples(QImage(output)) == expectedOutput &&
                QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot) ==
                    QStringList{"panel-stress-baseline.png"},
            "Panel stress setup export lacks exact full-document output pixels");
    naturalSave(c, setup.document);
    metadataReady(c);
    invariant(c, setup, setup.document, true, true, false);
    if (importing)
        d.click("main.libraryButton");
    const auto before = snapshot(c);
    const auto expectedPixels = fixtureReference(c, before.document, before.source);
    auto *panel = d.object(importing ? "library.importDialog" : "exportPanel");
    QSignalSpy opened(panel, SIGNAL(opened()));
    QSignalSpy closed(panel, SIGNAL(closed()));
    QSignalSpy rejected(panel, SIGNAL(rejected()));
    QSignalSpy accepted(panel, SIGNAL(accepted()));
    QSignalSpy closingClick(d.object(importing ? "library.importCancel" : "export.close"),
                            SIGNAL(clicked()));
    require(opened.isValid() && closed.isValid() && rejected.isValid() && accepted.isValid() &&
                closingClick.isValid(),
            "Panel stress lacks observable production popup/button signals");
    const auto sources = qmlValue(d.object("library.importDialog"), "sources");
    const auto destination = qmlValue(d.object("library.importDialog"), "destination");
    const bool recursive = d.object("library.importRecursive")->property("checked").toBool();
    const bool duplicates = d.object("library.importDuplicates")->property("checked").toBool();
    QJsonArray measurements{sample(c, 0)};
    for (int cycle = 1; cycle <= 200; ++cycle) {
        d.click(importing ? "library.importButton" : "exportButton");
        d.wait([&] { return panel->property("opened").toBool(); }, "stress panel opened");
        require(opened.count() == cycle && closed.count() == cycle - 1 && !c.library.importing() &&
                    !c.exporter.working(),
                "One stress opening duplicated a popup or started a job");
        if (!importing)
            require(qmlValue(panel, "form").toMap() == before.exportSettings,
                    "Export reopening lost its saved full settings");
        d.click(importing ? "library.importCancel" : "export.close");
        d.wait([&] { return !panel->property("visible").toBool() && closed.count() == cycle; },
               "stress panel fully closed");
        const QJsonObject checks{
            {"clicked_once", closingClick.count() == cycle},
            {"opened_once", opened.count() == cycle},
            {"accepted_empty", accepted.isEmpty()},
            {"rejected_once", !importing || rejected.count() == cycle},
            {"document", c.editor.currentSettings() == before.document},
            {"undo", c.editor.canUndo() == before.undo},
            {"redo", c.editor.canRedo() == before.redo},
            {"import_idle", !c.library.importing()},
            {"export_idle", !c.exporter.working()},
            {"sources", qmlValue(d.object("library.importDialog"), "sources") == sources},
            {"destination",
             qmlValue(d.object("library.importDialog"), "destination") == destination},
            {"recursive",
             d.object("library.importRecursive")->property("checked").toBool() == recursive},
            {"duplicates",
             d.object("library.importDuplicates")->property("checked").toBool() == duplicates}};
        for (auto check = checks.begin(); check != checks.end(); ++check)
            if (!check.value().toBool()) {
                writeJson(c.root + "/panel-stress-failure.json", {{"scenario", id},
                                                                  {"cycle", cycle},
                                                                  {"checks", checks},
                                                                  {"opened", opened.count()},
                                                                  {"closed", closed.count()},
                                                                  {"clicked", closingClick.count()},
                                                                  {"accepted", accepted.count()},
                                                                  {"rejected", rejected.count()}});
                break;
            }
        require(
            closingClick.count() == cycle && opened.count() == cycle && accepted.isEmpty() &&
                (!importing || rejected.count() == cycle) &&
                c.editor.currentSettings() == before.document &&
                c.editor.canUndo() == before.undo && c.editor.canRedo() == before.redo &&
                !c.library.importing() && !c.exporter.working() &&
                qmlValue(d.object("library.importDialog"), "sources") == sources &&
                qmlValue(d.object("library.importDialog"), "destination") == destination &&
                d.object("library.importRecursive")->property("checked").toBool() == recursive &&
                d.object("library.importDuplicates")->property("checked").toBool() == duplicates,
            "Panel stress lost an actual Cancel/Close or changed form/document/history");
        d.log({{"action", "panel_stress_cycle"},
               {"scenario", id},
               {"cycle", cycle},
               {"opened", opened.count()},
               {"closed", closed.count()},
               {"cancel_or_close_clicked", closingClick.count()}});
        if (cycle % 25 == 0) {
            invariant(c, before, before.document, before.undo, before.redo);
            require(c.provider->requestImage({}, nullptr, {}) == expectedPixels,
                    "Panel cycles changed the committed preview pixels");
            require(digest(output) == outputHash && samples(QImage(output)) == expectedOutput &&
                        QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot) ==
                            QStringList{"panel-stress-baseline.png"},
                    "Panel cycles changed the committed output or silently created another file");
            measurements.append(sample(c, cycle));
        }
    }
    invariant(c, before, before.document, before.undo, before.redo);
    // A leaked modal overlay/grab must not hide behind unchanged data or signal counts.
    if (importing) {
        d.click("main.developButton");
        require(!c.window->property("libraryMode").toBool(), "Import Cancel leaked input grab");
    } else {
        d.click("main.libraryButton");
        require(c.window->property("libraryMode").toBool(), "Export Close leaked input grab");
        d.click("main.developButton");
    }
    pixels(c, expectedPixels);
    proveSentinelHistory(c, before);
    pixels(c, expectedPixels);
    if (importing)
        d.click("main.libraryButton");
    invariant(c, before, before.document, before.undo, before.redo);
    require(digest(output) == outputHash && samples(QImage(output)) == expectedOutput &&
                QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot) ==
                    QStringList{"panel-stress-baseline.png"},
            "Panel stress or history positive control changed the exact baseline output");
    noTemporaryFiles(folder);
    c.checkData();
    writeJson(c.root + "/measurements.json",
              {{"scenario", id},
               {"completed_cycles", 200},
               {"opened_signals", opened.count()},
               {"closed_signals", closed.count()},
               {"cancel_or_close_clicked_signals", closingClick.count()},
               {"setup_export_count", 1},
               {"setup_output_sha256", QString::fromLatin1(outputHash.toHex())},
               {"measurements", measurements},
               {"measurement_mode", "RAW_ONLY"},
               {"object_count_scope", "QObject descendants of the test QQuickWindow only"},
               {"memory_threshold_applied", false},
               {"plateau_claimed", false},
               {"native_file_panel_exercised", false},
               {"verification", "200 real UI cycles; exact documents, durable assets/journal, "
                                "history branches, owned source SHA and Ready preview"}});
}
QImage samples(QImage image) {
    image = image.convertToFormat(QImage::Format_RGBA8888);
    image.setDevicePixelRatio(1);
    image.setColorSpace({});
    return image;
}
QImage exportReference(const QString &path, const luma::EditDocument &document) {
    const auto decoded = luma::PhotoDecoder().decode(path.toStdString());
    require(decoded.image.width <= 512 && decoded.image.height <= 512 &&
                decoded.image.alpha.empty(),
            "Detail export oracle requires small opaque private fixtures");
    const auto pixels =
        luma::renderCPU(decoded.image, document, decoded.metadata.orientation, {}, {},
                        decoded.calibration, luma::DetailDiagnostic::none, 1, decoded.metadata);
    return QImage(pixels.rgba.data(), pixels.width, pixels.height,
                  static_cast<qsizetype>(pixels.width) * 4, QImage::Format_RGBA8888)
        .copy();
}
void noTemporaryFiles(const QString &folder) {
    QDirIterator files(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const auto path = files.next();
        const auto name = QFileInfo(path).fileName();
        require(!name.startsWith(".luma-") && !name.endsWith(".tmp"),
                "Detail export left incomplete output: " + path);
    }
}
void detailStorm(Context &c, const QString &id) {
    auto &d = *c.driver;
    const auto before = snapshot(c);
    require(!before.undo && !before.redo && before.document == luma::EditDocument{},
            "Detail 200-edit fixture requires an empty full history");
    const std::vector<StepControl> controls{
        {"sharpen.detail", &luma::EditDocument::sharpenDetail, 0, 100, 22},
        {"sharpen.masking", &luma::EditDocument::sharpenMasking, 0, 100, 22},
        {"noise.luminance", &luma::EditDocument::noiseLuminance, 0, 100, 22},
        {"noise.luminanceDetail", &luma::EditDocument::noiseLuminanceDetail, 0, 100, 22},
        {"noise.luminanceContrast", &luma::EditDocument::noiseLuminanceContrast, 0, 100, 22},
        {"noise.color", &luma::EditDocument::noiseColor, 0, 100, 22},
        {"noise.colorDetail", &luma::EditDocument::noiseColorDetail, 0, 100, 22},
        {"noise.colorSmoothness", &luma::EditDocument::noiseColorSmoothness, 0, 100, 22},
        {"sharpen.amount", &luma::EditDocument::sharpenAmount, 0, 150, 24}};
    // The entire expected chain is constructed from literal UI steps, never from
    // post-input controller values or EditDocument::setParameter().
    std::vector<luma::EditDocument> expected{before.document};
    for (const auto &control : controls)
        for (int index = 0; index < control.edits; ++index) {
            auto next = expected.back();
            next.*(control.member) += 1;
            require(next.*(control.member) < control.maximum && next != expected.back(),
                    "Detail expected chain contains a clamped/no-op step");
            expected.push_back(next);
        }
    require(expected.size() == 201, "Detail stress must retain exactly 200 distinct UI edits");
    const auto expectedPreview = fixtureReference(c, expected.back(), before.source);
    const auto expectedOutput = exportReference(before.source, expected.back());
    const auto initialOutput = exportReference(before.source, before.document);
    require(!expectedOutput.isNull() && expectedOutput != initialOutput,
            "Detail stress fixture cannot distinguish processed output from the original");
    const auto folder = c.root + "/exports";
    const auto output = folder + "/detail-after-200.png";
    require(QDir().mkpath(folder) &&
                QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty(),
            "Immediate Detail export requires an empty owned destination");
    QJsonArray measurements{sample(c, 0)};
    QSignalSpy currentClicked(d.object("export.current"), SIGNAL(clicked()));
    require(currentClicked.isValid(), "Immediate export button lacks its real clicked signal");
    const ReleaseWrites release{c.storageFault};
    int edits = 0;
    for (const auto &control : controls) {
        focusSlider(c, control);
        for (int index = 0; index < control.edits; ++index) {
            if (edits == 199) {
                naturalSave(c, expected[199]);
                c.storageFault.holdWrites(true);
            }
            auto *slider = d.item(control.parameter + "Slider");
            require(slider->hasActiveFocus() && c.window->activeFocusItem() == slider &&
                        QGuiApplication::focusWindow() == c.window,
                    "Detail key input lost its actual window/Slider keyboard target");
            d.key(Qt::Key_Right);
            ++edits;
            require(c.editor.currentSettings() == expected[static_cast<std::size_t>(edits)] &&
                        c.editor.canUndo() && !c.editor.canRedo() &&
                        !d.object(control.parameter + "Slider")->property("pressed").toBool() &&
                        c.editor.detailDiagnostic().isEmpty() && c.editor.error().isEmpty(),
                    "One Detail keyboard edit changed the wrong full document or gesture history");
            d.log(
                {{"action", "detail_stress_edit"},
                 {"edit", edits},
                 {"parameter", control.parameter},
                 {"expected_value", expected[static_cast<std::size_t>(edits)].*(control.member)}});
            if (edits < 200 && edits % 25 == 0)
                measurements.append(sample(c, edits));
        }
    }
    require(edits == 200 && c.editor.saving(),
            "Last real Detail edit did not leave an actual pending save for immediate export");
    // No saved()/endEdit()/pixels wait between edit 200 and this first Export input.
    // The VFS holds the final natural catalog-associated write; it does not
    // expose whether the fd is the main DB, WAL or journal. This
    // makes the save/queue overlap deterministic; it is not a latency benchmark.
    d.click("exportButton");
    d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
           "immediate Detail Export panel opened");
    d.type("export.destination", folder);
    d.type("exportNaming", "detail-after-200");
    d.choose("exportFormat", 1);
    d.choose("export.conflict", 0);
    d.click("export.current");
    d.wait(
        [&] {
            return c.exporter.working() && c.exporter.waitingForSave() &&
                   c.storageFault.writeBlocked() && c.editor.saving() && c.editor.editingLocked();
        },
        "immediate export actually waits on the held final natural catalog write");
    require(currentClicked.count() == 1 && c.exporter.completed() == 0 &&
                c.exporter.succeeded() == 0 && !QFileInfo::exists(output) &&
                c.editor.currentSettings() == expected.back(),
            "Immediate export raced past pending edits or started more than once");
    d.log({{"action", "detail_immediate_export_wait_boundary"},
           {"completed_ui_edits", 200},
           {"catalog_write_blocked", c.storageFault.writeBlocked()},
           {"editor_saving", c.editor.saving()},
           {"queue_waiting_for_save", c.exporter.waitingForSave()},
           {"actual_export_clicks", currentClicked.count()},
           {"output_exists_before_release", QFileInfo::exists(output)}});
    c.storageFault.holdWrites(false);
    d.wait([&] { return !c.exporter.working(); }, "immediate Detail export completed", 60000);
    d.wait([&] { return !d.object("exportPanel")->property("visible").toBool(); },
           "immediate Detail Export modal exit completed before history inputs");
    require(c.exporter.error().isEmpty() && c.exporter.failures().isEmpty() &&
                c.exporter.total() == 1 && c.exporter.completed() == 1 &&
                c.exporter.succeeded() == 1 && c.exporter.skipped() == 0 &&
                !c.editor.editingLocked(),
            "Immediate Detail export did not complete exactly one immutable queue item");
    const auto settings = c.exporter.settings();
    require(
        settings.value("format").toInt() == 1 && settings.value("colorSpace").toInt() == 0 &&
            settings.value("resize").toInt() == 0 && settings.value("outputMedium").toInt() == 0 &&
            settings.value("watermarkKind").toInt() == 0 && settings.value("metadata").toInt() == 0,
        "Detail PNG pixel oracle disagrees with actual processing options");
    require(QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot) ==
                    QStringList{"detail-after-200.png"} &&
                samples(QImage(output)) == expectedOutput,
            "Immediate export missed the full final Detail document or exact output pixels");
    const auto outputHash = digest(output);
    noTemporaryFiles(folder);
    naturalSave(c, expected.back());
    metadataReady(c);
    pixels(c, expectedPreview);
    invariant(c, before, expected.back(), true, false, false);
    measurements.append(sample(c, 200));
    // With a clean initial history and the documented capacity of 200, every
    // real keyboard edit must be independently undoable and redoable exactly once.
    for (int current = 200; current > 0; --current) {
        require(c.editor.canUndo() && d.item("main.undoButton")->isEnabled(),
                "Detail stress retained fewer than 200 real Undo entries");
        d.click("main.undoButton");
        require(c.editor.currentSettings() == expected[static_cast<std::size_t>(current - 1)],
                "Detail Undo diverged from an independent full-document snapshot");
    }
    require(!c.editor.canUndo() && c.editor.canRedo(), "Detail Undo crossed its initial boundary");
    naturalSave(c, before.document);
    pixels(c, fixtureReference(c, before.document, before.source));
    invariant(c, before, before.document, false, true, false);
    for (int next = 1; next <= 200; ++next) {
        require(c.editor.canRedo() && d.item("main.redoButton")->isEnabled(),
                "Detail stress retained fewer than 200 real Redo entries");
        d.click("main.redoButton");
        require(c.editor.currentSettings() == expected[static_cast<std::size_t>(next)],
                "Detail Redo diverged from an independent full-document snapshot");
    }
    require(c.editor.canUndo() && !c.editor.canRedo(), "Detail Redo left an unexpected branch");
    naturalSave(c, expected.back());
    pixels(c, expectedPreview);
    invariant(c, before, expected.back(), true, false, false);
    require(digest(output) == outputHash && samples(QImage(output)) == expectedOutput,
            "Undo/Redo changed the previously committed export output");
    noTemporaryFiles(folder);
    c.checkData();
    writeJson(c.root + "/measurements.json",
              {{"scenario", id},
               {"native_ui_edits", 200},
               {"undo_steps", 200},
               {"redo_steps", 200},
               {"controls", 9},
               {"keyboard_step", 1},
               {"history_capacity_under_test", 200},
               {"immediate_export_count", 1},
               {"immediate_boundary",
                "UI edit 200 then Export; final natural catalog-associated write held"},
               {"storage_fixture", "existing test-only SqliteFault; no production latency claim"},
               {"output_sha256", QString::fromLatin1(outputHash.toHex())},
               {"pixel_oracle", "independent expected full document -> CPU reference; exact RGBA8"},
               {"renderer_numerical_correctness_claimed", false},
               {"measurements", measurements},
               {"measurement_mode", "RAW_ONLY"},
               {"memory_threshold_applied", false},
               {"plateau_claimed", false}});
}
} // namespace
bool panelDetailStressScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-PERF-017", "UX-PERF-018", "UX-DTL-011"};
    if (!supported.contains(id))
        return false;
    const KeyboardNavigation navigation;
    ownImport(c);
    if (id == "UX-DTL-011")
        detailStorm(c, id);
    else
        panelStress(c, id == "UX-PERF-018", id);
    return true;
}
} // namespace ux
