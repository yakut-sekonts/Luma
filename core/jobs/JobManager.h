#pragma once
#include "core/jobs/Cancellation.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace luma {
enum class JobState { queued, running, completed, cancelled, failed };
enum class JobPriority { background, interactive };
struct JobData {
    luma::CancellationSource cancellation;
    std::atomic<JobState> state{JobState::queued};
    std::atomic<double> progress{0};
    std::mutex mutex;
    std::string error;
};
class JobHandle {
  public:
    explicit JobHandle(std::shared_ptr<JobData> data = {}) : data_(std::move(data)) {}
    void cancel() const {
        if (data_) {
            data_->cancellation.cancel();
        }
    }
    JobState state() const {
        return data_ ? data_->state.load() : JobState::completed;
    }
    double progress() const {
        return data_ ? data_->progress.load() : 1;
    }

  private:
    std::shared_ptr<JobData> data_;
};
struct JobContext {
    std::shared_ptr<JobData> data;
    luma::CancellationToken stopToken() const {
        return data->cancellation.token();
    }
    void setProgress(double value) const;
};
class JobManager {
  public:
    explicit JobManager(std::size_t workers = 1);
    ~JobManager();
    JobHandle submit(JobPriority priority, std::function<void(const JobContext &)> work);
    bool idle() const;
    void shutdown();

  private:
    struct Job {
        std::shared_ptr<JobData> data;
        std::function<void(const JobContext &)> work;
    };
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Job> queue_;
    std::vector<std::shared_ptr<JobData>> active_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
    void run();
};
} // namespace luma
