#include "tests/ux/PreviewResultProbe.h"
#include "apps/desktop/EditorController.h"
#include <QThread>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace ux {
struct PreviewResultState {
    const EditorController *controller;
    int resolution;
    mutable std::mutex mutex;
    PreviewResultSnapshot observed;
    std::function<void()> dispatch;
    unsigned initialFailures = 0;
};
namespace {
std::mutex registryMutex;
std::weak_ptr<PreviewResultState> activeGate;
std::atomic_uint failures{0};
std::shared_ptr<PreviewResultState> current(const PreviewOperation &operation) {
    std::lock_guard lock(registryMutex);
    const auto state = activeGate.lock();
    return state && state->controller == operation.controller ? state : nullptr;
}
} // namespace
bool PreviewOperation::sameAs(const PreviewOperation &other) const noexcept {
    return controller == other.controller && revision == other.revision &&
           resolution == other.resolution && assetID == other.assetID && job == other.job;
}
PreviewResultGate::PreviewResultGate(EditorController &editor, int resolution)
    : state_(std::make_shared<PreviewResultState>()) {
    if (QThread::currentThread() != editor.thread())
        throw std::logic_error("Preview result gate must be owned by the editor GUI thread");
    std::lock_guard lock(registryMutex);
    if (!activeGate.expired())
        throw std::logic_error("Preview result gates cannot overlap");
    state_->controller = &editor;
    state_->resolution = resolution;
    state_->initialFailures = failures.load();
    activeGate = state_;
}
PreviewResultGate::~PreviewResultGate() noexcept {
    release();
    try {
        std::lock_guard lock(registryMutex);
        if (activeGate.lock() == state_)
            activeGate.reset();
    } catch (...) {
        ++failures;
    }
}
PreviewResultSnapshot PreviewResultGate::snapshot() const {
    std::lock_guard lock(state_->mutex);
    auto result = state_->observed;
    result.hookFailed = failures.load() != state_->initialFailures;
    return result;
}
void PreviewResultGate::release() noexcept {
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
bool deferPreviewSuccess(const PreviewOperation &operation, const QImage &pixels,
                         std::function<void()> dispatch) noexcept {
    try {
        const auto state = current(operation);
        if (!state || state->resolution != operation.resolution)
            return false;
        std::lock_guard lock(state->mutex);
        if (state->observed.held || state->observed.released)
            return false;
        if (!operation.job || operation.assetID.empty() || pixels.isNull() || !dispatch) {
            ++failures;
            return false;
        }
        state->observed.held = operation;
        state->observed.pixels = pixels;
        state->dispatch = std::move(dispatch);
        return true;
    } catch (...) {
        ++failures;
        return false;
    }
}
void observePreviewDelivery(const PreviewOperation &operation) noexcept {
    try {
        const auto state = current(operation);
        if (!state)
            return;
        std::lock_guard lock(state->mutex);
        if (!state->observed.held || !state->observed.held->sameAs(operation))
            return;
        ++state->observed.deliveries;
        state->observed.delivered = operation;
        state->observed.deliveryBeforeRelease |= !state->observed.released;
    } catch (...) {
        ++failures;
    }
}
void observePreviewRejection(const PreviewOperation &operation,
                             std::uint64_t currentRevision) noexcept {
    try {
        const auto state = current(operation);
        if (!state)
            return;
        std::lock_guard lock(state->mutex);
        if (!state->observed.held || !state->observed.held->sameAs(operation))
            return;
        ++state->observed.rejections;
        state->observed.rejectionRevision = currentRevision;
    } catch (...) {
        ++failures;
    }
}
} // namespace ux
