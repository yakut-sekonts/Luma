#pragma once
#include "core/jobs/Cancellation.h"
#include <filesystem>
#include <functional>
#include <optional>

namespace luma {
enum class BackupSchedule { manual, everyLaunch, daily, weekly };
class BackupService {
  public:
    explicit BackupService(std::filesystem::path catalog) : catalog_(std::move(catalog)) {}
    std::optional<std::filesystem::path>
    run(BackupSchedule schedule, int keep, bool force, CancellationToken stop = {},
        const std::function<void(double)> &progress = {}) const;

  private:
    std::filesystem::path catalog_;
};
} // namespace luma
