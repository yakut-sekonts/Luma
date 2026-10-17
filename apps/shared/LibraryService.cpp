#include "apps/shared/LibraryService.h"
#include "apps/shared/PhotoDecoder.h"
#include "core/image/Bayer.h"
#include "platform/macos/FileHasher.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <set>
#include <unistd.h>

namespace luma {
namespace {
struct DiscoveryResult {
    std::vector<std::filesystem::path> sources;
    std::map<std::filesystem::path, std::string> failures;
};
DiscoveryResult discover(const std::vector<std::filesystem::path> &roots, bool recursive,
                         CancellationToken stop) {
    std::set<std::filesystem::path> paths;
    std::map<std::filesystem::path, std::string> failures;
    auto failed = [&](const std::filesystem::path &path, const std::error_code &error) {
        const auto normalized = path.lexically_normal();
        paths.insert(normalized);
        failures.try_emplace(normalized, "Cannot access import source: " + error.message());
    };
    auto file = [&](const std::filesystem::path &path) {
        std::error_code error;
        const auto canonical = std::filesystem::canonical(path, error);
        if (error)
            failed(path, error);
        else {
            paths.insert(canonical);
            failures.erase(canonical);
        }
    };
    for (const auto &root : roots) {
        checkCancellation(stop);
        std::error_code error;
        const auto status = std::filesystem::status(root, error);
        if (error) {
            failed(root, error);
            continue;
        }
        if (std::filesystem::is_regular_file(status)) {
            file(root);
            continue;
        }
        if (!std::filesystem::is_directory(status)) {
            failed(root, std::make_error_code(std::errc::no_such_file_or_directory));
            continue;
        }
        // Open each directory independently. An increment failure in a recursive
        // iterator can terminate the whole traversal and discard readable siblings.
        std::vector<std::filesystem::path> directories{root};
        while (!directories.empty()) {
            checkCancellation(stop);
            const auto directory = directories.back();
            directories.pop_back();
            std::filesystem::directory_iterator entry(directory, error), end;
            if (error) {
                failed(directory, error);
                continue;
            }
            while (entry != end) {
                checkCancellation(stop);
                const auto path = entry->path();
                const auto linkStatus = entry->symlink_status(error);
                if (error)
                    failed(path, error);
                else {
                    const auto targetStatus =
                        std::filesystem::is_symlink(linkStatus) ? entry->status(error) : linkStatus;
                    if (error)
                        failed(path, error);
                    else if (std::filesystem::is_regular_file(targetStatus) &&
                             PhotoDecoder::supportedExtension(path))
                        file(path);
                    else if (recursive && std::filesystem::is_directory(linkStatus))
                        directories.push_back(path); // Never follow child directory symlinks.
                }
                entry.increment(error);
                if (error) {
                    failed(directory, error);
                    break;
                }
            }
        }
    }
    checkCancellation(stop);
    if (paths.empty())
        throw std::runtime_error("No supported photos found in the selected sources.");
    return {{paths.begin(), paths.end()}, std::move(failures)};
}
class Descriptor {
  public:
    explicit Descriptor(int value) : value(value) {
        if (value < 0)
            throw std::runtime_error("Import file access failed: " +
                                     std::string(std::strerror(errno)));
    }
    ~Descriptor() {
        close(value);
    }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
    int value;
};
class CopyCleanup {
  public:
    CopyCleanup(std::filesystem::path target, std::filesystem::path temporary, std::string checksum)
        : target(std::move(target)), temporary(std::move(temporary)),
          checksum(std::move(checksum)) {}
    ~CopyCleanup() {
        try {
            if (published && !registered && std::filesystem::is_regular_file(target) &&
                macos::sha256File(target) == checksum)
                std::filesystem::remove(target);
            if (temporaryOwned) {
                std::error_code error;
                std::filesystem::remove(temporary, error);
            }
        } catch (...) {
        }
    }
    std::filesystem::path target, temporary;
    std::string checksum;
    bool temporaryOwned = false, published = false, registered = false;
};
void copyAndInstall(const std::filesystem::path &source, CopyCleanup &copy,
                    CancellationToken stop) {
    Descriptor input(open(source.c_str(), O_RDONLY));
    Descriptor output(open(copy.temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600));
    copy.temporaryOwned = true;
    std::vector<char> buffer(1024 * 1024);
    while (true) {
        checkCancellation(stop);
        const auto count = read(input.value, buffer.data(), buffer.size());
        if (count == 0)
            break;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            throw std::runtime_error("Cannot read original during Copy.");
        }
        ssize_t position = 0;
        while (position < count) {
            checkCancellation(stop);
            const auto written = write(output.value, buffer.data() + position,
                                       static_cast<std::size_t>(count - position));
            if (written < 0) {
                if (errno == EINTR)
                    continue;
                throw std::runtime_error("Cannot write imported copy.");
            }
            if (written == 0)
                throw std::runtime_error("Copy write made no progress.");
            position += written;
        }
    }
    if (fsync(output.value) != 0)
        throw std::runtime_error("Cannot sync imported copy.");
    if (macos::sha256File(copy.temporary, stop) != copy.checksum)
        throw std::runtime_error("Copy checksum mismatch.");
    checkCancellation(stop);
    if (link(copy.temporary.c_str(), copy.target.c_str()) != 0)
        throw std::runtime_error("Cannot install Copy without replacing an existing file.");
    copy.published = true;
    Descriptor directory(open(copy.target.parent_path().c_str(), O_RDONLY));
    if (fsync(directory.value) != 0)
        throw std::runtime_error("Cannot sync Copy directory.");
}
bool deadOwner(std::int64_t owner) {
    if (owner <= 0 || owner == getpid() || owner > 2147483647)
        return false;
    errno = 0;
    return kill(static_cast<pid_t>(owner), 0) != 0 && errno == ESRCH;
}
} // namespace
ImportSummary
LibraryService::import(const std::vector<std::filesystem::path> &roots, ImportOptions options,
                       CancellationToken stop,
                       const std::function<void(double, const std::string &)> &progress) const {
    if (options.mode != ImportMode::add && options.mode != ImportMode::copy)
        throw std::runtime_error("Invalid import mode.");
    const auto discovery = discover(roots, options.recursive, stop);
    const auto &sources = discovery.sources;
    if (options.mode == ImportMode::copy) {
        if (options.destination.empty() || !std::filesystem::is_directory(options.destination))
            throw std::runtime_error("Choose an existing destination folder for Copy.");
        options.destination = std::filesystem::canonical(options.destination);
    }
    Catalog catalog(catalog_);
    const auto session = catalog.beginImport(options.mode, options.destination, sources, getpid(),
                                             options.skipDuplicates, discovery.failures);
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (stop.isCancelled())
            break;
        const auto &source = sources[index];
        if (discovery.failures.contains(source)) {
            if (progress)
                progress(static_cast<double>(index + 1) / sources.size(),
                         source.filename().string());
            continue; // Already recorded atomically as failed when the session was created.
        }
        try {
            const auto initialTime = std::filesystem::last_write_time(source);
            const auto size = std::filesystem::file_size(source);
            if (size > 512'000'000)
                throw std::runtime_error("Image exceeds the 512 MB import budget.");
            const auto checksum = macos::sha256File(source, stop);
            const auto metadata = PhotoDecoder().inspect(source, stop);
            if (initialTime != std::filesystem::last_write_time(source) ||
                size != std::filesystem::file_size(source))
                throw std::runtime_error("Original changed while being inspected.");
            if (options.skipDuplicates) {
                if (const auto duplicate =
                        catalog.duplicate(checksum, static_cast<std::int64_t>(size))) {
                    catalog.finishImportItem(session, static_cast<int>(index), "duplicate", "",
                                             duplicate->id);
                    if (progress)
                        progress(static_cast<double>(index + 1) / sources.size(),
                                 source.filename().string());
                    continue;
                }
            }
            ImportJournalItem item;
            item.sessionID = session;
            item.index = static_cast<int>(index);
            item.source = source;
            item.target = source;
            item.checksum = checksum;
            item.size = static_cast<std::int64_t>(size);
            if (options.mode == ImportMode::copy) {
                const auto directory =
                    options.destination / ("Luma-" + session) / std::to_string(index);
                std::filesystem::create_directories(directory);
                item.target = directory / source.filename();
                item.temporary =
                    directory / (".luma-copy-" + session + "-" + std::to_string(index) + ".tmp");
            }
            catalog.stageImport(item);
            if (initialTime != std::filesystem::last_write_time(source) ||
                size != std::filesystem::file_size(source))
                throw std::runtime_error("Original changed while being inspected.");
            if (options.mode == ImportMode::copy) {
                CopyCleanup copy(item.target, item.temporary, checksum);
                copyAndInstall(source, copy, stop);
                if (initialTime != std::filesystem::last_write_time(source) ||
                    size != std::filesystem::file_size(source))
                    throw std::runtime_error("Original changed during Copy.");
                checkCancellation(stop);
                copy.registered = catalog.commitImport(item, metadata, options.skipDuplicates);
            } else {
                checkCancellation(stop);
                catalog.commitImport(item, metadata, options.skipDuplicates);
            }
        } catch (const std::exception &error) {
            catalog.finishImportItem(session, static_cast<int>(index),
                                     stop.isCancelled() ? "cancelled" : "failed", error.what());
        }
        if (progress)
            progress(static_cast<double>(index + 1) / sources.size(), source.filename().string());
    }
    catalog.finishImport(session, stop.isCancelled() ? "cancelled" : "completed");
    return catalog.importSummary(session);
}
std::vector<ImportSummary> LibraryService::recover(CancellationToken stop) const {
    Catalog catalog(catalog_);
    std::vector<ImportSummary> summaries;
    for (const auto &journal : catalog.unfinishedImports()) {
        checkCancellation(stop);
        if (!deadOwner(journal.ownerPID) ||
            !catalog.claimImport(journal.id, journal.ownerPID, getpid()))
            continue;
        for (const auto &item : journal.items) {
            checkCancellation(stop);
            if (item.state == "queued") {
                catalog.finishImportItem(journal.id, item.index, "cancelled",
                                         "Interrupted before validation.");
                continue;
            }
            try {
                if (journal.mode == ImportMode::copy) {
                    if (journal.id.size() != 36 ||
                        journal.id.find_first_not_of("0123456789abcdef-") != std::string::npos ||
                        item.target.filename() != item.source.filename() ||
                        item.target == item.temporary)
                        throw std::runtime_error("Invalid Copy journal identity; files preserved.");
                    const auto expectedParent =
                        std::filesystem::canonical(journal.destination) / ("Luma-" + journal.id);
                    if (std::filesystem::weakly_canonical(item.target)
                                .parent_path()
                                .parent_path() != expectedParent ||
                        item.target.parent_path().filename() != std::to_string(item.index) ||
                        item.temporary.parent_path() != item.target.parent_path() ||
                        item.temporary.filename() != (".luma-copy-" + journal.id + "-" +
                                                      std::to_string(item.index) + ".tmp"))
                        throw std::runtime_error(
                            "Copy journal path is not session-owned; files preserved.");
                    std::error_code ignored;
                    std::filesystem::remove(item.temporary, ignored);
                } else if (item.target != item.source)
                    throw std::runtime_error("Invalid Add journal path.");
                if (!std::filesystem::is_regular_file(item.target) ||
                    std::filesystem::file_size(item.target) !=
                        static_cast<std::uintmax_t>(item.size) ||
                    macos::sha256File(item.target, stop) != item.checksum)
                    throw std::runtime_error(
                        "Interrupted item is incomplete or changed; published file preserved.");
                const auto metadata = PhotoDecoder().inspect(item.target, stop);
                const bool inserted = catalog.commitImport(item, metadata, journal.skipDuplicates);
                if (!inserted && journal.mode == ImportMode::copy &&
                    (!std::filesystem::exists(item.source) ||
                     !std::filesystem::equivalent(item.source, item.target))) {
                    std::error_code cleanupError;
                    std::filesystem::remove(item.target, cleanupError);
                }
            } catch (const std::exception &error) {
                if (stop.isCancelled())
                    throw;
                catalog.finishImportItem(journal.id, item.index, "failed", error.what());
            }
        }
        catalog.finishImport(journal.id, "interrupted");
        summaries.push_back(catalog.importSummary(journal.id));
    }
    return summaries;
}
} // namespace luma
