#pragma once
#ifndef LUMA_UX_PERSISTENCE_PROBE
#error "Persistence observation is restricted to the UX executable"
#endif
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace ux {
struct PersistenceEvent {
    std::string point;
    std::filesystem::path target, temporary;
};
struct PersistenceSnapshot {
    std::vector<PersistenceEvent> events;
    bool held = false, hookFailed = false;
};
struct PersistenceState;
// Holds a real I/O boundary on a worker, restricted to one owned test catalog.
// All writes, links, renames and transactions still use production code.
class PersistenceGate final {
  public:
    PersistenceGate(std::filesystem::path catalog, std::vector<std::string> points);
    ~PersistenceGate() noexcept;
    PersistenceGate(const PersistenceGate &) = delete;
    PersistenceGate &operator=(const PersistenceGate &) = delete;
    PersistenceSnapshot snapshot() const;
    void release() noexcept;

  private:
    std::shared_ptr<PersistenceState> state_;
};
void observePersistenceCheckpoint(const std::filesystem::path &catalog, std::string_view point,
                                  const std::filesystem::path &target,
                                  const std::filesystem::path &temporary) noexcept;
} // namespace ux
