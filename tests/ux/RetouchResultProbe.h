#pragma once
#ifndef LUMA_UX_RETOUCH_RESULT_PROBE
#error "Retouch result probe is restricted to the UX executable"
#endif
#include "core/edits/Retouch.h"
#include <cstdint>
#include <functional>
#include <memory>
class EditorController;
namespace ux {
struct RetouchResultSnapshot {
    bool held = false, released = false, rejected = false, hookFailed = false;
    unsigned deliveries = 0;
    std::uint64_t revision = 0;
    luma::RetouchStroke stroke;
};
struct RetouchResultState;
// Retains the actual computed donor's original dispatch until release. No fake
// pixels, worker result or stop token is substituted. GUI cancellation stays live.
class RetouchResultGate final {
  public:
    explicit RetouchResultGate(EditorController &editor);
    ~RetouchResultGate() noexcept;
    RetouchResultGate(const RetouchResultGate &) = delete;
    RetouchResultGate &operator=(const RetouchResultGate &) = delete;
    RetouchResultSnapshot snapshot() const;
    void release() noexcept;

  private:
    std::shared_ptr<RetouchResultState> state_;
};
bool deferRetouchSuccess(const EditorController *editor, std::uint64_t revision,
                         const luma::RetouchStroke &stroke,
                         std::function<void()> dispatch) noexcept;
void observeRetouchDelivery(const EditorController *editor, std::uint64_t revision,
                            bool rejected) noexcept;
} // namespace ux
