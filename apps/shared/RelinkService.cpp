#include "apps/shared/RelinkService.h"
#include "core/image/Bayer.h"
#include "platform/macos/FileHasher.h"

namespace luma {
namespace {
bool missing(const std::filesystem::path &path) {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    return !exists && !error;
}
std::optional<RelinkRecord> candidate(const FileRecord &source, const std::filesystem::path &path,
                                      CancellationToken stop) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error ||
        std::filesystem::file_size(path, error) != static_cast<std::uintmax_t>(source.size) ||
        error)
        return std::nullopt;
    const auto canonical = std::filesystem::canonical(path);
    const auto time = std::filesystem::last_write_time(canonical);
    if (macos::sha256File(canonical, stop) != source.checksum ||
        std::filesystem::last_write_time(canonical) != time)
        return std::nullopt;
    return RelinkRecord{source, canonical};
}
} // namespace
void RelinkService::locate(const std::string &id, const std::filesystem::path &file,
                           CancellationToken stop) const {
    Catalog catalog(catalog_);
    const auto source = catalog.fileRecord(id);
    if (!source)
        throw std::runtime_error("Photo disappeared while locating its original.");
    if (!missing(source->path))
        throw std::runtime_error(
            "Locate only relinks a missing original; an existing original is never replaced.");
    const auto record = candidate(*source, file, stop);
    if (!record)
        throw std::runtime_error(
            "Selected file does not match the original checksum and size. Edits are preserved.");
    checkCancellation(stop);
    catalog.relink({*record});
}
RelinkSummary RelinkService::locateFolder(const std::filesystem::path &oldRoot,
                                          const std::filesystem::path &newRoot, bool nearby,
                                          CancellationToken stop) const {
    const auto old = std::filesystem::absolute(oldRoot).lexically_normal();
    const auto replacement = std::filesystem::canonical(newRoot);
    if (!std::filesystem::is_directory(replacement))
        throw std::runtime_error("Locate Folder requires a folder.");
    Catalog catalog(catalog_);
    RelinkSummary summary;
    std::string after;
    for (;;) {
        checkCancellation(stop);
        const auto page = catalog.fileRecords(after);
        if (page.empty())
            break;
        std::vector<RelinkRecord> updates;
        for (const auto &source : page) {
            checkCancellation(stop);
            const auto relative = source.path.lexically_relative(old);
            if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
                !missing(source.path))
                continue;
            auto record = candidate(source, replacement / relative, stop);
            if (!record && nearby)
                record = candidate(source, replacement / source.path.filename(), stop);
            if (record)
                updates.push_back(*record);
            else
                ++summary.unresolved;
        }
        checkCancellation(stop);
        catalog.relink(updates);
        summary.relinked += updates.size();
        after = page.back().id;
    }
    return summary;
}
} // namespace luma
