#include "trinity/jobs/JobWorker.hpp"

#include "trinity/core/Logger.hpp"

namespace trinity::jobs {

JobWorker::JobWorker(std::shared_ptr<JobManager> jobs, int workerCount)
    : jobs_(std::move(jobs)), workerCount_(workerCount <= 0 ? 1 : workerCount) {
}

JobWorker::~JobWorker() {
    stop();
}

void JobWorker::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return;
    }
    running_ = true;
    stopRequested_ = false;
    for (int i = 0; i < workerCount_; ++i) {
        threads_.emplace_back([this] { loop(); });
    }
    core::Logger::instance().info(
        "jobs", "job worker started", core::Json{{"threads", workerCount_}});
}

void JobWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        stopRequested_ = true;
    }
    cv_.notify_all();
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    threads_.clear();
    running_ = false;
    core::Logger::instance().info("jobs", "job worker stopped", core::Json::object());
}

bool JobWorker::running() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

std::string JobWorker::submit(const std::string& engine, const std::string& operation,
                              const core::Json& params, const JobCreateOptions& options) {
    Job job = jobs_->createJob(engine, operation, params, options);
    submitExisting(job.jobId);
    return job.jobId;
}

void JobWorker::submitExisting(const std::string& jobId) {
    auto token = std::make_shared<CancellationToken>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        liveTokens_[jobId] = token;
        queue_.push_back(QueuedTask{jobId, token});
    }
    cv_.notify_one();
}

bool JobWorker::cancel(const std::string& jobId) {
    std::shared_ptr<CancellationToken> token;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = liveTokens_.find(jobId);
        if (it != liveTokens_.end()) {
            token = it->second;
        }
        // Remove from queue if still waiting (reliable queued cancel).
        for (auto qit = queue_.begin(); qit != queue_.end(); ++qit) {
            if (qit->jobId == jobId) {
                queue_.erase(qit);
                break;
            }
        }
    }
    if (token) {
        token->cancel();
    }
    // JobManager marks QUEUED->CANCELLED or records RUNNING request.
    return jobs_->cancel(jobId);
}

size_t JobWorker::queueDepth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void JobWorker::loop() {
    for (;;) {
        QueuedTask task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopRequested_ || !queue_.empty(); });
            if (stopRequested_ && queue_.empty()) {
                return;
            }
            if (queue_.empty()) {
                continue;
            }
            task = queue_.front();
            queue_.pop_front();
        }
        try {
            jobs_->executeJob(task.jobId, task.token);
        } catch (const std::exception& exc) {
            core::Logger::instance().warning(
                "jobs", "worker task threw",
                core::Json{{"job_id", task.jobId}, {"error", exc.what()}});
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            liveTokens_.erase(task.jobId);
        }
    }
}

}  // namespace trinity::jobs
