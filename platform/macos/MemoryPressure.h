#pragma once
#include <functional>
#include <memory>

namespace luma::macos {
enum class MemoryPressure { normal, warning, critical };
class MemoryPressureMonitor {
  public:
    // Delivered serially on a dedicated dispatch queue, never the UI thread.
    explicit MemoryPressureMonitor(std::function<void(MemoryPressure)> callback);
    ~MemoryPressureMonitor();
    MemoryPressureMonitor(const MemoryPressureMonitor &) = delete;
    MemoryPressureMonitor &operator=(const MemoryPressureMonitor &) = delete;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace luma::macos
