#include "core/io/StagingFile.h"
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#include "apps/shared/LibraryService.h"
#include "apps/shared/PhotoDecoder.h"
#include "core/image/Bayer.h"
#include "platform/macos/FileHasher.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
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
                         CancellationToken stop, bool allowFileSymlinks = true) {
    std::set<std::filesystem::path> paths;
    std::map<std::filesystem::path, std::string> failures;
    auto failed = [&](const std::filesystem::path &path, const std::error_code &error) {
        const auto normalized = path.lexically_normal();
        paths.insert(normalized);
        failures.try_emplace(normalized, "Cannot access import source: " + error.message());
    };
    auto file = [&](const std::filesystem::path &path) {
        std::error_code error;
        if (!allowFileSymlinks &&
            std::filesystem::is_symlink(std::filesystem::symlink_status(path, error))) {
            const auto normalized = path.lexically_normal();
            paths.insert(normalized);
            failures[normalized] =
                "Move does not follow source file symlinks. Select the original file explicitly.";
            return;
        }
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
    Descriptor(int value, const std::filesystem::path &path) : value(value) {
        if (value < 0) {
            const auto error = errno;
            throw std::runtime_error("Import file access failed: " + path.string() + ": " +
                                     std::string(std::strerror(error)));
        }
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
    Descriptor input(open(source.c_str(), O_RDONLY), source);
    Descriptor output(open(copy.temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600),
                      copy.temporary);
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
    if (unlink(copy.temporary.c_str()) != 0)
        throw std::runtime_error("Cannot remove imported Copy temporary.");
    copy.temporaryOwned = false;
    Descriptor directory(open(copy.target.parent_path().c_str(), O_RDONLY),
                         copy.target.parent_path());
    if (fsync(directory.value) != 0)
        throw std::runtime_error("Cannot sync Copy directory.");
}
std::filesystem::path existingDirectory(const std::filesystem::path &path) {
    if (path.empty() || !std::filesystem::is_directory(path))
        throw std::runtime_error("Choose an existing import destination folder.");
    return std::filesystem::canonical(path);
}
bool inside(const std::filesystem::path &path, const std::filesystem::path &root) {
    const auto relative = std::filesystem::weakly_canonical(path).lexically_relative(
        std::filesystem::canonical(root));
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != ".." &&
           relative != ".";
}
std::filesystem::path backupTemporary(const ImportJournalItem &item) {
    return item.backup.empty()
               ? std::filesystem::path{}
               : item.backup.parent_path() /
                     (".luma-backup-" + item.sessionID + "-" + std::to_string(item.index) + ".tmp");
}
void chooseImportPaths(ImportJournalItem &item, const std::filesystem::path &relative,
                       const ImportOptions &options) {
    for (int suffix = 0; suffix < 10000; ++suffix) {
        auto name = relative.filename();
        if (suffix)
            name = relative.stem().string() + "-" + std::to_string(suffix) +
                   relative.extension().string();
        const auto leaf = relative.parent_path() / name;
        const auto target =
            options.mode == ImportMode::add ? item.source : options.destination / leaf;
        const auto backup = options.backupDestination.empty() ? std::filesystem::path{}
                                                              : options.backupDestination / leaf;
        if ((options.mode != ImportMode::add && std::filesystem::exists(target)) ||
            (!backup.empty() && std::filesystem::exists(backup)))
            continue;
        if (options.mode != ImportMode::add) {
            std::filesystem::create_directories(target.parent_path());
            if (!inside(target, options.destination))
                throw std::runtime_error("Import destination escaped its selected folder.");
        }
        if (!backup.empty()) {
            std::filesystem::create_directories(backup.parent_path());
            if (!inside(backup, options.backupDestination))
                throw std::runtime_error("Backup destination escaped its selected folder.");
        }
        item.target = target;
        item.backup = backup;
        if (options.mode != ImportMode::add)
            item.temporary = target.parent_path() / (".luma-copy-" + item.sessionID + "-" +
                                                     std::to_string(item.index) + ".tmp");
        return;
    }
    throw std::runtime_error("Too many import filename collisions.");
}
void applyImportMetadata(PhotoMetadata &metadata, const ImportOptions &options) {
    if (options.metadata == DescriptiveMetadata{})
        return;
    metadata.descriptive =
        importMetadata(metadata.descriptive.value_or(DescriptiveMetadata{}), options.metadata);
    metadata.descriptiveSource.clear();
    metadata.sidecarHash.clear();
}
void verified(const std::filesystem::path &path, const ImportJournalItem &item,
              CancellationToken stop = {}) {
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)) ||
        !std::filesystem::is_regular_file(path) ||
        std::filesystem::file_size(path) != static_cast<std::uintmax_t>(item.size) ||
        macos::sha256File(path, stop) != item.checksum)
        throw std::runtime_error("Import file is missing or changed; files preserved.");
}
void syncParent(const std::filesystem::path &path) {
    Descriptor directory(open(path.parent_path().c_str(), O_RDONLY), path.parent_path());
    if (fsync(directory.value) != 0)
        throw std::runtime_error("Cannot sync Move source directory.");
}
void completeMove(Catalog &catalog, const ImportJournalItem &item, bool recovering = false) {
    // Do not cancel between quarantine and deletion. Journal recovery handles a process crash.
    try {
        verified(item.target, item);
        if (!item.backup.empty())
            verified(item.backup, item);
        if (catalog.referencesSource(item.source))
            throw std::runtime_error("Move source is used by this catalog; original retained.");
        if (!recovering && std::filesystem::exists(item.sourceHold))
            throw std::runtime_error("Move source hold path already exists; original retained.");
        if (!std::filesystem::exists(item.sourceHold)) {
            if (!std::filesystem::exists(item.source)) {
                catalog.finishImportMove(item, "done");
                return;
            }
            verified(item.source, item);
            if (renamex_np(item.source.c_str(), item.sourceHold.c_str(), RENAME_EXCL) != 0)
                throw std::runtime_error("Cannot reserve Move source; original retained.");
            syncParent(item.source);
        }
        try {
            verified(item.sourceHold, item);
        } catch (...) {
            // Restore without replacing a newly-created source at the old path.
            renamex_np(item.sourceHold.c_str(), item.source.c_str(), RENAME_EXCL);
            throw;
        }
        if (unlink(item.sourceHold.c_str()) != 0)
            throw std::runtime_error(
                "Cannot finish Move; original retained in the journal's source hold path.");
        syncParent(item.source);
        catalog.finishImportMove(item, "done");
    } catch (const std::exception &failure) {
        // A registered target stays usable. The report identifies preserved source material.
        const auto detail = std::string(failure.what()) + " Source: " + item.source.string() +
                            "; hold: " + item.sourceHold.string();
        catalog.finishImportMove(item, "retained", detail);
    }
}
void validatePlan(const ImportJournal &journal, const ImportJournalItem &item) {
    if (!journal.options || journal.id.size() != 36 ||
        journal.id.find_first_not_of("0123456789abcdef-") != std::string::npos || item.index < 0 ||
        !item.source.is_absolute())
        throw std::runtime_error("Invalid import recovery plan; files preserved.");
    const auto &options = *journal.options;
    if (options.mode == ImportMode::add) {
        if (item.target != item.source || !item.temporary.empty())
            throw std::runtime_error("Invalid Add journal path.");
    } else if (!inside(item.target, options.destination) ||
               item.temporary != item.target.parent_path() / (".luma-copy-" + journal.id + "-" +
                                                              std::to_string(item.index) + ".tmp"))
        throw std::runtime_error("Invalid import destination plan; files preserved.");
    if (options.backupDestination.empty() != item.backup.empty() ||
        (!item.backup.empty() &&
         (!inside(item.backup, options.backupDestination) || item.backup == item.target)))
        throw std::runtime_error("Invalid backup recovery plan; files preserved.");
    const auto expectedHold =
        item.source.parent_path() / (".luma-move-" + journal.id + "-" + std::to_string(item.index));
    if ((options.mode == ImportMode::move && item.sourceHold != expectedHold) ||
        (options.mode != ImportMode::move && !item.sourceHold.empty()))
        throw std::runtime_error("Invalid Move recovery plan; files preserved.");
}
void recoverPlannedImport(Catalog &catalog, const ImportJournal &journal,
                          const ImportJournalItem &item, CancellationToken stop) {
    validatePlan(journal, item);
    const auto &options = *journal.options;
    if (item.state == "committed") {
        completeMove(catalog, item, true);
        return;
    }
    const auto resumeCopy = [&](const std::filesystem::path &destination,
                                const std::filesystem::path &temporary) {
        if (std::filesystem::exists(destination)) {
            verified(destination, item, stop);
            return;
        }
        verified(item.source, item, stop);
        // Never remove an unknown temporary. Recover a verified completed temporary;
        // otherwise preserve it and use an exclusive new name for the restarted copy.
        if (std::filesystem::exists(temporary)) {
            try {
                verified(temporary, item, stop);
                if (link(temporary.c_str(), destination.c_str()) == 0) {
                    syncParent(destination);
                    return;
                }
            } catch (const std::exception &) {
                checkCancellation(stop);
            }
        }
        CopyCleanup copy(destination, temporary.string() + ".resume-" + std::to_string(getpid()),
                         item.checksum);
        copyAndInstall(item.source, copy, stop);
        copy.registered = true;
    };
    if (options.mode != ImportMode::add)
        resumeCopy(item.target, item.temporary);
    else
        verified(item.target, item, stop);
    if (!item.backup.empty())
        resumeCopy(item.backup, backupTemporary(item));
    auto metadata = PhotoDecoder().inspect(item.target, stop);
    metadata.descriptive = decodeImportOptions(item.metadataDocument).metadata;
    metadata.descriptiveSource.clear();
    metadata.sidecarHash.clear();
    checkCancellation(stop);
    const bool inserted =
        catalog.commitImport(item, metadata, journal.skipDuplicates, options.develop);
    // Recovery never removes a published destination whose ownership may be ambiguous.
    if (inserted && options.mode == ImportMode::move)
        completeMove(catalog, item);
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
    options.startedAt = static_cast<std::int64_t>(std::time(nullptr));
    options.validate();
    const auto discovery =
        discover(roots, options.recursive, stop, options.mode != ImportMode::move);
    const auto &sources = discovery.sources;
    if (options.mode != ImportMode::add)
        options.destination = existingDirectory(options.destination);
    if (!options.backupDestination.empty()) {
        options.backupDestination = existingDirectory(options.backupDestination);
        if (options.mode != ImportMode::add && options.backupDestination == options.destination)
            throw std::runtime_error("Backup and import destinations must be different folders.");
    }
    Catalog catalog(catalog_);
    const auto session = catalog.beginImport(options.mode, options.destination, sources, getpid(),
                                             options.skipDuplicates, discovery.failures, options);
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (stop.isCancelled())
            break;
        const auto &source = sources[index];
        if (discovery.failures.contains(source))
            continue;
        bool committed = false;
        try {
            const auto initialTime = std::filesystem::last_write_time(source);
            const auto size = std::filesystem::file_size(source);
            if (size > 512'000'000)
                throw std::runtime_error("Image exceeds the 512 MB import budget.");
            const auto checksum = macos::sha256File(source, stop);
            auto metadata = PhotoDecoder().inspect(source, stop);
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
            if (options.mode != ImportMode::add || !options.backupDestination.empty()) {
                const auto relative =
                    importRelativePath(source, metadata.captureTimestamp, item.index, options);
                chooseImportPaths(item, relative, options);
            }
            if (options.mode == ImportMode::move) {
                if (catalog.referencesSource(source))
                    throw std::runtime_error(
                        "Move source is already used by this catalog. Use Copy instead.");
                if (!metadata.descriptiveError.empty())
                    throw std::runtime_error("Cannot Move while source metadata is unreadable.");
                item.sourceHold =
                    source.parent_path() / (".luma-move-" + session + "-" + std::to_string(index));
            }
            applyImportMetadata(metadata, options);
            ImportOptions metadataSnapshot;
            metadataSnapshot.metadata = metadata.descriptive.value_or(DescriptiveMetadata{});
            item.metadataDocument = encodeImportOptions(metadataSnapshot);
            catalog.stageImportPlan(item);
            CopyCleanup primary(item.target, item.temporary, checksum);
            CopyCleanup backup(item.backup, backupTemporary(item), checksum);
            if (options.mode != ImportMode::add)
                copyAndInstall(source, primary, stop);
            if (!item.backup.empty())
                copyAndInstall(source, backup, stop);
            if (initialTime != std::filesystem::last_write_time(source) ||
                size != std::filesystem::file_size(source) ||
                macos::sha256File(source, stop) != checksum)
                throw std::runtime_error("Original changed during import; source preserved.");
            checkCancellation(stop);
            const bool inserted =
                catalog.commitImport(item, metadata, options.skipDuplicates, options.develop);
            committed = true;
            primary.registered = inserted;
            backup.registered = inserted;
            if (inserted && options.mode == ImportMode::move)
                completeMove(catalog, item);
        } catch (const std::exception &error) {
            if (committed)
                throw; // Keep the session recoverable if post-commit journaling fails.
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
    const auto derivedFolder =
        std::filesystem::weakly_canonical(std::filesystem::absolute(catalog_).parent_path() /
                                          (catalog_.filename().string() + ".luma-derived"));
    for (const auto &record : catalog.pendingDerived()) {
        checkCancellation(stop);
        if (!deadOwner(record.ownerPID))
            continue;
        if (std::filesystem::weakly_canonical(record.path).parent_path() != derivedFolder ||
            record.path.extension() != ".dng")
            throw std::runtime_error(
                "Derived image journal path is outside the managed folder; files preserved.");
        if (!std::filesystem::exists(record.path)) {
            catalog.discardDerived(record.path);
            continue;
        }
        if (!std::filesystem::is_regular_file(record.path) ||
            macos::sha256File(record.path, stop) != record.checksum)
            throw std::runtime_error(
                "Interrupted derived image changed; file and journal preserved.");
        catalog.finishDerived(record);
    }
    (void)StagingFile::cleanup(derivedFolder, ".luma-derived-");
    (void)StagingFile::cleanup(std::filesystem::absolute(catalog_).parent_path() /
                                   (catalog_.filename().string() + ".luma-backups"),
                               ".luma-backup-");
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
                if (journal.options) {
                    recoverPlannedImport(catalog, journal, item, stop);
                    continue;
                }
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
                if (item.state == "committed")
                    catalog.finishImportMove(item, "retained", error.what());
                else
                    catalog.finishImportItem(journal.id, item.index, "failed", error.what());
            }
        }
        catalog.finishImport(journal.id, "interrupted");
        summaries.push_back(catalog.importSummary(journal.id));
    }
    return summaries;
}
} // namespace luma
