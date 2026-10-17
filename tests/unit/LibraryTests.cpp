#include "apps/desktop/LibraryModel.h"
#include "core/catalog/Identity.h"
#include "core/raw/RawDecoder.h"
#include "platform/macos/FileHasher.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <fstream>
#include <future>
#include <iostream>
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
void modelTests(const std::filesystem::path &root) {
    const auto path = root / "large.sqlite";
    {
        luma::Catalog db(path);
    }
    sql(path, R"sql(BEGIN;
        WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM n WHERE i<100000)
        INSERT INTO assets(id,original_path,filename,extension,filesize,checksum,width,height,orientation,camera)
        SELECT printf('%06d',i),'/fixture/'||printf('%06d',i)||'.dng',printf('%06d',i)||'.dng','dng',1,'fixture',128,96,0,'Synthetic' FROM n;
        INSERT INTO edit_documents(asset_id) SELECT id FROM assets;COMMIT;)sql");
    luma::Catalog db(path);
    require(db.libraryCount() == 100000, "Large fixture count is wrong.");
    require(db.libraryPage({}, 99990, 128).size() == 10, "Last metadata page boundary failed.");
    LibraryModel model(path);
    wait([&] { return !model.working() && !model.loading() && model.count() == 100000; });
    for (int page = 0; page < 40; ++page) {
        const int row = page * 2000;
        model.data(model.index(row), LibraryModel::AssetID);
        wait([&] {
            return !model.data(model.index(row), LibraryModel::AssetID).toString().isEmpty();
        });
        require(model.cachedPages() <= LibraryModel::maximumPages,
                "Page cache exceeded its bound.");
    }
    model.selectAll();
    wait([&] { return model.selectedCount() == 100000; });
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
    std::cout
        << "100k metadata rows: <=16 cached pages (2048 rows); selection and filters passed\n";
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2)
        return 2;
    const auto root = std::filesystem::temp_directory_path() / ("luma-library-" + luma::newID());
    std::filesystem::create_directories(root);
    try {
        catalogTests(root, argv[1]);
        importTests(root, argv[1]);
        viewportTests(root);
        modelTests(root);
        std::filesystem::remove_all(root);
        std::cout << "Library unit tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::filesystem::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
