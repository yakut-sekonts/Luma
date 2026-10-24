#include "platform/macos/MemoryPressure.h"
#include <dispatch/dispatch.h>
#include <utility>

namespace luma::macos {
struct MemoryPressureMonitor::State {
    dispatch_queue_t queue;
    dispatch_source_t source;
};
MemoryPressureMonitor::MemoryPressureMonitor(std::function<void(MemoryPressure)> callback)
    : state_(std::make_unique<State>()) {
    state_->queue = dispatch_queue_create("app.luma.memory-pressure", DISPATCH_QUEUE_SERIAL);
    state_->source =
        dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0,
                               DISPATCH_MEMORYPRESSURE_NORMAL | DISPATCH_MEMORYPRESSURE_WARN |
                                   DISPATCH_MEMORYPRESSURE_CRITICAL,
                               state_->queue);
    if (!state_->source)
        return;
    auto *state = state_.get();
    dispatch_source_set_event_handler(state_->source, ^{
      const auto flags = dispatch_source_get_data(state->source);
      const auto pressure = flags & DISPATCH_MEMORYPRESSURE_CRITICAL ? MemoryPressure::critical
                            : flags & DISPATCH_MEMORYPRESSURE_WARN   ? MemoryPressure::warning
                                                                     : MemoryPressure::normal;
      callback(pressure);
    });
    dispatch_resume(state_->source);
}
MemoryPressureMonitor::~MemoryPressureMonitor() {
    if (state_->source) {
        dispatch_source_cancel(state_->source);
        // Destruction is on the owner thread. Drain queued handlers before the
        // state they capture is released.
        dispatch_sync(state_->queue, ^{
                      });
    }
}
} // namespace luma::macos
