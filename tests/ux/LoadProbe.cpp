#include "tests/ux/LoadProbe.h"
#include "apps/desktop/EditorController.h"
#include <QThread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace ux {
struct LoadProbeState {
    explicit LoadProbeState(const EditorController *target) : controller(target) {}
    const EditorController *controller;
    mutable std::mutex mutex;
    std::condition_variable condition;
    LoadProbeSnapshot observed;
    std::atomic_bool hookFailed{false};
    unsigned initialGlobalFailures = 0;
};
namespace {
std::mutex registryMutex;
std::weak_ptr<LoadProbeState> activeGate;
std::atomic_uint globalFailures{0};
std::shared_ptr<LoadProbeState> current(const LoadOperation &operation) {
    std::lock_guard lock(registryMutex);
    const auto state = activeGate.lock();
    return state && state->controller == operation.controller ? state : nullptr;
}
} // namespace

bool LoadOperation::sameAs(const LoadOperation &other) const noexcept {
    return controller == other.controller && revision == other.revision &&
           assetID == other.assetID && sourcePath == other.sourcePath &&
           job.get() == other.job.get();
}
LoadResultGate::LoadResultGate(EditorController &editor)
    : state_(std::make_shared<LoadProbeState>(&editor)) {
    if (QThread::currentThread() != editor.thread())
        throw std::logic_error("Load result gate must be owned by the editor GUI thread");
    std::lock_guard lock(registryMutex);
    if (!activeGate.expired())
        throw std::logic_error("Load result gate cannot overlap another gate");
    state_->initialGlobalFailures = globalFailures.load();
    activeGate = state_;
}
LoadResultGate::~LoadResultGate() noexcept {
    // Never leave the worker waiting when an assertion unwinds before release.
    // The worker owns a shared State until it leaves the hold; no stack captures.
    release();
    try {
        std::lock_guard lock(registryMutex);
        if (activeGate.lock() == state_)
            activeGate.reset();
    } catch (...) {
        state_->hookFailed.store(true);
        ++globalFailures;
    }
}
LoadProbeSnapshot LoadResultGate::snapshot() const {
    std::lock_guard lock(state_->mutex);
    auto result = state_->observed;
    result.hookFailed =
        state_->hookFailed.load() || globalFailures.load() != state_->initialGlobalFailures;
    return result;
}
void LoadResultGate::release() noexcept {
    try {
        {
            std::lock_guard lock(state_->mutex);
            state_->observed.released = true;
        }
        state_->condition.notify_all();
    } catch (...) {
        state_->hookFailed.store(true);
        ++globalFailures;
        state_->condition.notify_all();
    }
}
void holdLoadSuccess(const LoadOperation &operation) noexcept {
    std::shared_ptr<LoadProbeState> state;
    try {
        state = current(operation);
        if (!state)
            return;
        std::unique_lock lock(state->mutex);
        ++state->observed.successfulArrivals;
        if (state->observed.held || !operation.job || operation.assetID.empty() ||
            operation.sourcePath.empty()) {
            state->hookFailed.store(true);
            return;
        }
        state->observed.held = operation;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        if (!state->condition.wait_until(lock, deadline,
                                         [&] { return state->observed.released; })) {
            state->observed.timedOut = true;
            state->observed.released = true;
        }
        state->observed.leftHold = true;
    } catch (...) {
        if (state)
            state->hookFailed.store(true);
        ++globalFailures;
    }
}
void observeLoadSuccessDelivery(const LoadOperation &operation) noexcept {
    std::shared_ptr<LoadProbeState> state;
    try {
        state = current(operation);
        if (!state)
            return;
        std::lock_guard lock(state->mutex);
        ++state->observed.deliveries;
        state->observed.lastDelivered = operation;
        if (!state->observed.released || !state->observed.leftHold)
            state->observed.deliveryBeforeRelease = true;
        if (!state->observed.held || !state->observed.held->sameAs(operation))
            ++state->observed.mismatchedDeliveries;
    } catch (...) {
        if (state)
            state->hookFailed.store(true);
        ++globalFailures;
    }
}
} // namespace ux
