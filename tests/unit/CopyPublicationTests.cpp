#include "apps/shared/LibraryService.h"
#include "tests/ux/SqliteFault.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QByteArray bytes(const std::filesystem::path &path) {
    QFile file(QString::fromStdString(path.string()));
    require(file.open(QIODevice::ReadOnly) && file.size() > 0 && file.size() <= 1024LL * 1024,
            "Cannot read bounded private Copy fixture bytes.");
    const auto result = file.readAll();
    require(result.size() == file.size(), "Private Copy read was incomplete.");
    return result;
}
std::string text(sqlite3_stmt *query, int column) {
    const auto *value = sqlite3_column_text(query, column);
    return value ? std::string(reinterpret_cast<const char *>(value),
                               static_cast<std::size_t>(sqlite3_column_bytes(query, column)))
                 : std::string();
}
struct Held {
    luma::ImportJournalItem item;
    std::string mode, sessionState;
    std::int64_t assetsVisible = -1;
    bool futurePending = false, targetRegular = false, temporaryPresent = false;
    bool temporarySameInode = false;
    std::uint64_t targetLinks = 0;
    QByteArray targetBytes;
    std::vector<std::string> names;
};
Held capture(const std::filesystem::path &catalog) {
    sqlite3 *raw = nullptr;
    const int opened =
        sqlite3_open_v2(catalog.c_str(), &raw, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
    require(opened == SQLITE_OK && sqlite3_busy_timeout(raw, 1000) == SQLITE_OK,
            "Cannot read actual held catalog journal.");
    sqlite3_stmt *statement = nullptr;
    const int prepared = sqlite3_prepare_v2(
        raw,
        "SELECT i.session_id,i.position,i.source_path,i.target_path,i.temporary_path,"
        "i.checksum,i.filesize,i.state,s.mode,s.state,(SELECT COUNT(*) FROM assets) "
        "FROM import_items i JOIN import_sessions s ON s.id=i.session_id",
        -1, &statement, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> query(statement,
                                                                           sqlite3_finalize);
    require(prepared == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW,
            "Actual held Copy has no single journal row.");
    Held result;
    result.item.sessionID = text(statement, 0);
    result.item.index = sqlite3_column_int(statement, 1);
    result.item.source = text(statement, 2);
    result.item.target = text(statement, 3);
    result.item.temporary = text(statement, 4);
    result.item.checksum = text(statement, 5);
    result.item.size = sqlite3_column_int64(statement, 6);
    result.item.state = text(statement, 7);
    result.mode = text(statement, 8);
    result.sessionState = text(statement, 9);
    result.assetsVisible = sqlite3_column_int64(statement, 10);
    require(sqlite3_step(statement) == SQLITE_DONE, "Held Copy has unexpected additional rows.");
    struct stat target{}, temporary{};
    result.targetRegular =
        ::lstat(result.item.target.c_str(), &target) == 0 && S_ISREG(target.st_mode);
    if (result.targetRegular) {
        result.targetLinks = static_cast<std::uint64_t>(target.st_nlink);
        result.targetBytes = bytes(result.item.target);
    }
    result.temporaryPresent = ::lstat(result.item.temporary.c_str(), &temporary) == 0;
    result.temporarySameInode = result.targetRegular && result.temporaryPresent &&
                                target.st_dev == temporary.st_dev &&
                                target.st_ino == temporary.st_ino;
    for (const auto &entry : std::filesystem::directory_iterator(result.item.target.parent_path()))
        result.names.push_back(entry.path().filename().string());
    std::sort(result.names.begin(), result.names.end());
    return result; // RO statement/database close before release and VFS teardown.
}
struct ReleaseBeforeJoin {
    ux::SqliteFault &fault;
    ~ReleaseBeforeJoin() noexcept {
        // false branch is an atomic store; it never takes the diagnostic mutex.
        fault.holdWrites(false);
    }
};
struct Observation {
    bool entered = false;
    Held held;
    luma::ImportSummary summary;
};
Observation observe(const std::filesystem::path &catalog, const std::filesystem::path &source,
                    const std::filesystem::path &destination) {
    // No controller/model/thumbnail workers exist in this QCoreApplication-only target.
    ux::SqliteFault fault(catalog.string());
    fault.holdWritesAfterCopy(destination.string(), 0);
    luma::ImportOptions options;
    options.mode = luma::ImportMode::copy;
    options.destination = destination;
    options.skipDuplicates = false;
    auto operation = std::async(std::launch::async, [&] {
        return luma::LibraryService(catalog).import({source}, options);
    });
    // Declaration order is deliberate: release BEFORE future destructor joins on any throw.
    ReleaseBeforeJoin release{fault};
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!fault.writeBlocked() && std::chrono::steady_clock::now() < deadline &&
           operation.wait_for(0ms) != std::future_status::ready)
        std::this_thread::sleep_for(1ms);
    Observation observed;
    observed.entered = fault.writeBlocked();
    if (observed.entered) {
        observed.held = capture(catalog);
        observed.held.futurePending = operation.wait_for(0ms) != std::future_status::ready;
    }
    fault.holdWrites(false);
    require(operation.wait_for(10s) == std::future_status::ready,
            "Copy worker did not finish after actual WAL hold release.");
    observed.summary = operation.get();
    return observed; // release, empty future, then VFS destruct; no handles/worker escape.
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir privateRoot(QDir::tempPath() + "/luma-copy-publication-XXXXXX");
    try {
        require(privateRoot.isValid(), "Cannot create private Copy test root.");
        const auto root = std::filesystem::canonical(privateRoot.path().toStdString());
        const auto source = root / "source.png", catalog = root / "catalog.sqlite";
        const auto destination = root / "destination";
        std::filesystem::create_directory(destination);
        QImage image(8, 6, QImage::Format_RGB32);
        image.fill(qRgb(31, 77, 123));
        image.setPixel(3, 2, qRgb(200, 40, 7));
        require(image.save(QString::fromStdString(source.string()), "PNG"),
                "Cannot generate private PNG fixture.");
        const auto original = bytes(source);
        const auto checksum =
            QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex().toStdString();
        {
            luma::Catalog initial(catalog);
            require(initial.libraryCount() == 0, "Copy test catalog was not empty.");
            initial.verify();
        }
        const auto result = observe(catalog, source, destination);
        const auto &held = result.held;
        // All assertions below run AFTER release+get/join and complete test VFS teardown.
        std::cout << QJsonDocument(
                         QJsonObject{
                             {"held", result.entered},
                             {"future_pending_at_capture", held.futurePending},
                             {"item_state_at_capture", QString::fromStdString(held.item.state)},
                             {"assets_visible_at_capture", static_cast<qint64>(held.assetsVisible)},
                             {"temporary_present_at_capture", held.temporaryPresent},
                             {"temporary_same_inode_at_capture", held.temporarySameInode},
                             {"target_links_at_capture", static_cast<qint64>(held.targetLinks)}})
                         .toJson(QJsonDocument::Compact)
                         .constData()
                  << '\n';
        require(result.entered && held.futurePending && held.targetRegular,
                "Required real publication/WAL hold boundary was not reached.");
        require(
            held.mode == "copy" && held.sessionState == "running" && held.item.state == "staged" &&
                held.assetsVisible == 0 && held.item.index == 0 && held.item.source == source &&
                held.item.target.filename() == source.filename() &&
                held.item.target.parent_path() == destination &&
                held.item.temporary.parent_path() == held.item.target.parent_path() &&
                held.item.temporary.filename() == ".luma-copy-" + held.item.sessionID + "-0.tmp" &&
                held.item.checksum == checksum &&
                held.item.size == static_cast<std::int64_t>(original.size()) &&
                held.targetBytes == original,
            "Captured boundary crossed actual staged source/target/journal identities.");
        require(!held.temporaryPresent && held.targetLinks == 1 &&
                    held.names == std::vector<std::string>{"source.png"},
                "Published Copy still owned a temporary hardlink while catalog write was held.");
        require(result.summary.added == 1 && result.summary.duplicates == 0 &&
                    result.summary.failed == 0 && result.summary.cancelled == 0,
                "Copy publication cleanup changed actual import outcome.");
        luma::Catalog final(catalog);
        const auto ids = final.libraryIDs({});
        const auto stored = ids.size() == 1 ? final.assetSnapshot(ids.front()) : std::nullopt;
        require(stored && stored->asset.path == held.item.target &&
                    stored->asset.checksum == checksum &&
                    stored->asset.edits == luma::EditDocument{} &&
                    stored->asset.metadata.width == 8 && stored->asset.metadata.height == 6 &&
                    final.unfinishedImports().empty() && !final.activeAsset() &&
                    bytes(source) == original && bytes(held.item.target) == original &&
                    !std::filesystem::exists(held.item.temporary) &&
                    std::filesystem::hard_link_count(held.item.target) == 1,
                "Released Copy lost durable document/metadata/bytes or retained its alias.");
        final.verify();
        std::cout << "Copy publication single-link invariant passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
