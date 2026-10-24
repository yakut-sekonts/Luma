#pragma once
#include "core/jobs/JobManager.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class EditorController;

namespace ux {
// Test-target-only observation. No production QObject properties or ABI changes.
struct LoadOperation {
    const EditorController *controller = nullptr;
    std::uint64_t revision = 0;
    std::string assetID, sourcePath;
    std::shared_ptr<const luma::JobData> job;
    bool sameAs(const LoadOperation &other) const noexcept;
};
struct LoadProbeSnapshot {
    std::optional<LoadOperation> held;
    std::optional<LoadOperation> lastDelivered;
    unsigned successfulArrivals = 0, deliveries = 0, mismatchedDeliveries = 0;
    bool released = false, leftHold = false, timedOut = false;
    bool deliveryBeforeRelease = false, hookFailed = false;
};
struct LoadProbeState;
class LoadResultGate final {
  public:
    explicit LoadResultGate(EditorController &editor);
    ~LoadResultGate() noexcept;
    LoadResultGate(const LoadResultGate &) = delete;
    LoadResultGate &operator=(const LoadResultGate &) = delete;
    LoadProbeSnapshot snapshot() const;
    void release() noexcept;

  private:
    std::shared_ptr<LoadProbeState> state_;
};

// Runs on the actual load worker after SessionService returned a PhotoSession,
// before the original queued success delivery. Bounded to 30s and RAII-released.
void holdLoadSuccess(const LoadOperation &operation) noexcept;
// Passive observation at entry to the unchanged original success lambda.
void observeLoadSuccessDelivery(const LoadOperation &operation) noexcept;
} // namespace ux
