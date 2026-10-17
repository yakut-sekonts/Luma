#pragma once
#include <atomic>
#include <cstddef>
#include <map>
#include <mutex>
#include <sqlite3.h>
#include <string>

namespace ux {
// Process-local test VFS. Production binaries never link this implementation.
// All I/O delegates to SQLite's original VFS except explicitly armed writes to
// this test's catalog/WAL/journal, which return the real SQLITE_FULL error code.
class SqliteFault {
  public:
    explicit SqliteFault(std::string catalog);
    ~SqliteFault();
    SqliteFault(const SqliteFault &) = delete;
    SqliteFault &operator=(const SqliteFault &) = delete;
    void diskFull(bool enabled) {
        full_.store(enabled);
    }
    int rejectedWrites() const {
        return rejected_.load();
    }
    void holdWrites(bool enabled) {
        if (enabled) {
            std::lock_guard lock(mutex_);
            copyRoot_.clear();
        }
        hold_.store(enabled);
    }
    void holdWritesAfterCopy(std::string directory, std::size_t position = 0) {
        {
            std::lock_guard lock(mutex_);
            copyRoot_ = std::move(directory);
            copyPosition_ = position;
        }
        hold_.store(true);
    }
    bool writeBlocked() const {
        return blocked_.load();
    }

  private:
    struct File {
        sqlite3_io_methods methods;
        const sqlite3_io_methods *original;
        bool catalog;
    };
    static SqliteFault *active_;
    sqlite3_vfs wrapper_{}, *original_ = nullptr;
    std::string catalog_, copyRoot_;
    std::size_t copyPosition_ = 0;
    std::mutex mutex_;
    std::map<sqlite3_file *, File> files_;
    std::atomic<bool> full_{false};
    std::atomic<bool> hold_{false}, blocked_{false};
    std::atomic<int> rejected_{0};
    static int open(sqlite3_vfs *, const char *, sqlite3_file *, int, int *);
    static int write(sqlite3_file *, const void *, int, sqlite3_int64);
    static int close(sqlite3_file *);
};
} // namespace ux
