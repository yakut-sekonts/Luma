#include "tests/ux/RetouchResultProbe.h"
#include "apps/desktop/EditorController.h"
#include <QThread>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <utility>
namespace ux {
struct RetouchResultState {
    const EditorController *editor = nullptr;
    mutable std::mutex mutex;
    RetouchResultSnapshot observed;
    unsigned initialFailures = 0;
    std::function<void()> dispatch;
};
namespace {
std::mutex registryMutex;
std::atomic_uint failures{0};
std::weak_ptr<RetouchResultState> active;
std::shared_ptr<RetouchResultState> current(const EditorController *editor) {
    std::lock_guard lock(registryMutex);
    auto state = active.lock();
    return state && state->editor == editor ? state : nullptr;
}
} // namespace
RetouchResultGate::RetouchResultGate(EditorController &editor)
    : state_(std::make_shared<RetouchResultState>()) {
    if (QThread::currentThread() != editor.thread())
        throw std::logic_error("Retouch gate must be owned by the GUI thread");
    std::lock_guard lock(registryMutex);
    if (!active.expired())
        throw std::logic_error("Retouch gates cannot overlap");
    state_->editor = &editor;
    state_->initialFailures = failures.load();
    active = state_;
}
RetouchResultGate::~RetouchResultGate() noexcept {
    release();
    try {
        std::lock_guard lock(registryMutex);
        if (active.lock() == state_)
            active.reset();
    } catch (...) {
        ++failures;
    }
}
RetouchResultSnapshot RetouchResultGate::snapshot() const {
    std::lock_guard lock(state_->mutex);
    auto result = state_->observed;
    result.hookFailed = failures.load() != state_->initialFailures;
    return result;
}
void RetouchResultGate::release() noexcept {
    try {
        std::function<void()> dispatch;
        {
            std::lock_guard lock(state_->mutex);
            state_->observed.released = true;
            dispatch = std::move(state_->dispatch);
        }
        if (dispatch)
            dispatch();
    } catch (...) {
        ++failures;
    }
}
bool deferRetouchSuccess(const EditorController *editor, std::uint64_t revision,
                         const luma::RetouchStroke &stroke,
                         std::function<void()> dispatch) noexcept {
    try {
        const auto state = current(editor);
        if (!state)
            return false;
        std::lock_guard lock(state->mutex);
        if (state->observed.held || state->observed.released)
            return false;
        state->observed.held = true;
        state->observed.revision = revision;
        state->observed.stroke = stroke;
        state->dispatch = std::move(dispatch);
        return true;
    } catch (...) {
        ++failures;
        return false;
    }
}
void observeRetouchDelivery(const EditorController *editor, std::uint64_t revision,
                            bool rejected) noexcept {
    try {
        const auto state = current(editor);
        if (!state)
            return;
        std::lock_guard lock(state->mutex);
        if (!state->observed.held || state->observed.revision != revision)
            return;
        ++state->observed.deliveries;
        state->observed.rejected = rejected;
    } catch (...) {
        ++failures;
    }
}
} // namespace ux
