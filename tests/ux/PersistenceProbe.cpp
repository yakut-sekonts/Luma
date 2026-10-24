#include "tests/ux/PersistenceProbe.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <utility>
namespace ux {
struct PersistenceState {
    std::filesystem::path catalog;
    std::vector<std::string> points;
    mutable std::mutex mutex;
    std::condition_variable changed;
    PersistenceSnapshot observed;
    unsigned initialFailures = 0;
    std::size_t releasedThrough = 0;
    bool closing = false;
};
namespace {
std::mutex registryMutex;
std::weak_ptr<PersistenceState> active;
std::atomic_uint failures{0};
std::shared_ptr<PersistenceState> current(const std::filesystem::path &catalog) {
    std::lock_guard lock(registryMutex);
    auto state = active.lock();
    return state && state->catalog == catalog ? state : nullptr;
}
} // namespace
PersistenceGate::PersistenceGate(std::filesystem::path catalog, std::vector<std::string> points)
    : state_(std::make_shared<PersistenceState>()) {
    if (!catalog.is_absolute() || std::filesystem::canonical(catalog) != catalog || points.empty())
        throw std::logic_error("Persistence probe requires a canonical owned catalog and points");
    std::lock_guard lock(registryMutex);
    if (!active.expired())
        throw std::logic_error("Persistence gates cannot overlap");
    state_->catalog = std::move(catalog);
    state_->points = std::move(points);
    state_->initialFailures = failures.load();
    active = state_;
}
PersistenceGate::~PersistenceGate() noexcept {
    try {
        {
            std::lock_guard lock(state_->mutex);
            state_->closing = true;
        }
        state_->changed.notify_all();
        std::lock_guard lock(registryMutex);
        if (active.lock() == state_)
            active.reset();
    } catch (...) {
        ++failures;
    }
}
PersistenceSnapshot PersistenceGate::snapshot() const {
    std::lock_guard lock(state_->mutex);
    auto observed = state_->observed;
    observed.hookFailed = failures.load() != state_->initialFailures;
    return observed;
}
void PersistenceGate::release() noexcept {
    try {
        {
            std::lock_guard lock(state_->mutex);
            state_->releasedThrough = state_->observed.events.size();
        }
        state_->changed.notify_all();
    } catch (...) {
        ++failures;
    }
}
void observePersistenceCheckpoint(const std::filesystem::path &catalog, std::string_view point,
                                  const std::filesystem::path &target,
                                  const std::filesystem::path &temporary) noexcept {
    try {
        const auto state = current(catalog);
        if (!state)
            return;
        std::unique_lock lock(state->mutex);
        if (state->closing ||
            std::find(state->points.begin(), state->points.end(), point) == state->points.end())
            return;
        const auto index = state->observed.events.size();
        if (index >= state->points.size() || state->points[index] != point) {
            ++failures;
            return;
        }
        state->observed.events.push_back({std::string(point), target, temporary});
        state->observed.held = true;
        state->changed.wait(lock, [&] { return state->closing || state->releasedThrough > index; });
        state->observed.held = false;
    } catch (...) {
        ++failures;
    }
}
} // namespace ux
