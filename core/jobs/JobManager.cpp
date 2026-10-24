#include "core/jobs/JobManager.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace luma {
void JobContext::setProgress(double value) const {
    if (std::isfinite(value)) {
        data->progress.store(std::clamp(value, 0.0, 1.0));
    }
}
JobManager::JobManager(std::size_t workers) {
    if (workers < 1 || workers > 4) {
        throw std::runtime_error("Job worker count must be between 1 and 4.");
    }
    for (std::size_t i = 0; i < workers; ++i) {
        workers_.emplace_back([this] { run(); });
    }
}
JobManager::~JobManager() {
    shutdown();
}
JobHandle JobManager::submit(JobPriority priority, std::function<void(const JobContext &)> work) {
    auto data = std::make_shared<JobData>();
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("Job system is shutting down.");
        }
        std::erase_if(queue_, [](const Job &job) {
            if (!job.data->cancellation.isCancelled()) {
                return false;
            }
            job.data->state.store(JobState::cancelled);
            return true;
        });
        if (queue_.size() >= 64) {
            throw std::runtime_error("Background job queue is full.");
        }
        Job job{data, std::move(work)};
        if (priority == JobPriority::interactive) {
            queue_.push_front(std::move(job));
        } else {
            queue_.push_back(std::move(job));
        }
    }
    condition_.notify_one();
    return JobHandle(data);
}
void JobManager::run() {
    while (true) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) {
                return;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
            active_.push_back(job.data);
        }
        if (!job.data->cancellation.isCancelled()) {
            job.data->state.store(JobState::running);
            try {
                job.work(JobContext{job.data});
                job.data->progress.store(1);
                job.data->state.store(job.data->cancellation.isCancelled() ? JobState::cancelled
                                                                           : JobState::completed);
            } catch (const std::exception &error) {
                std::lock_guard lock(job.data->mutex);
                job.data->error = error.what();
                job.data->state.store(job.data->cancellation.isCancelled() ? JobState::cancelled
                                                                           : JobState::failed);
            } catch (...) {
                job.data->state.store(JobState::failed);
            }
        } else {
            job.data->state.store(JobState::cancelled);
        }
        std::lock_guard lock(mutex_);
        std::erase(active_, job.data);
    }
}
bool JobManager::idle() const {
    std::lock_guard lock(mutex_);
    return queue_.empty() && active_.empty();
}
void JobManager::shutdown() {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
        for (const auto &data : active_) {
            data->cancellation.cancel();
        }
        for (const auto &job : queue_) {
            job.data->cancellation.cancel();
            job.data->state.store(JobState::cancelled);
        }
        queue_.clear();
    }
    condition_.notify_all();
    for (auto &worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}
} // namespace luma
