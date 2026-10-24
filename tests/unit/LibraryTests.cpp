#include "apps/desktop/LibraryModel.h"
#include "apps/shared/MetadataService.h"
#include "core/catalog/Identity.h"
#include "core/raw/RawDecoder.h"
#include "platform/macos/FileHasher.h"
#include "tests/unit/LegacyCatalogFixture.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <fstream>
#include <future>
#include <iostream>
#include <set>
#include <sqlite3.h>
#include <unistd.h>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void throws(F work) {
    bool caught = false;
    try {
        work();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Expected exception.");
}
template <class F> void wait(F predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        QCoreApplication::processEvents();
        if (timer.elapsed() > 10000)
            throw std::runtime_error("Library model timed out.");
        QThread::msleep(1);
    }
    QCoreApplication::processEvents();
}
void sql(const std::filesystem::path &path, const char *statement) {
    sqlite3 *db = nullptr;
    require(sqlite3_open(path.c_str(), &db) == SQLITE_OK, "Fixture DB open failed.");
    char *error = nullptr;
    const auto result = sqlite3_exec(db, statement, nullptr, nullptr, &error);
    std::string message = error ? error : "";
    sqlite3_free(error);
    sqlite3_close(db);
    if (result != SQLITE_OK)
        throw std::runtime_error(message);
}
void catalogTests(const std::filesystem::path &root, const std::filesystem::path &source) {
    const auto path = root / "catalog.sqlite";
    std::promise<void> start;
    auto ready = start.get_future().share();
    auto open = [&] {
        ready.wait();
        luma::Catalog db(path);
        db.verify();
    };
    auto first = std::async(std::launch::async, open),
         second = std::async(std::launch::async, open);
    start.set_value();
    first.get();
    second.get();
    luma::Catalog db(path);
    const auto metadata = luma::RawDecoder().inspect(source);
    const auto asset = db.addAndSelect(source, luma::macos::sha256File(source), metadata);
    db.saveExposure(asset.id, 1.25);
    db.setRating({asset.id}, 4);
    db.setFlag({asset.id}, 1);
    db.setLabel({asset.id}, "blue");
    {
        sqlite3 *connection = nullptr;
        require(sqlite3_open(path.c_str(), &connection) == SQLITE_OK, "Writer fixture failed.");
        const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> writer(connection, &sqlite3_close);
        require(sqlite3_exec(writer.get(), "BEGIN IMMEDIATE; UPDATE assets SET rating=rating;",
                             nullptr, nullptr, nullptr) == SQLITE_OK,
                "Writer lock failed.");
        luma::Catalog reader(path);
        require(reader.libraryCount() == 1, "WAL reader was blocked by active writer.");
        require(sqlite3_exec(writer.get(), "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK,
                "Writer fixture rollback failed.");
    }
    throws([&] { db.setRating({asset.id, "missing"}, 1); });
    require(db.libraryPage({}, 0, 128).front().rating == 4, "Bulk rating partially committed.");
    throws([&] { db.setFlag({asset.id, "missing"}, -1); });
    throws([&] { db.setLabel({asset.id, "missing"}, "red"); });
    const auto collection = db.createCollection("Отбор");
    db.collectionMembership(collection, {asset.id}, true);
    db.collectionMembership(collection, {asset.id}, true);
    require(db.collections().front().count == 1, "Collection membership was duplicated.");
    throws([&] { db.collectionMembership(collection, {asset.id, "missing"}, false); });
    luma::LibraryFilter filter;
    filter.collectionID = collection;
    filter.minimumRating = 4;
    filter.flag = 1;
    filter.label = "blue";
    require(db.libraryCount(filter) == 1, "Combined metadata filter failed.");
    filter.search = "%";
    require(db.libraryCount(filter) == 0, "Search wildcards were not escaped.");
    db.renameCollection(collection, "Избранное");
    db.deleteCollection(collection);
    require(db.libraryCount() == 1, "Deleting collection deleted original.");
    require(db.activeAsset()->edits.exposure == 1.25, "Library mutation changed Develop edits.");
    sql(path, fixture::removePostV11Tables);
    sql(path,
        R"sql(PRAGMA foreign_keys=OFF;
            CREATE TABLE historical_edits(
                asset_id TEXT PRIMARY KEY REFERENCES assets(id) ON DELETE CASCADE,
                schema_version INTEGER NOT NULL DEFAULT 2 CHECK(schema_version=2),
                exposure REAL NOT NULL DEFAULT 0,temperature REAL NOT NULL DEFAULT 6500,
                tint REAL NOT NULL DEFAULT 0,custom_wb INTEGER NOT NULL DEFAULT 0,
                contrast REAL NOT NULL DEFAULT 0,highlights REAL NOT NULL DEFAULT 0,
                shadows REAL NOT NULL DEFAULT 0,whites REAL NOT NULL DEFAULT 0,
                blacks REAL NOT NULL DEFAULT 0,vibrance REAL NOT NULL DEFAULT 0,saturation REAL NOT NULL DEFAULT 0,
                enabled INTEGER NOT NULL DEFAULT 511 CHECK(enabled BETWEEN 0 AND 511),
                revision INTEGER NOT NULL DEFAULT 0,updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
            INSERT INTO historical_edits(asset_id,exposure,temperature,tint,custom_wb,contrast,highlights,shadows,whites,blacks,vibrance,saturation,enabled,revision,updated_at)
                SELECT asset_id,exposure,temperature,tint,custom_wb,contrast,highlights,shadows,whites,blacks,vibrance,saturation,enabled&511,revision,updated_at FROM edit_documents;
            DROP TRIGGER initialize_local_masks; DROP TABLE edit_local_masks;
            DROP TABLE edit_documents; ALTER TABLE historical_edits RENAME TO edit_documents;
DROP TABLE edit_curve_points;DROP TABLE edit_hsl_bands;DROP TABLE collection_assets;DROP TABLE collections;DROP TABLE import_items;DROP TABLE import_sessions;DROP INDEX assets_filename;DROP INDEX assets_flag;DELETE FROM schema_migrations WHERE version>=3;PRAGMA user_version=2;)sql");
    luma::Catalog migrated(path);
    require(migrated.activeAsset()->edits.exposure == 1.25, "Schema 2 migration lost edits.");
    require(migrated.libraryPage({}, 0, 1).front().rating == 4, "Migration lost ratings.");
}
void importTests(const std::filesystem::path &root, const std::filesystem::path &source) {
    const auto input = root / "input", nested = input / "nested", destination = root / "copies";
    std::filesystem::create_directories(nested);
    std::filesystem::create_directories(destination);
    const auto a = input / "a.DNG", b = nested / "b.dng", duplicate = input / "same.DNG",
               bad = input / "broken.dng";
    std::filesystem::copy_file(source, a);
    std::filesystem::copy_file(source, b);
    std::filesystem::copy_file(source, duplicate);
    {
        std::fstream file(b, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(-2, std::ios::end);
        file.put('\0');
        file.put('\0');
    }
    {
        std::ofstream file(bad);
        file << "Not RAW";
    }
    const auto hash = luma::macos::sha256File(a);
    luma::LibraryService service(root / "add.sqlite");
    auto result = service.import({input}, {});
    require(result.added == 2 && result.duplicates == 1 && result.failed == 1,
            "Recursive Add summary failed.");
    result = service.import({input}, {});
    require(result.added == 0 && result.duplicates == 3 && result.failed == 1,
            "Duplicate reimport changed catalog.");
    luma::ImportOptions copy;
    copy.mode = luma::ImportMode::copy;
    copy.destination = destination;
    luma::CancellationSource cancellation;
    result = luma::LibraryService(root / "cancel.sqlite")
                 .import({a, b}, copy, cancellation.token(),
                         [&](double, const auto &) { cancellation.cancel(); });
    require(result.added == 1 && result.cancelled == 1,
            "Cancellation lost a completed checkpoint.");
    require(luma::Catalog(root / "cancel.sqlite").libraryCount() == 1,
            "Cancellation result was not durable.");
    for (const auto &entry : std::filesystem::recursive_directory_iterator(destination))
        require(entry.path().extension() != ".tmp", "Cancellation left temporary copy.");
    result = luma::LibraryService(root / "copy.sqlite").import({a, b}, copy);
    require(result.added == 2, "Copy did not add both assets.");
    for (const auto &item : luma::Catalog(root / "copy.sqlite").libraryPage({}, 0, 128)) {
        require(item.path != a && item.path != b, "Copy registered source path.");
        require(luma::macos::sha256File(item.path) == item.checksum, "Copy checksum changed.");
    }
    require(luma::macos::sha256File(a) == hash, "Import modified original.");
    // Discovery failures are durable in the initial transaction, before any file
    // is processed; recovery must not reclassify them as queued cancellations.
    const auto deniedPath = input / "denied-child";
    const auto discoveryCatalog = root / "discovery-recovery.sqlite";
    luma::Catalog discoveryDB(discoveryCatalog);
    throws([&] {
        discoveryDB.beginImport(luma::ImportMode::add, {}, {a}, getpid(), true,
                                {{deniedPath, "permission denied"}});
    });
    require(discoveryDB.unfinishedImports().empty(), "Invalid discovery error created a session.");
    const auto discoverySession =
        discoveryDB.beginImport(luma::ImportMode::add, {}, {a, deniedPath}, 99999999, true,
                                {{deniedPath, "permission denied"}});
    require(discoveryDB.importSummary(discoverySession).failed == 1 &&
                discoveryDB.importProblems(discoverySession) ==
                    std::vector<std::string>{deniedPath.string() + "\npermission denied"},
            "Initial discovery failure was not durable.");
    const auto discoveryRecovery = luma::LibraryService(discoveryCatalog).recover();
    require(discoveryRecovery.size() == 1 && discoveryRecovery.front().failed == 1 &&
                discoveryRecovery.front().cancelled == 1 &&
                discoveryDB.importProblems(discoverySession).front() ==
                    deniedPath.string() + "\npermission denied" &&
                discoveryDB.unfinishedImports().empty(),
            "Recovery discarded a known discovery failure.");
    discoveryDB.verify();
    // Simulate process death after filesystem installation but before DB checkpoint.
    const auto recoveryPath = root / "recovery.sqlite";
    luma::Catalog db(recoveryPath);
    const auto baseline =
        db.addAndSelect(a, luma::macos::sha256File(a), luma::RawDecoder().inspect(a));
    db.saveExposure(baseline.id, 0.75);
    const auto session =
        db.beginImport(luma::ImportMode::copy, destination, {a, b}, 99999999, false);
    auto journalItem = [&](int index, const auto &raw) {
        luma::ImportJournalItem item;
        item.sessionID = session;
        item.index = index;
        item.source = raw;
        item.checksum = luma::macos::sha256File(raw);
        item.size = std::filesystem::file_size(raw);
        const auto parent = destination / ("Luma-" + session) / std::to_string(index);
        std::filesystem::create_directories(parent);
        item.target = parent / raw.filename();
        item.temporary = parent / (".luma-copy-" + session + "-" + std::to_string(index) + ".tmp");
        return item;
    };
    auto complete = journalItem(0, a), changed = journalItem(1, b);
    db.stageImport(complete);
    db.stageImport(changed);
    std::filesystem::copy_file(a, complete.target);
    {
        std::ofstream file(complete.temporary);
        file << "partial";
    }
    {
        std::ofstream file(changed.target);
        file << "Externally modified published copy";
    }
    const auto live = db.beginImport(luma::ImportMode::add, {}, {a}, getpid());
    std::promise<void> startRecovery;
    auto readyRecovery = startRecovery.get_future().share();
    auto recover = [&] {
        readyRecovery.wait();
        return luma::LibraryService(recoveryPath).recover();
    };
    auto recoveryA = std::async(std::launch::async, recover),
         recoveryB = std::async(std::launch::async, recover);
    startRecovery.set_value();
    auto recovered = recoveryA.get();
    auto other = recoveryB.get();
    recovered.insert(recovered.end(), other.begin(), other.end());
    require(recovered.size() == 1 && recovered.front().added == 1 && recovered.front().failed == 1,
            "Recovery did not register complete copy.");
    require(db.libraryCount() == 2, "Recovery ignored persisted allow-duplicates policy.");
    require(db.activeAsset()->id == baseline.id && db.activeAsset()->edits.exposure == 0.75,
            "Import recovery changed active edits.");
    require(!std::filesystem::exists(complete.temporary), "Recovery left owned temporary.");
    require(std::filesystem::exists(changed.target), "Recovery deleted changed published file.");
    require(db.unfinishedImports().size() == 1 && db.unfinishedImports().front().id == live,
            "Recovery touched live owner.");
    require(luma::LibraryService(recoveryPath).recover().empty(),
            "Recovery repeated terminal import.");
    db.finishImport(live, "cancelled");
    db.verify();
}
void viewportTests(const std::filesystem::path &root) {
    const auto path = root / "viewport.sqlite";
    luma::Catalog db(path);
    sql(path, R"sql(BEGIN;
        INSERT INTO assets(id,original_path,filename,extension,filesize,checksum,width,height,orientation)
        VALUES('b-a','/fixture/b.dng','b.dng','dng',1,'b-a',16,16,0),
              ('b-z','/fixture/B.dng','B.dng','dng',1,'b-z',16,16,0),
              ('d','/fixture/d.dng','d.dng','dng',1,'d',16,16,0);
        INSERT INTO edit_documents(asset_id) SELECT id FROM assets;COMMIT;)sql");
    require(db.libraryIndex({}, "b-a") == 0 && db.libraryIndex({}, "b-z") == 1 &&
                db.libraryIndex({}, "d") == 2,
            "Viewport index lost NOCASE ordering or the stable ID tie-break.");
    require(!db.libraryIndex({}, "missing") && !db.libraryIndex({}, ""),
            "Absent viewport asset incorrectly resolved to a row.");
    db.setRating({"b-z"}, 4);
    db.setLabel({"b-z"}, "blue");
    const auto collection = db.createCollection("Viewport");
    db.collectionMembership(collection, {"b-z"}, true);
    luma::LibraryFilter filtered;
    filtered.search = "b.";
    filtered.minimumRating = 4;
    filtered.label = "blue";
    filtered.collectionID = collection;
    require(db.libraryIndex(filtered, "b-z") == 0 && !db.libraryIndex(filtered, "d"),
            "Viewport index ignored the current metadata/search/collection filter.");
    filtered.search = "%";
    require(!db.libraryIndex(filtered, "b-z"), "Viewport search interpreted a literal wildcard.");
    filtered.minimumRating = 6;
    throws([&] { db.libraryIndex(filtered, ""); });

    LibraryModel model(path);
    QString requested = "d";
    int restored = -2, restorations = 0;
    QObject observer;
    QObject::connect(&model, &LibraryModel::reloadStarted, &observer, [&](bool preserve) {
        model.setViewportAnchor(preserve ? requested : QString{});
    });
    QObject::connect(&model, &LibraryModel::viewportRestored, &observer, [&](int row) {
        restored = row;
        ++restorations;
    });
    wait([&] { return !model.working() && !model.loading() && model.count() == 3; });
    const auto refresh = [&] {
        const int before = restorations;
        model.refresh();
        wait([&] { return restorations > before && !model.loading(); });
        require(model.error().isEmpty(), "Viewport lookup failed the Library reload.");
    };
    refresh();
    require(restored == 2, "Unchanged viewport asset did not resolve after reload.");
    sql(path, R"sql(BEGIN;
        INSERT INTO assets(id,original_path,filename,extension,filesize,checksum,width,height,orientation)
        VALUES('a','/fixture/a.dng','a.dng','dng',1,'a',16,16,0);
        INSERT INTO edit_documents(asset_id) VALUES('a');COMMIT;)sql");
    refresh();
    require(model.count() == 4 && restored == 3,
            "Insert before the viewport retained a numeric row instead of the same asset.");
    requested = "missing";
    refresh();
    require(restored == -1, "Missing viewport asset must not restore another photo.");
    requested = "d";
    const int beforeFilter = restorations;
    model.filter("b.", 0, -2, "any", "");
    wait([&] { return restorations > beforeFilter && !model.loading() && model.count() == 2; });
    require(restored == -1, "Changing filters restored an anchor from the previous result.");
    refresh();
    require(restored == -1, "Filtered-out viewport asset must not restore another row.");
    requested = "b-a";
    const int beforeSuperseded = restorations;
    model.refresh();
    requested = "b-z";
    model.refresh();
    wait([&] { return restorations > beforeSuperseded && !model.loading(); });
    require(restored == 1 && restorations == beforeSuperseded + 1,
            "Superseded reload emitted a stale viewport restoration.");
    db.verify();
}
void firstPageOnResetTests(const std::filesystem::path &root) {
    const auto path = root / "first-page-reset.sqlite";
    luma::Catalog db(path);
    sql(path, R"sql(BEGIN;
        INSERT INTO assets(id,original_path,filename,extension,filesize,checksum,width,height,orientation)
        VALUES('page-a','/fixture/A-first.dng','A-first.dng','dng',1,'page-a',16,16,0),
              ('page-z','/fixture/Z-last.dng','Z-last.dng','dng',1,'page-z',16,16,0);
        INSERT INTO edit_documents(asset_id) SELECT id FROM assets;COMMIT;)sql");

    struct Observation {
        int populatedResets = 0;
        int rowsAtReset = 0;
        std::size_t pagesAtReset = 0;
        bool loadingAtReset = false;
        QString initialID, notifiedID;
        int metadataNotifications = 0, cachedMetadataNotifications = 0;
    } observed;
    LibraryModel model(path);
    QObject observer; // Disconnect before model and captured observation are destroyed.
    QObject::connect(
        &model, &QAbstractItemModel::modelReset, &observer,
        [&] {
            if (model.rowCount() == 0)
                return;
            ++observed.populatedResets;
            observed.rowsAtReset = model.rowCount();
            observed.pagesAtReset = model.cachedPages();
            observed.loadingAtReset = model.loading();
            // This is the ONLY request on a cache miss in each phase. Simulate
            // a real view reading synchronously from the populated modelReset.
            observed.initialID = model.data(model.index(0), LibraryModel::AssetID).toString();
        },
        Qt::DirectConnection);
    QObject::connect(
        &model, &QAbstractItemModel::dataChanged, &observer,
        [&](const QModelIndex &first, const QModelIndex &last, const QList<int> &roles) {
            if (model.rowCount() == 0 || !first.isValid() || !last.isValid() || first.row() > 0 ||
                last.row() < 0 || (!roles.isEmpty() && !roles.contains(LibraryModel::AssetID)))
                return;
            ++observed.metadataNotifications;
            // Never turn a stray Selected/Active or other notification into a
            // cache-miss retry. This two-row fixture can only have page0 cached.
            if (model.cachedPages() != 1)
                return;
            observed.notifiedID = model.data(model.index(0), LibraryModel::AssetID).toString();
            ++observed.cachedMetadataNotifications;
        },
        Qt::DirectConnection);

    const auto verifyPhase = [&](const QString &expectedID, int expectedRows) {
        // Observation/count/cache getters only: DO NOT poll model.data(), use a
        // card helper, select a row, refresh, or otherwise schedule another page.
        wait([&] {
            return observed.populatedResets == 1 && observed.cachedMetadataNotifications > 0 &&
                   observed.notifiedID == expectedID && !model.working() && !model.loading() &&
                   model.count() == expectedRows && model.rowCount() == expectedRows;
        });
        require(observed.initialID.isEmpty() && observed.pagesAtReset == 0,
                "First metadata page was not requested by the one-shot reset observer.");
        require(!observed.loadingAtReset && observed.rowsAtReset == expectedRows,
                "Populated modelReset exposed rows while rejecting first-page requests.");
        require(
            observed.metadataNotifications > 0 && model.cachedPages() == 1 &&
                model.error().isEmpty(),
            "One-shot modelReset read did not receive its actual cached metadata notification.");
    };

    verifyPhase("page-a", 2); // Initial asynchronous recovery/count publication.
    observed = {};
    model.refresh();
    verifyPhase("page-a", 2); // Same rows after a real page-cache reset.
    observed = {};
    model.filter("Z-last", 0, -2, "any", "");
    verifyPhase("page-z", 1); // Debounced filtered reload, different exact first UUID.
    db.verify();
}

void sourceAvailabilityTests(const std::filesystem::path &root,
                             const std::filesystem::path &fixture) {
    const auto folder = root / "watched-originals";
    std::filesystem::create_directory(folder);
    const auto source = folder / "source.dng";
    const auto held = root / "temporarily-removed.dng";
    std::filesystem::copy_file(fixture, source);
    const auto path = root / "source-watch.sqlite";
    luma::Catalog catalog(path);
    const auto original = catalog.addAndSelect(source, luma::macos::sha256File(source),
                                               luma::RawDecoder().inspect(source));
    LibraryModel model(path);
    wait([&] {
        return !model.working() && !model.loading() && model.count() == 1 &&
               model.data(model.index(0), LibraryModel::AssetID).toString() ==
                   QString::fromStdString(original.id);
    });
    model.select(0);
    model.photoOpened(QString::fromStdString(original.id));
    require(!model.data(model.index(0), LibraryModel::Missing).toBool(),
            "Existing watched source is incorrectly marked missing.");
    int resets = 0, notifications = 0;
    bool unexpectedRoles = false;
    QObject::connect(&model, &QAbstractItemModel::modelReset, &model, [&] { ++resets; });
    QObject::connect(
        &model, &QAbstractItemModel::dataChanged, &model,
        [&](const QModelIndex &first, const QModelIndex &last, const QList<int> &roles) {
            ++notifications;
            unexpectedRoles = unexpectedRoles || first.row() != 0 || last.row() != 0 ||
                              roles != QList<int>{LibraryModel::Missing};
        });
    std::filesystem::rename(source, held);
    wait([&] { return model.data(model.index(0), LibraryModel::Missing).toBool(); });
    require(resets == 0 && notifications == 1 && !unexpectedRoles && model.count() == 1 &&
                model.selectedCount() == 1 &&
                model.data(model.index(0), LibraryModel::Active).toBool(),
            "Actual source removal reset the model or changed selection/metadata.");
    std::filesystem::rename(held, source);
    wait([&] { return !model.data(model.index(0), LibraryModel::Missing).toBool(); });
    require(resets == 0 && notifications == 2 && !unexpectedRoles && model.count() == 1 &&
                model.selectedCount() == 1 &&
                model.data(model.index(0), LibraryModel::AssetID).toString() ==
                    QString::fromStdString(original.id) &&
                catalog.assetByID(original.id)->edits == original.edits &&
                luma::macos::sha256File(source) == original.checksum,
            "Actual source restoration changed identity, original bytes or edits.");
    const auto movedFolder = root / "moved-watched-originals";
    std::filesystem::rename(folder, movedFolder);
    wait([&] { return model.data(model.index(0), LibraryModel::Missing).toBool(); });
    std::filesystem::rename(movedFolder, folder);
    wait([&] { return !model.data(model.index(0), LibraryModel::Missing).toBool(); });
    require(resets == 0 && notifications == 4 && !unexpectedRoles,
            "Parent rename/restoration did not refresh only the missing role.");
    std::cout << "Source watcher: file and parent rename/restoration passed\n";
    // Unrelated incoming files must not defer the availability refresh forever.
    // Keep real directory events flowing faster than the production coalescer.
    const auto activity = folder / "unrelated-incoming-file";
    QTimer events;
    events.setInterval(25);
    QObject::connect(&events, &QTimer::timeout, &model, [&] {
        if (std::filesystem::exists(activity))
            std::filesystem::remove(activity);
        else
            std::ofstream(activity) << "owned test activity";
    });
    events.start();
    std::filesystem::rename(source, held);
    try {
        wait([&] { return model.data(model.index(0), LibraryModel::Missing).toBool(); });
    } catch (const std::exception &) {
        throw std::runtime_error("Continuous directory events starved missing-source refresh.");
    }
    std::filesystem::rename(held, source);
    wait([&] { return !model.data(model.index(0), LibraryModel::Missing).toBool(); });
    events.stop();
    std::filesystem::remove(activity);
    require(resets == 0 && notifications == 6 && !unexpectedRoles && model.count() == 1 &&
                model.selectedCount() == 1 &&
                model.data(model.index(0), LibraryModel::Active).toBool() &&
                catalog.assetByID(original.id)->edits == original.edits &&
                luma::macos::sha256File(source) == original.checksum,
            "Continuous folder events starved availability or changed source/model state.");
}

void modelTests(const std::filesystem::path &root, bool scaleContract) {
    const int rowCount = scaleContract ? 100000 : 256;
    const auto path = root / (scaleContract ? "large.sqlite" : "bounded-model.sqlite");
    {
        luma::Catalog db(path);
    }
    const auto createRows = std::string("BEGIN; WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL "
                                        "SELECT i+1 FROM n WHERE i<") +
                            std::to_string(rowCount) + R"sql()
        INSERT INTO assets(id,original_path,filename,extension,filesize,checksum,width,height,orientation,camera)
        SELECT printf('%06d',i),'/fixture/'||printf('%06d',i)||'.dng',printf('%06d',i)||'.dng','dng',1,'fixture',128,96,0,'Synthetic' FROM n;
        INSERT INTO edit_documents(asset_id) SELECT id FROM assets;COMMIT;)sql";
    sql(path, createRows.c_str());
    luma::Catalog db(path);
    require(db.libraryCount() == rowCount, "Metadata fixture count is wrong.");
    require(db.libraryPage({}, rowCount - 10, 128).size() == 10,
            "Last metadata page boundary failed.");
    LibraryModel model(path);
    wait([&] {
        return !model.working() && !model.loading() && model.count() == rowCount &&
               model.rowCount() == rowCount;
    });
    const auto readPage = [&](int row) {
        model.data(model.index(row), LibraryModel::AssetID);
        wait([&] {
            return !model.data(model.index(row), LibraryModel::AssetID).toString().isEmpty();
        });
        require(model.cachedPages() <= LibraryModel::maximumPages,
                "Page cache exceeded its bound.");
    };
    if (scaleContract) {
        for (int page = 0; page < 40; ++page)
            readPage(page * 2000);
    } else {
        for (const int row : {0, 128, 255})
            readPage(row);
    }
    model.selectAll();
    wait([&] { return model.selectedCount() == rowCount; });
    model.clearSelection();
    model.data(model.index(0), LibraryModel::AssetID);
    wait([&] { return !model.data(model.index(0), LibraryModel::AssetID).toString().isEmpty(); });
    model.select(0);
    model.select(5, false, true);
    wait([&] { return model.selectedCount() == 6; });
    model.select(0, true, false);
    require(model.selectedCount() == 5, "Toggle selection failed.");
    int metadataResets = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, &model, [&] { ++metadataResets; });
    model.rate(5);
    wait([&] { return !model.working() && !model.loading(); });
    require(metadataResets == 0,
            "Metadata update unnecessarily reset the unchanged Library model.");
    require(model.data(model.index(1), LibraryModel::Rating).toInt() == 5,
            "Metadata update left cached rating data stale.");
    luma::LibraryFilter rated;
    rated.minimumRating = 5;
    require(db.libraryCount(rated) == 5, "Model bulk edit lost selected assets.");
    model.filter("00000", 5, -2, "any", "");
    wait([&] { return model.count() == 5 && !model.loading(); });
    require(model.error().isEmpty(), "Model reported an error.");
    model.data(model.index(0), LibraryModel::AssetID);
    wait([&] { return !model.data(model.index(0), LibraryModel::AssetID).toString().isEmpty(); });
    model.select(0);
    model.rate(0);
    wait([&] { return !model.working() && !model.loading() && model.count() == 4; });
    require(metadataResets > 0, "A metadata filter retained an asset that no longer matches.");
    std::cout << (scaleContract ? "100k metadata rows: <=16 cached pages (2048 rows); selection "
                                  "and filters passed\n"
                                : "256 metadata rows: two page boundaries, selection/filter/cache "
                                  "notifications passed; scale deferred\n");
}
void maintenanceAfterCancelledImportTests(const std::filesystem::path &root,
                                          const std::filesystem::path &fixture) {
    const auto folder = root / "maintenance-originals";
    std::filesystem::create_directory(folder);
    const auto source = folder / "source.dng";
    std::filesystem::copy_file(fixture, source);
    const auto hash = luma::macos::sha256File(source);
    const auto catalogFolder = root / "maintenance-catalog";
    std::filesystem::create_directory(catalogFolder);
    const auto path = catalogFolder / "catalog.sqlite";
    luma::Catalog catalog(path);
    const auto asset = catalog.addAndSelect(source, hash, luma::RawDecoder().inspect(source));
    catalog.saveExposure(asset.id, 1.25);
    catalog.setRating({asset.id}, 4);
    const auto collection = catalog.createCollection("Keep after cancellation");
    catalog.collectionMembership(collection, {asset.id}, true);
    const auto document = catalog.assetSnapshot(asset.id)->asset.edits;
    const auto revision = catalog.assetSnapshot(asset.id)->revision;
    const auto id = QString::fromStdString(asset.id);
    LibraryModel model(path);
    auto settled = [&] { wait([&] { return !model.working() && !model.loading(); }); };
    auto metadata = [&] {
        model.loadMetadata(id);
        wait([&] { return model.descriptiveMetadata().value("id").toString() == id; });
        require(model.error().isEmpty(), "Maintenance metadata read failed.");
    };
    settled();
    require(model.error().isEmpty(), "Maintenance startup failed.");
    model.setBackupSchedule(0);
    const auto backups = std::filesystem::path(model.backupFolder().toLocalFile().toStdString());
    auto backupFiles = [&] {
        std::set<std::filesystem::path> files;
        for (const auto &entry : std::filesystem::directory_iterator(backups))
            if (entry.path().extension() == ".sqlite3")
                files.insert(entry.path());
        return files;
    };
    auto backup = [&] {
        const auto before = backupFiles();
        model.backupNow();
        settled();
        require(model.error().isEmpty(), "Cancelled import poisoned manual backup.");
        auto after = backupFiles();
        for (const auto &file : before)
            after.erase(file);
        require(after.size() == 1, "Manual backup did not create exactly one snapshot.");
        luma::Catalog::verifyBackup(*after.begin());
        return *after.begin();
    };
    const auto first = backup();
    require(luma::Catalog(first).assetSnapshot(asset.id)->asset.edits == document,
            "Baseline backup lost saved edits.");
    model.importFiles({QUrl::fromLocalFile(QString::fromStdString(source.string()))}, false, {},
                      true, true);
    require(model.importing(), "Cancellation fixture did not start a real import.");
    model.cancelImport();
    settled();
    require(!model.importing() && model.error().isEmpty(), "Import cancellation did not settle.");
    const auto second = backup();
    const auto restored = root / "restored-after-cancel.sqlite";
    model.restoreCatalog(QUrl::fromLocalFile(QString::fromStdString(second.string())),
                         QUrl::fromLocalFile(QString::fromStdString(restored.string())));
    settled();
    require(model.error().isEmpty(), "Cancelled import poisoned backup restore.");
    luma::Catalog::verifyBackup(restored);
    const auto restoredAsset = luma::Catalog(restored).assetSnapshot(asset.id);
    require(restoredAsset && restoredAsset->asset.edits == document &&
                restoredAsset->revision == revision &&
                luma::Catalog(restored).libraryPage({}, 0, 1).front().rating == 4,
            "Restore after import cancellation lost edits/revision/metadata.");
    metadata();
    model.writeMetadataSidecar();
    settled();
    require(model.error().isEmpty() &&
                std::filesystem::is_regular_file(luma::MetadataService::sidecarPath(source)),
            "Cancelled import poisoned explicit XMP write.");
    model.setAutoWriteXMP(true);
    settled();
    require(model.error().isEmpty(), "Cancelled import poisoned pending sidecar sync.");
    wait([&] { return model.data(model.index(0), LibraryModel::AssetID).toString() == id; });
    model.select(0);
    require(model.selectionIDs() == std::vector<std::string>{asset.id},
            "Metadata fixture did not select the original identity.");
    model.rate(5);
    settled();
    const auto written = luma::MetadataService(path).read(asset.id);
    require(model.error().isEmpty() && written.values.rating == 5 &&
                written.writtenRevision == written.revision,
            "Cancelled import poisoned automatic XMP after metadata commit.");
    metadata();
    model.importDescriptiveMetadata();
    settled();
    require(model.error().isEmpty() &&
                luma::MetadataService(path).read(asset.id).values.rating == 5,
            "Cancelled import poisoned XMP readback.");
    const auto movedFolder = root / "located-originals";
    std::filesystem::create_directory(movedFolder);
    const auto moved = movedFolder / source.filename();
    std::filesystem::rename(source, moved);
    metadata();
    model.locateOriginal(QUrl::fromLocalFile(QString::fromStdString(moved.string())));
    settled();
    require(model.error().isEmpty() &&
                catalog.fileRecord(asset.id)->path == std::filesystem::canonical(moved),
            "Cancelled import poisoned Locate Original.");
    const auto movedAgainFolder = root / "located-again-originals";
    std::filesystem::create_directory(movedAgainFolder);
    const auto movedAgain = movedAgainFolder / source.filename();
    std::filesystem::rename(moved, movedAgain);
    metadata();
    model.locateFolder(QUrl::fromLocalFile(QString::fromStdString(movedAgainFolder.string())),
                       false);
    settled();
    require(model.error().isEmpty() &&
                catalog.fileRecord(asset.id)->path == std::filesystem::canonical(movedAgain),
            "Cancelled import poisoned Locate Folder.");
    const auto final = catalog.assetSnapshot(asset.id);
    require(final && final->asset.edits == document && final->revision == revision &&
                catalog.libraryCount() == 1 && catalog.collections().front().count == 1 &&
                luma::MetadataService(path).read(asset.id).values.rating == 5 &&
                luma::macos::sha256File(movedAgain) == hash,
            "Maintenance after cancellation changed original, edits, identity or organization.");
    catalog.verify();
    const auto beforeClose = backupFiles();
    bool closeRequested = false, closeWasImmediate = true, closed = false;
    QObject::connect(&model, &LibraryModel::closeReady, &model, [&] { closed = true; });
    QObject::connect(&model, &LibraryModel::stateChanged, &model, [&] {
        if (!closeRequested && model.working()) {
            closeRequested = true;
            closeWasImmediate = model.requestClose();
        }
    });
    // Request Close at the operation's busy notification, before the worker can
    // start. This exercises cancellation deterministically without timed sleeps.
    model.backupNow();
    require(closeRequested && !closeWasImmediate, "Close bypassed pending maintenance work.");
    wait([&] { return closed; });
    require(model.error() == QStringLiteral("Operation cancelled.") && backupFiles() == beforeClose,
            "Close did not cancel queued maintenance without creating a snapshot.");
    model.backupNow();
    QCoreApplication::processEvents();
    require(backupFiles() == beforeClose, "Closing model accepted new maintenance work.");
    std::cout << "After cancelled import: backup/restore, explicit/automatic XMP, readback, "
                 "file/folder relink and close passed\n";
}

void invalidXmpTextTests(const std::filesystem::path &root, const std::filesystem::path &fixture) {
    const auto folder = root / "invalid-xmp-originals";
    std::filesystem::create_directory(folder);
    const auto source = folder / "source.dng";
    std::filesystem::copy_file(fixture, source);
    const auto hash = luma::macos::sha256File(source);
    const auto path = root / "invalid-xmp.sqlite";
    luma::Catalog catalog(path);
    const auto asset = catalog.addAndSelect(source, hash, luma::RawDecoder().inspect(source));
    catalog.saveExposure(asset.id, 1.25);
    const auto edits = catalog.assetSnapshot(asset.id).value();
    const luma::MetadataService service(path);
    const auto sidecar = luma::MetadataService::sidecarPath(source);
    for (const bool existing : {false, true}) {
        if (existing) {
            const auto before = catalog.metadataSnapshot(asset.id);
            auto valid = before.values;
            valid.title = "Valid Unicode 夜景";
            service.save(asset.id, before.revision, valid);
            const auto published = service.writeSidecar(asset.id, {});
            require(std::filesystem::is_regular_file(published) &&
                        std::filesystem::equivalent(published, sidecar),
                    "Valid metadata did not create the sidecar positive control.");
        }
        const auto oldHash = existing ? luma::macos::sha256File(sidecar) : std::string{};
        const auto before = catalog.metadataSnapshot(asset.id);
        auto invalid = before.values;
        invalid.title = "\xef\xbf\xbe";
        service.save(asset.id, before.revision, invalid);
        const auto pending = catalog.metadataSnapshot(asset.id);
        throws([&] { service.writeSidecar(asset.id, {}); });
        const auto after = catalog.metadataSnapshot(asset.id);
        const auto currentEdits = catalog.assetSnapshot(asset.id).value();
        require(after.values == pending.values && after.revision == pending.revision &&
                    after.writtenRevision == pending.writtenRevision &&
                    after.sidecarHash == pending.sidecarHash &&
                    currentEdits.asset.edits == edits.asset.edits &&
                    currentEdits.revision == edits.revision &&
                    luma::macos::sha256File(source) == hash &&
                    (existing ? luma::macos::sha256File(sidecar) == oldHash
                              : !std::filesystem::exists(sidecar)),
                "Rejected XML text changed sidecar, acknowledgement, original or edits.");
        for (const auto &entry : std::filesystem::directory_iterator(folder))
            require(entry.path() == source || (existing && entry.path() == sidecar),
                    "Rejected XMP text left an unexpected temporary file.");
    }
    catalog.verify();
}

} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2)
        return 2;
    const bool scaleContract = std::string(argv[1]) == "--scale-contract";
    const auto root = std::filesystem::temp_directory_path() / ("luma-library-" + luma::newID());
    std::filesystem::create_directories(root);
    try {
        if (scaleContract) {
            modelTests(root, true);
        } else {
            invalidXmpTextTests(root, argv[1]);
            maintenanceAfterCancelledImportTests(root, argv[1]);
            catalogTests(root, argv[1]);
            importTests(root, argv[1]);
            viewportTests(root);
            firstPageOnResetTests(root);
            modelTests(root, false);
            sourceAvailabilityTests(root, argv[1]);
        }
        std::filesystem::remove_all(root);
        std::cout << (scaleContract ? "Library 100k scale contract passed\n"
                                    : "Library functional unit tests passed\n");
        return 0;
    } catch (const std::exception &e) {
        std::filesystem::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
