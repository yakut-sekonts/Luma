#pragma once
#include <QString>
#include <QUrl>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

class QLockFile;
namespace luma::desktop {
enum class LogLevel { trace, debug, info, warning, error };
enum class LogEvent {
    sessionStarted,
    sessionFinished,
    sessionFailed,
    catalogReady,
    recoveryRequested,
    crashCaptureUnavailable,
    historyUnavailable,
    operationStarted,
    operationFinished,
    operationFailed
};
enum class LoggedWorkflow { application, library, editor, exportQueue, count };

// No free-form text enters the persistent log. Files and reports stay local.
class DiagnosticSession final {
  public:
    explicit DiagnosticSession(bool enabled);
    ~DiagnosticSession();
    DiagnosticSession(const DiagnosticSession &) = delete;
    DiagnosticSession &operator=(const DiagnosticSession &) = delete;
    void record(LogLevel level, LogEvent event,
                LoggedWorkflow workflow = LoggedWorkflow::application) noexcept;
    void observe(LoggedWorkflow workflow, bool busy, bool error) noexcept;
    void markCleanExit() {
        cleanExit_ = true;
    }
    QUrl folder() const {
        return root_.isEmpty() ? QUrl{} : QUrl::fromLocalFile(root_);
    }
    bool loggingAvailable() const {
        return logAvailable_.load();
    }
    bool crashCaptureEnabled() const {
        return crashCaptureEnabled_;
    }
    std::uint64_t droppedEvents() const {
        return dropped_.load();
    }

  private:
    struct Event {
        LogLevel level;
        LogEvent event;
        LoggedWorkflow workflow;
        std::int64_t milliseconds;
    };
    QString root_, session_, version_;
    std::unique_ptr<QLockFile> lock_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Event> pending_;
    // Xcode 16's libc++ does not provide jthread/stop_token. Shutdown is guarded
    // by mutex_ and the destructor joins after the queued events are drained.
    std::thread writer_;
    bool stopRequested_ = false;
    std::array<std::array<bool, 2>, static_cast<std::size_t>(LoggedWorkflow::count)> states_{};
    std::atomic_bool enabled_{false}, logAvailable_{false};
    std::atomic<std::uint64_t> dropped_{0};
    bool cleanExit_ = false, crashCaptureEnabled_ = false;
    void maintainHistory();
    void writeLoop() noexcept;
};
} // namespace luma::desktop
