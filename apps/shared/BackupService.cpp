#include "apps/shared/BackupService.h"
#include "core/catalog/Catalog.h"
#include "core/catalog/Identity.h"
#include "core/image/Bayer.h"
#include <algorithm>
#include <chrono>
#include <vector>

namespace luma {
std::optional<std::filesystem::path>
BackupService::run(BackupSchedule schedule, int keep, bool force, CancellationToken stop,
                   const std::function<void(double)> &progress) const {
    if (keep < 1 || keep > 50 || schedule < BackupSchedule::manual ||
        schedule > BackupSchedule::weekly)
        throw std::runtime_error("Invalid catalog backup preferences.");
    if (!force && schedule == BackupSchedule::manual)
        return std::nullopt;
    const auto folder = std::filesystem::absolute(catalog_).parent_path() /
                        (catalog_.filename().string() + ".luma-backups");
    std::filesystem::create_directories(folder);
    struct Saved {
        std::filesystem::path path;
        std::filesystem::file_time_type time;
    };
    std::vector<Saved> saved;
    for (const auto &entry : std::filesystem::directory_iterator(folder)) {
        checkCancellation(stop);
        const auto name = entry.path().filename().string();
        if (!entry.is_symlink() && entry.is_regular_file() && name.starts_with("auto-") &&
            name.size() == 49 && entry.path().extension() == ".sqlite3")
            saved.push_back({entry.path(), entry.last_write_time()});
    }
    std::sort(saved.begin(), saved.end(), [](const auto &a, const auto &b) {
        return a.time != b.time ? a.time > b.time : a.path < b.path;
    });
    if (!force && !saved.empty() &&
        (schedule == BackupSchedule::daily || schedule == BackupSchedule::weekly)) {
        const auto age = std::filesystem::file_time_type::clock::now() - saved.front().time;
        const auto interval = std::chrono::hours(schedule == BackupSchedule::daily ? 24 : 168);
        if (age >= std::chrono::hours(0) && age < interval)
            return std::nullopt;
    }
    const auto target = folder / ("auto-" + newID() + ".sqlite3");
    Catalog(catalog_).backupTo(target, stop, progress);
    // Never prune the last good copy in favor of an unreadable new snapshot.
    Catalog::verifyBackup(target, stop);
    for (std::size_t index = static_cast<std::size_t>(keep - 1); index < saved.size(); ++index) {
        checkCancellation(stop);
        // Preserve corrupt copies too: they can contain evidence or recoverable data.
        Catalog::verifyBackup(saved[index].path, stop);
        std::filesystem::remove(saved[index].path);
    }
    return target;
}
} // namespace luma
