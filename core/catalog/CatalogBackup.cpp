#ifdef LUMA_UX_PERSISTENCE_PROBE
#include "tests/ux/PersistenceProbe.h"
#endif
#include "core/catalog/Catalog.h"
#include "core/catalog/SqliteStatement.h"
#include "core/image/Bayer.h"
#include "core/io/StagingFile.h"
#include <chrono>
#include <fcntl.h>
#include <sqlite3.h>
#include <thread>
#include <unistd.h>

namespace luma {
namespace {
struct ReadConnection {
    sqlite3 *db = nullptr;
    explicit ReadConnection(const std::filesystem::path &path) {
        if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX,
                            nullptr) != SQLITE_OK) {
            if (db)
                sqlite3_close(db);
            db = nullptr;
            throw std::runtime_error("Cannot open catalog backup read-only.");
        }
        sqlite3_busy_timeout(db, 3000);
    }
    ~ReadConnection() {
        if (db)
            sqlite3_close(db);
    }
};
void verifyBackupDatabase(sqlite3 *db, CancellationToken stop) {
    checkCancellation(stop);
    sqlite3_progress_handler(
        db, 1000,
        [](void *context) {
            return static_cast<CancellationToken *>(context)->isCancelled() ? 1 : 0;
        },
        &stop);
    struct Reset {
        sqlite3 *db;
        ~Reset() {
            sqlite3_progress_handler(db, 0, nullptr, nullptr);
        }
    } reset{db};
    detail::Statement integrity(db, "PRAGMA integrity_check");
    if (!integrity.row() || integrity.string(0) != "ok" || integrity.row())
        throw std::runtime_error("Catalog backup failed its integrity check.");
    detail::Statement foreignKeys(db, "PRAGMA foreign_key_check");
    if (foreignKeys.row())
        throw std::runtime_error("Catalog backup has broken references.");
    detail::Statement schema(db,
                             "SELECT 1 FROM sqlite_master WHERE type='table' AND name='assets'");
    if (!schema.row())
        throw std::runtime_error("The file is not a Luma catalog.");
}
void snapshot(sqlite3 *source, const std::filesystem::path &destination, CancellationToken stop,
              const std::function<void(double)> &progress) {
    checkCancellation(stop);
    const auto target = std::filesystem::absolute(destination);
    if (std::filesystem::exists(target))
        throw std::runtime_error("Backup destination already exists.");
    std::filesystem::create_directories(target.parent_path());
    StagingFile staging(target.parent_path(), ".luma-backup-");
    struct Temporary {
        std::filesystem::path path;
        ~Temporary() {
            std::error_code ignored;
            std::filesystem::remove(path.string() + "-journal", ignored);
            std::filesystem::remove(path.string() + "-wal", ignored);
            std::filesystem::remove(path.string() + "-shm", ignored);
        }
    } temporary{staging.path()};
    sqlite3 *output = nullptr;
    if (sqlite3_open_v2(temporary.path.c_str(), &output, SQLITE_OPEN_READWRITE, nullptr) !=
        SQLITE_OK) {
        if (output)
            sqlite3_close(output);
        throw std::runtime_error("Cannot open catalog backup.");
    }
    struct Connection {
        sqlite3 *db;
        ~Connection() {
            if (db)
                sqlite3_close(db);
        }
    } connection{output};
    sqlite3_backup *copy = sqlite3_backup_init(output, "main", source, "main");
    if (!copy)
        throw std::runtime_error("Cannot start catalog snapshot.");
    struct Copy {
        sqlite3_backup *value;
        ~Copy() {
            if (value)
                sqlite3_backup_finish(value);
        }
    } backup{copy};
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
#ifdef LUMA_UX_PERSISTENCE_PROBE
    bool firstStep = true;
#endif
    for (;;) {
        checkCancellation(stop);
        const int status = sqlite3_backup_step(copy, 256);
#ifdef LUMA_UX_PERSISTENCE_PROBE
        if (firstStep && (status == SQLITE_OK || status == SQLITE_DONE)) {
            firstStep = false;
            ux::observePersistenceCheckpoint(sqlite3_db_filename(source, "main"),
                                             "backup.after-step", target, temporary.path);
        }
#endif
        if (status == SQLITE_DONE)
            break;
        if (status != SQLITE_OK && status != SQLITE_BUSY && status != SQLITE_LOCKED)
            throw std::runtime_error("Catalog snapshot failed.");
        if (std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("Catalog remained busy during backup.");
        if (status == SQLITE_OK) {
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            const int total = sqlite3_backup_pagecount(copy);
            if (progress && total > 0)
                progress(1.0 - static_cast<double>(sqlite3_backup_remaining(copy)) / total);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
    const int finished = sqlite3_backup_finish(copy);
    backup.value = nullptr;
    if (finished != SQLITE_OK)
        throw std::runtime_error("Cannot finish catalog snapshot.");
    if (sqlite3_close(output) != SQLITE_OK)
        throw std::runtime_error("Cannot finalize catalog backup.");
    connection.db = nullptr;
    const int file = ::open(temporary.path.c_str(), O_RDONLY);
    if (file < 0)
        throw std::runtime_error("Cannot reopen catalog backup.");
    const int synced = fsync(file);
    close(file);
    if (synced != 0)
        throw std::runtime_error("Cannot sync catalog backup.");
    checkCancellation(stop);
    if (link(temporary.path.c_str(), target.c_str()) != 0)
        throw std::runtime_error("Cannot install catalog backup without replacing a file.");
    const int folder = ::open(target.parent_path().c_str(), O_RDONLY);
    if (folder >= 0) {
        (void)fsync(folder);
        close(folder);
    }
    if (progress)
        progress(1);
}
} // namespace
void Catalog::backupTo(const std::filesystem::path &destination, CancellationToken stop,
                       const std::function<void(double)> &progress) const {
    snapshot(db_, destination, stop, progress);
}
void Catalog::verifyBackup(const std::filesystem::path &source, CancellationToken stop) {
    ReadConnection input(source);
    verifyBackupDatabase(input.db, stop);
}
void Catalog::restoreBackupTo(const std::filesystem::path &source,
                              const std::filesystem::path &destination, CancellationToken stop) {
    ReadConnection input(source);
    verifyBackupDatabase(input.db, stop);
    snapshot(input.db, destination, stop, {});
}
} // namespace luma
