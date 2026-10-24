#include "OwnedVolume.h"
#include "apps/shared/PhotoDecoder.h"
#include "apps/shared/PresetStore.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QSignalSpy>
#include <QUrl>
#include <QUuid>
#include <cstdio>
#include <exception>
#include <memory>
#include <sqlite3.h>
#include <stdexcept>
#include <tuple>
#include <vector>

// Real owned-filesystem disappearance/recovery. No physical USB claim, direct
// controller open/reload, fake mount events or synthetic missing-file errors.
namespace ux {
namespace {
QString hash(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read owned source for SHA256: " + path);
    QCryptographicHash value(QCryptographicHash::Sha256);
    require(value.addData(&file), "Cannot hash complete owned source: " + path);
    return QString::fromLatin1(value.result().toHex());
}
bool sameMetadata(const luma::PhotoMetadata &a, const luma::PhotoMetadata &b) {
    return std::tie(a.camera, a.lens, a.iso, a.shutterSeconds, a.aperture, a.focalLength,
                    a.captureTimestamp, a.width, a.height, a.orientation) ==
           std::tie(b.camera, b.lens, b.iso, b.shutterSeconds, b.aperture, b.focalLength,
                    b.captureTimestamp, b.width, b.height, b.orientation);
}
QJsonObject uiMetadata(Context &c) {
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
QJsonObject windowState(Context &c) {
    return {{"visible", c.window->isVisible()},
            {"exposed", c.window->isExposed()},
            {"active", c.window->isActive()},
            {"visibility", static_cast<int>(c.window->visibility())},
            {"window_state", static_cast<int>(c.window->windowState())},
            {"app_state", static_cast<int>(QGuiApplication::applicationState())},
            {"closing_down", QCoreApplication::closingDown()}};
}
QJsonObject completeDocument(const luma::EditDocument &value) {
    const auto encoded = QJsonDocument::fromJson(
        luma::encodePreset({"Removable source full document", {}, value, luma::allSettingsGroups}));
    require(encoded.isObject(), "Cannot retain complete removable-source document evidence");
    return encoded.object();
}
luma::EditDocument landscape() {
    luma::EditDocument value;
    value.curves[0].points = {{0, 0}, {.2, .17}, {.5, .52}, {.8, .84}, {1, 1}};
    value.hsl = {{{0, 4, 0}, {0, 4, 0}, {-3, 8, 2}, {-4, 12, 0}, {0, 8, -2}, {-2, 12, -4}, {}, {}}};
    return value;
}
QJsonArray query(sqlite3 *database, const char *sql) {
    sqlite3_stmt *raw = nullptr;
    const int prepared = sqlite3_prepare_v2(database, sql, -1, &raw, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw,
                                                                               sqlite3_finalize);
    require(prepared == SQLITE_OK, "Cannot prepare read-only removable-source journal audit");
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
                require(false, "Unexpected BLOB in actual production import journal");
        }
        result.append(row);
    }
    require(status == SQLITE_DONE, "Read-only import journal audit was incomplete");
    return result;
}
QJsonObject journal(Context &c) {
    sqlite3 *raw = nullptr;
    const int opened = sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &raw,
                                       SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
    require(opened == SQLITE_OK && sqlite3_busy_timeout(raw, 1000) == SQLITE_OK &&
                sqlite3_exec(raw, "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK,
            "Cannot consistently read the actual local catalog journal");
    const auto sessions = query(raw, "SELECT * FROM import_sessions ORDER BY id");
    const auto items = query(raw, "SELECT * FROM import_items ORDER BY session_id,position");
    require(sqlite3_exec(raw, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK,
            "Cannot finish consistent read-only journal audit");
    return {{"sessions", sessions}, {"items", items}};
}
luma::AssetSnapshot snapshot(Context &c, const QString &id) {
    const auto value = luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(id.toStdString());
    if (!value)
        throw std::runtime_error("Pinned removable-source UUID disappeared from local catalog");
    return *value;
}
void ready(Context &c, const QImage &expected, const QString &reason) {
    auto condition = [&] {
        auto *preview = c.driver->item("develop.preview");
        return !c.editor.busy() && c.editor.hasPhoto() && preview->isVisible() &&
               preview->property("status").toInt() == 1 && !c.editor.previewUrl().isEmpty() &&
               preview->property("source").toUrl().toString() == c.editor.previewUrl() &&
               c.provider->requestImage({}, nullptr, {}) == expected;
    };
    try {
        c.driver->wait(condition, reason);
    } catch (...) {
        recordPreviewDifference(c, expected, reason);
        throw;
    }
    c.driver->frame();
    require(condition(), "Preview changed after its accepted removable-source boundary");
}
void savedNaturally(Context &c, const QString &id, const luma::EditDocument &expected) {
    c.driver->wait(
        [&] {
            const auto value =
                luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(id.toStdString());
            return !c.editor.saving() && !c.editor.busy() && c.editor.error().isEmpty() && value &&
                   value->asset.edits == expected && c.editor.currentSettings() == expected;
        },
        "real preset full document saved without explicit controller endEdit");
}
int modelRow(Context &c, const QString &id) {
    for (int row = 0; row < c.library.count(); ++row)
        if (c.library.data(c.library.index(row), LibraryModel::AssetID).toString() == id)
            return row;
    return -1;
}
void modelIdentity(Context &c, const luma::AssetSnapshot &a, const luma::AssetSnapshot &b,
                   const QString &active) {
    c.driver->wait(
        [&] {
            if (c.library.loading() || c.library.working() || c.library.count() != 2)
                return false;
            for (const auto *value : {&a, &b}) {
                const auto id = QString::fromStdString(value->asset.id);
                const int row = modelRow(c, id);
                if (row < 0)
                    return false;
                const auto index = c.library.index(row);
                if (c.library.data(index, LibraryModel::Path).toUrl().toLocalFile() !=
                        QString::fromStdString(value->asset.path.string()) ||
                    c.library.data(index, LibraryModel::Filename).toString() !=
                        QString::fromStdString(value->asset.path.filename().string()) ||
                    c.library.data(index, LibraryModel::Camera).toString() !=
                        QString::fromStdString(value->asset.metadata.camera) ||
                    c.library.data(index, LibraryModel::Active).toBool() != (id == active))
                    return false;
            }
            return true;
        },
        "exact Library UUID/path/filename/metadata/active identity");
}
QMap<QString, QString> cacheBaseline(Context &c, const QString &a, const QString &b) {
    try {
        c.driver->wait(
            [&] {
                for (const auto &id : {a, b}) {
                    auto *card = c.driver->find("filmstrip.card." + id);
                    const int row = modelRow(c, id);
                    auto *thumbnail =
                        card ? card->findChild<QQuickItem *>("photoThumbnail") : nullptr;
                    if (row < 0 || !thumbnail || thumbnail->property("status").toInt() != 1)
                        return false;
                    const auto base = card->property("thumbnailURL").toString();
                    const int retry = card->property("retryCount").toInt();
                    const auto source =
                        base + (retry ? "&retry=" + QString::number(retry) : QString{});
                    // Only edited B receives a targeted Thumbnail notification.
                    // Unchanged A retains its previously notified URL; the global
                    // generation is not A's saved document revision.
                    if (!base.startsWith("image://thumbnail/" + id + "?generation=") || retry < 0 ||
                        retry > 2 || thumbnail->property("source").toUrl().toString() != source ||
                        (id == b &&
                         base != c.library.data(c.library.index(row), LibraryModel::Thumbnail)
                                     .toString()))
                        return false;
                }
                return true;
            },
            "both actual bound thumbnail sources became Ready");
    } catch (...) {
        QJsonArray states;
        for (const auto &id : {a, b}) {
            auto *card = c.driver->find("filmstrip.card." + id);
            const int row = modelRow(c, id);
            auto *image = card ? card->findChild<QQuickItem *>("photoThumbnail") : nullptr;
            states.append(QJsonObject{
                {"asset", id},
                {"row", row},
                {"card_present", card != nullptr},
                {"card_url", card ? card->property("thumbnailURL").toString() : QString{}},
                {"retry", card ? card->property("retryCount").toInt() : -1},
                {"image_status", image ? image->property("status").toInt() : -1},
                {"image_source", image ? image->property("source").toUrl().toString() : QString{}},
                {"model_url",
                 row < 0
                     ? QString{}
                     : c.library.data(c.library.index(row), LibraryModel::Thumbnail).toString()}});
        }
        writeJson(c.root + "/thumbnail-boundary.json", {{"states", states}});
        throw;
    }
    for (const auto &id : {a, b})
        thumbnailCardPixels(c, c.driver->item("filmstrip.card." + id), snapshot(c, id));
    const auto directory = c.catalogFile + ".luma-cache/thumbnails";
    require(QFileInfo(directory).canonicalFilePath().startsWith(
                QFileInfo(c.root).canonicalFilePath() + '/'),
            "Thumbnail baseline escaped the private host catalog root");
    QMap<QString, QString> result;
    for (const auto &name : QDir(directory).entryList({"*.jpg"}, QDir::Files)) {
        const auto path = directory + '/' + name;
        require(!QFileInfo(path).isSymLink() && !QImageReader(path, "JPEG").read().isNull(),
                "Retained host thumbnail cache is not a complete valid JPEG");
        result.insert(path, hash(path));
    }
    require(result.size() >= 2, "Ready two-asset thumbnail fixture has no retained cache baseline");
    return result;
}
void unchangedCache(const QMap<QString, QString> &baseline) {
    // New legitimate cache revisions may appear. Only the known valid baseline
    // files are frozen; last-access timestamps are deliberately not an oracle.
    for (auto it = baseline.cbegin(); it != baseline.cend(); ++it)
        require(QFileInfo(it.key()).isFile() && !QFileInfo(it.key()).isSymLink() &&
                    hash(it.key()) == it.value(),
                "Source detach/recovery damaged retained host thumbnail cache bytes");
}
void unchanged(Context &c, const luma::AssetSnapshot &a, const luma::AssetSnapshot &b,
               const QJsonObject &baselineJournal) {
    const luma::Catalog catalog(c.catalogFile.toStdString());
    require(catalog.libraryCount() == 2 && catalog.unfinishedImports().empty() &&
                journal(c) == baselineJournal,
            "Mount loss/recovery changed membership or the complete terminal Add journal");
    for (const auto *before : {&a, &b}) {
        const auto after = snapshot(c, QString::fromStdString(before->asset.id));
        require(after.asset.id == before->asset.id && after.asset.path == before->asset.path &&
                    after.asset.checksum == before->asset.checksum &&
                    sameMetadata(after.asset.metadata, before->asset.metadata) &&
                    after.asset.edits == before->asset.edits && after.revision == before->revision,
                "Mount loss/recovery damaged a full saved document/revision/source/metadata/UUID");
    }
}
} // namespace

bool removableSourceScenario(Context &c, const QString &id) {
    if (id != "UX-MAC-007")
        return false;
    auto &d = *c.driver;
    require(c.library.count() == 0 && !c.editor.hasPhoto() &&
                luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 0 &&
                QFileInfo(c.catalogFile)
                    .canonicalFilePath()
                    .startsWith(QFileInfo(c.root).canonicalFilePath() + '/'),
            "Owned-volume source candidate requires a fresh isolated catalog");
    const auto originalA = c.fixture + "/Photo-0000.png";
    const auto originalB = c.fixture + "/Photo-0001.png";
    const auto expectedHashA = hash(originalA), expectedHashB = hash(originalB);
    require(expectedHashA != expectedHashB,
            "Generated control/source fixtures are indistinguishable");
    const auto local = c.root + "/Control-A.png";
    require(QFile::copy(originalA, local) && !QFileInfo(local).isSymLink() &&
                QFileInfo(local).canonicalFilePath().startsWith(
                    QFileInfo(c.root).canonicalFilePath() + '/') &&
                hash(local) == expectedHashA,
            "Cannot exclusively create the private host control original");
    const auto expectedMetadataA = luma::PhotoDecoder().inspect(originalA.toStdString());
    const auto expectedMetadataB = luma::PhotoDecoder().inspect(originalB.toStdString());
    const luma::EditDocument defaultDocument;
    const auto editedB = landscape();
    QString phase = "before_volume_create";
    OwnedVolume volume(d, c.root); // Lifetime encloses catch; parent owns native-screen flock.
    try {
        volume.checkpoint("before_fixture_attach", {{"ui_window", windowState(c)}});
        volume.createAndAttach();
        volume.checkpoint("after_fixture_attach", {{"ui_window", windowState(c)}});
        phase = "own_volume_created";
        const auto external = volume.mountPath() + "/Source-B.png";
        require(QFile::copy(originalB, external) && !QFileInfo(external).isSymLink() &&
                    QFileInfo(external).canonicalFilePath() == external &&
                    hash(external) == expectedHashB,
                "Cannot create the exclusively owned external generated original");
        volume.pinOriginals({{"Source-B.png", expectedHashB}});
        const auto volumeID = volume.volumeUUID();
        const auto mountPath = volume.mountPath();
        require(!volumeID.isEmpty(), "Own mounted filesystem has no accepted VolumeUUID");

        phase = "actual_add_import";
        require(d.object("library.importDialog")
                    ->setProperty("sources",
                                  QVariant::fromValue(QList<QUrl>{QUrl::fromLocalFile(local),
                                                                  QUrl::fromLocalFile(external)})),
                "Actual Import pane rejected the known file-panel selection result");
        d.click("library.importButton");
        d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
               "actual Add import dialog opened");
        d.choose("library.importMode", 0);
        if (d.object("library.importDuplicates")->property("checked").toBool())
            d.click("library.importDuplicates");
        d.click("library.importStart");
        d.wait(
            [&] {
                return !c.library.importing() && !c.library.working() && !c.library.loading() &&
                       c.library.count() == 2;
            },
            "both actual Add originals committed and model loaded", 60000);
        d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
               "actual Add import modal fully closed");
        require(c.library.error().isEmpty() && c.library.importReport().isEmpty(),
                "Actual Add setup produced import errors");
        QMap<QString, luma::AssetSnapshot> byPath;
        for (const auto &assetID : luma::Catalog(c.catalogFile.toStdString()).libraryIDs({})) {
            const auto value = snapshot(c, QString::fromStdString(assetID));
            require(!QUuid(QString::fromStdString(assetID)).isNull() &&
                        value.asset.edits == defaultDocument,
                    "Actual Add fixture has invalid identity or nondefault full document");
            byPath.insert(QString::fromStdString(value.asset.path.string()), value);
        }
        require(byPath.size() == 2 && byPath.contains(local) && byPath.contains(external),
                "Add did not preserve the exact host/mounted source paths");
        auto a = byPath[local], b = byPath[external];
        const auto aID = QString::fromStdString(a.asset.id),
                   bID = QString::fromStdString(b.asset.id);
        require(aID != bID && a.asset.checksum == expectedHashA.toStdString() &&
                    b.asset.checksum == expectedHashB.toStdString() &&
                    sameMetadata(a.asset.metadata, expectedMetadataA) &&
                    sameMetadata(b.asset.metadata, expectedMetadataB),
                "Add crossed source SHA/metadata/UUID identities");
        const auto baselineJournal = journal(c);
        const auto sessions = baselineJournal["sessions"].toArray();
        const auto items = baselineJournal["items"].toArray();
        require(sessions.size() == 1 && items.size() == 2 &&
                    sessions[0].toObject()["mode"].toString() == "add" &&
                    sessions[0].toObject()["state"].toString() == "completed" &&
                    !sessions[0].toObject()["finished_at"].isNull() &&
                    !QUuid(sessions[0].toObject()["id"].toString()).isNull(),
                "Actual two-file Add has no complete durable production journal");
        QSet<QString> journalPaths, journalIDs;
        QSet<int> positions;
        for (const auto &entry : items) {
            const auto item = entry.toObject();
            const auto source = item["source_path"].toString();
            require(byPath.contains(source), "Add journal contains an unexpected source");
            const auto &known = byPath[source];
            const auto assetID = QString::fromStdString(known.asset.id);
            const int position = item["position"].toInt(-1);
            require(item["state"].toString() == "committed" &&
                        item["session_id"] == sessions[0].toObject()["id"] &&
                        item["asset_id"].toString() == assetID &&
                        item["target_path"].toString() == source &&
                        item["temporary_path"].toString().isEmpty() &&
                        item["checksum"].toString() ==
                            QString::fromStdString(known.asset.checksum) &&
                        item["filesize"].toInteger(-1) == QFileInfo(source).size() &&
                        item["error"].toString().isEmpty() && position >= 0 && position < 2 &&
                        !positions.contains(position) && !journalPaths.contains(source) &&
                        !journalIDs.contains(assetID),
                    "Actual Add journal is partial/duplicate or crosses UUID/source/SHA/size");
            positions.insert(position);
            journalPaths.insert(source);
            journalIDs.insert(assetID);
        }

        QSignalSpy opens(&c.library, &LibraryModel::openRequested);
        QSignalSpy loads(&c.editor, &EditorController::photoLoaded);
        require(opens.isValid() && loads.isValid(),
                "Cannot observe actual card activation routing");
        d.wait([&] { return d.find("library.card." + bID) != nullptr; }, "actual external B card");
        d.doubleClick("library.card." + bID);
        c.loaded();
        require(opens.count() == 1 && loads.count() == 1 &&
                    opens[0][0].toUrl().toLocalFile() == external && c.editor.photoID() == bID &&
                    c.editor.sourcePath() == external &&
                    c.editor.currentSettings() == defaultDocument,
                "Actual external B card did not open its own full source document");
        d.click("filter_landscape");
        d.move("main.filename", {.5, .5});
        require(c.editor.currentSettings() == editedB && !c.editor.presetPreview(),
                "Actual Landscape input missed its independent complete document");
        savedNaturally(c, bID, editedB);
        b = snapshot(c, bID);
        a = snapshot(c, aID);
        require(a.asset.edits == defaultDocument && b.asset.edits == editedB &&
                    c.editor.canUndo() && !c.editor.canRedo(),
                "Real preset setup damaged the control or did not establish its expected history");
        const auto pixelsA = fixtureReference(c, defaultDocument, originalA);
        const auto pixelsB = fixtureReference(c, editedB, originalB);
        require(pixelsA != pixelsB && pixelsB != fixtureReference(c, defaultDocument, originalB),
                "Generated source/control/document fixtures cannot detect crossed/stale pixels");
        ready(c, pixelsB, "external B has exact saved-document current-source pixels");
        modelIdentity(c, a, b, bID);
        const auto retainedCache = cacheBaseline(c, aID, bID);
        QJsonObject cacheHashes;
        for (auto it = retainedCache.cbegin(); it != retainedCache.cend(); ++it)
            cacheHashes[it.key()] = it.value();
        const auto beforeDetachMetadata = uiMetadata(c);
        const auto beforeDetachSelection = c.library.selectionIDs();
        unchanged(c, a, b, baselineJournal);
        volume.checkpoint("add_and_saved_document_baseline",
                          {{"a_uuid", aID},
                           {"a_source", local},
                           {"a_sha256", expectedHashA},
                           {"a_document", completeDocument(defaultDocument)},
                           {"b_uuid", bID},
                           {"b_source", external},
                           {"b_sha256", expectedHashB},
                           {"b_document", completeDocument(editedB)},
                           {"b_ui_metadata", beforeDetachMetadata},
                           {"production_import_journal", baselineJournal},
                           {"host_cache_sha256", cacheHashes}});

        phase = "actual_source_detach";
        volume.detach();
        require(volume.detached() && !QFileInfo::exists(external),
                "Source fixture did not lose its actual mounted pathname");
        require(c.editor.photoID() == bID && c.editor.sourcePath() == external &&
                    c.editor.currentSettings() == editedB &&
                    uiMetadata(c) == beforeDetachMetadata && c.editor.canUndo() &&
                    !c.editor.canRedo() && c.library.selectionIDs() == beforeDetachSelection,
                "Actual source detach damaged the retained decoded B document or metadata");
        ready(c, pixelsB, "retained decoded B still has coherent pixels after real source detach");
        unchanged(c, a, b, baselineJournal);
        unchangedCache(retainedCache);

        phase = "actual_host_control_card";
        d.wait([&] { return d.find("filmstrip.card." + aID) != nullptr; }, "actual local A card");
        d.click("filmstrip.card." + aID);
        c.loaded();
        require(opens.count() == 2 && loads.count() == 2 &&
                    opens[1][0].toUrl().toLocalFile() == local && c.editor.photoID() == aID &&
                    c.editor.sourcePath() == local &&
                    c.editor.currentSettings() == defaultDocument && c.editor.error().isEmpty(),
                "Actual A activation did not replace the retained decoded B session");
        ready(c, pixelsA, "local A positive control after mounted source loss");
        modelIdentity(c, a, b, aID);
        const auto aMetadata = uiMetadata(c);
        const bool aUndo = c.editor.canUndo(), aRedo = c.editor.canRedo();

        phase = "actual_offline_source_reread";
        d.wait([&] { return d.find("filmstrip.card." + bID) != nullptr; }, "actual offline B card");
        d.click("filmstrip.card." + bID);
        d.wait([&] { return !c.editor.busy() && !c.editor.error().isEmpty(); },
               "actual offline B activation reports its new source read failure");
        const auto reported = c.editor.error();
        require(opens.count() == 3 && opens[2][0].toUrl().toLocalFile() == external &&
                    loads.count() == 2 && reported.contains(external) &&
                    d.item("main.editorError")->isVisible() &&
                    d.object("main.editorError")->property("text").toString() == reported &&
                    c.editor.photoID() == aID && c.editor.sourcePath() == local &&
                    c.editor.currentSettings() == defaultDocument && uiMetadata(c) == aMetadata &&
                    c.editor.canUndo() == aUndo && c.editor.canRedo() == aRedo &&
                    c.library.selectionIDs() == std::vector<std::string>{b.asset.id},
                "Offline B read did not surface its exact error or crossed retained A identity");
        ready(c, pixelsA, "failed B reread retains coherent A preview, never B metadata/A pixels");
        modelIdentity(c, a, b, aID);
        const auto activeA = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
        require(activeA && activeA->id == a.asset.id && activeA->edits == defaultDocument,
                "Failed offline activation changed durable active A to unavailable B");
        unchanged(c, a, b, baselineJournal);
        unchangedCache(retainedCache);
        volume.checkpoint(
            "actual_offline_reread_error_before_remount",
            {{"error", reported},
             {"retained_active_metadata", aMetadata},
             {"open_requests", opens.count()},
             {"successful_loads", loads.count()},
             {"retained_a_full_document_match", c.editor.currentSettings() == defaultDocument},
             {"saved_b_full_document_unchanged", snapshot(c, bID).asset.edits == editedB}});
        d.screenshot(c.root + "/offline-source-error.png");

        phase = "same_image_path_uuid_remount";
        volume.remount();
        require(volume.attached() && volume.volumeUUID() == volumeID &&
                    volume.mountPath() == mountPath && hash(external) == expectedHashB,
                "Source recovery substituted a new image/path/UUID or changed original bytes");
        volume.verifyOriginals();
        unchanged(c, a, b, baselineJournal);
        phase = "actual_recovered_source_card";
        d.click("filmstrip.card." + bID);
        c.loaded();
        require(opens.count() == 4 && opens[3][0].toUrl().toLocalFile() == external &&
                    loads.count() == 3 && c.editor.photoID() == bID &&
                    c.editor.sourcePath() == external && c.editor.currentSettings() == editedB &&
                    uiMetadata(c) == beforeDetachMetadata && c.editor.error().isEmpty() &&
                    !d.item("main.editorError")->isVisible() && !c.editor.canUndo() &&
                    !c.editor.canRedo() &&
                    c.library.selectionIDs() == std::vector<std::string>{b.asset.id},
                "Actual recovered B card lost its UUID/full document, error clearing or identity");
        ready(c, pixelsB, "same B UUID/source/saved document recovers exact current-source pixels");
        modelIdentity(c, a, b, bID);
        const auto activeB = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
        require(activeB && activeB->id == b.asset.id && activeB->edits == editedB,
                "Recovered B was not the same durable active full document");
        unchanged(c, a, b, baselineJournal);
        unchangedCache(retainedCache);
        require(hash(local) == expectedHashA && hash(originalA) == expectedHashA &&
                    hash(originalB) == expectedHashB && hash(external) == expectedHashB,
                "Removal/recovery changed an own or shared generated original");
        volume.checkpoint("actual_source_recovered_before_final_detach",
                          {{"same_volume_uuid", volumeID},
                           {"b_uuid", bID},
                           {"exact_full_document_and_pixels", true},
                           {"complete_journal_unchanged", true}});
        phase = "checked_finalize";
        volume.finalize();
        require(volume.detached(), "Successful source candidate requires checked final detach");
        unchanged(c, a, b, baselineJournal);
        unchangedCache(retainedCache);
        c.checkData();
        writeJson(c.root + "/measurements.json",
                  {{"scenario", id},
                   {"hardware_input_exercised", false},
                   {"boundary", "OWNED_DISK_IMAGE_VFS_DETACH_REMOUNT"},
                   {"physical_usb_exercised", false},
                   {"input", "QT_QTEST_WINDOW_INPUT"},
                   {"assets", 2},
                   {"actual_open_requests", opens.count()},
                   {"actual_successful_loads", loads.count()},
                   {"actual_missing_source_error", reported},
                   {"full_documents_revisions_metadata_journal_unchanged", true},
                   {"retained_host_cache_files", retainedCache.size()},
                   {"host_cache_sha_unchanged", true},
                   {"same_volume_uuid", volumeID},
                   {"checked_finalize_detached", true},
                   {"volume_evidence", volume.evidenceRoot()},
                   {"renderer_mathematical_correctness_claimed", false}});
        return true;
    } catch (...) {
        const auto failure = std::current_exception();
        try {
            volume.checkpoint(
                "mac007_failure_before_any_volume_cleanup",
                {{"phase", phase},
                 {"ui_window", windowState(c)},
                 {"active_ui_metadata", uiMetadata(c)},
                 {"editor_error", c.editor.error()},
                 {"library_error", c.library.error()},
                 {"library_count", c.library.count()},
                 {"editor_busy", c.editor.busy()},
                 {"editor_saving", c.editor.saving()},
                 {"can_undo", c.editor.canUndo()},
                 {"can_redo", c.editor.canRedo()},
                 {"full_document_matches_known_default",
                  c.editor.currentSettings() == defaultDocument},
                 {"full_document_matches_known_landscape", c.editor.currentSettings() == editedB}});
        } catch (...) {
            std::fputs(
                "MAC007 failure evidence could not be persisted; original failure retained.\n",
                stderr);
        }
        std::rethrow_exception(failure);
    }
}
} // namespace ux
