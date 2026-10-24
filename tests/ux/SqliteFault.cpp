#include "tests/ux/SqliteFault.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <thread>

namespace ux {
namespace {
bool publishedCopy(const std::string &root, std::size_t position) noexcept {
    if (root.empty())
        return true;
    try {
        std::error_code error;
        std::filesystem::directory_iterator session(root, error), end;
        std::size_t published = 0;
        while (!error && session != end) {
            // Current default Copy publishes directly into the fresh, private
            // destination. The Nth completed PNG arms the next catalog write.
            // Keep the old session-directory branch for legacy recovery fixtures.
            if (session->path().extension() == ".png" && session->is_regular_file(error)) {
                if (published++ == position)
                    return true;
            }
            if (session->path().filename().string().starts_with("Luma-") &&
                session->is_directory(error)) {
                // A fresh owned destination contains one import session. Check
                // its exact item directory instead of scanning all earlier copies.
                std::error_code itemError;
                std::filesystem::directory_iterator file(session->path() / std::to_string(position),
                                                         itemError);
                while (!itemError && file != end) {
                    if (file->path().extension() == ".png" && file->is_regular_file(itemError))
                        return true;
                    file.increment(itemError);
                }
            }
            session.increment(error);
        }
    } catch (...) {
        // A missing/failed fixture checkpoint must time out, never escape SQLite's C API.
        return false;
    }
    return false;
}
} // namespace
SqliteFault *SqliteFault::active_ = nullptr;
SqliteFault::SqliteFault(std::string catalog) : catalog_(std::move(catalog)) {
    if (active_)
        throw std::runtime_error("Only one UX catalog fault VFS may exist per process");
    original_ = sqlite3_vfs_find(nullptr);
    if (!original_)
        throw std::runtime_error("SQLite default VFS unavailable");
    wrapper_ = *original_;
    wrapper_.pNext = nullptr;
    wrapper_.zName = "luma-ux-fault";
    wrapper_.xOpen = open;
    active_ = this;
    if (sqlite3_vfs_register(&wrapper_, 1) != SQLITE_OK) {
        active_ = nullptr;
        throw std::runtime_error("Cannot register UX catalog fault VFS");
    }
}
SqliteFault::~SqliteFault() {
    std::fprintf(stderr, "UX_SHUTDOWN phase=sqlite_vfs.destructor.begin\n");
    std::fflush(stderr);
    std::size_t wrappedFiles = 0, catalogFiles = 0;
    bool countsAvailable = false;
    try {
        std::lock_guard lock(mutex_);
        wrappedFiles = files_.size();
        for (const auto &entry : files_)
            if (entry.second.catalog)
                ++catalogFiles;
        countsAvailable = true;
    } catch (...) {
        // Diagnostics must not add an exception to the original destructor path.
        std::fputs("UX_SHUTDOWN phase=sqlite_vfs.destructor.snapshot.unavailable\n", stderr);
    }
    std::fprintf(stderr,
                 "UX_SHUTDOWN phase=sqlite_vfs.destructor.snapshot counts_available=%d "
                 "wrapped_files=%zu catalog_files=%zu\n",
                 countsAvailable ? 1 : 0, wrappedFiles, catalogFiles);
    std::fflush(stderr);
    sqlite3_vfs_unregister(&wrapper_);
    sqlite3_vfs_register(original_, 1);
    active_ = nullptr;
    // Member storage is destroyed after this body; Context's end marker brackets it.
    std::fprintf(stderr, "UX_SHUTDOWN phase=sqlite_vfs.destructor.body.end\n");
    std::fflush(stderr);
}
int SqliteFault::open(sqlite3_vfs *, const char *name, sqlite3_file *file, int flags, int *out) {
    auto &self = *active_;
    const int result = self.original_->xOpen(self.original_, name, file, flags, out);
    if (result != SQLITE_OK || !file->pMethods)
        return result;
    const auto *methods = file->pMethods;
    try {
        const std::string path = name ? name : "";
        const bool catalog = path == self.catalog_ || path == self.catalog_ + "-wal" ||
                             path == self.catalog_ + "-journal";
        std::lock_guard lock(self.mutex_);
        auto [entry, inserted] = self.files_.emplace(file, File{*methods, methods, catalog});
        if (!inserted) {
            methods->xClose(file);
            return SQLITE_IOERR;
        }
        entry->second.methods.xWrite = write;
        entry->second.methods.xClose = close;
        file->pMethods = &entry->second.methods;
        return result;
    } catch (...) {
        methods->xClose(file);
        return SQLITE_NOMEM;
    }
}
int SqliteFault::write(sqlite3_file *file, const void *data, int size, sqlite3_int64 offset) {
    auto &self = *active_;
    const sqlite3_io_methods *original;
    bool catalog = false;
    {
        std::lock_guard lock(self.mutex_);
        const auto found = self.files_.find(file);
        if (found == self.files_.end())
            return SQLITE_IOERR;
        const auto &entry = found->second;
        if (entry.catalog && self.full_.load()) {
            ++self.rejected_;
            return SQLITE_FULL;
        }
        original = entry.original;
        catalog = entry.catalog;
    }
    const int result = original->xWrite(file, data, size, offset);
    bool checkpoint = false;
    if (result == SQLITE_OK && catalog && self.hold_.load()) {
        std::lock_guard lock(self.mutex_);
        checkpoint = publishedCopy(self.copyRoot_, self.copyPosition_);
    }
    if (checkpoint) {
        self.blocked_.store(true);
        while (self.hold_.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        self.blocked_.store(false);
    }
    return result;
}
int SqliteFault::close(sqlite3_file *file) {
    auto &self = *active_;
    const sqlite3_io_methods *original;
    {
        std::lock_guard lock(self.mutex_);
        const auto entry = self.files_.find(file);
        if (entry == self.files_.end())
            return SQLITE_IOERR;
        original = entry->second.original;
        file->pMethods = original;
        self.files_.erase(entry);
    }
    return original->xClose(file);
}
} // namespace ux
