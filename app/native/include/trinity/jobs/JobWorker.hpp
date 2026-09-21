#pragma once

// Background job worker (§10, Decision 1).
// Keeps execution off the Qt UI thread: submit() creates a QUEUED job row
// and returns immediately; a single worker thread (extensible to a pool
// via workerCount) runs JobManager::executeJob and records results.
// UI polls JobManager::get/listRecent via QTimer.

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../core/Json.hpp"
#include "Job.hpp"

namespace trinity::jobs {

struct QueuedTask {
    std::string jobId;
    std::shared_ptr<CancellationToken> token;
};

class JobWorker {
public:
    explicit JobWorker(std::shared_ptr<JobManager> jobs, int workerCount = 1);
    ~JobWorker();

    JobWorker(const JobWorker&) = delete;
    JobWorker& operator=(const JobWorker&) = delete;

    void start();
    void stop();
    bool running() const noexcept;

    // Create a QUEUED job and enqueue it. Returns jobId immediately.
    std::string submit(const std::string& engine, const std::string& operation,
                       const core::Json& params, const JobCreateOptions& options = {});
    // Enqueue an already-created QUEUED job.
    void submitExisting(const std::string& jobId);
    // Reliable cancel for queued; cooperative request for running.
    bool cancel(const std::string& jobId);
    size_t queueDepth() const;

private:
    void loop();

    std::shared_ptr<JobManager> jobs_;
    int workerCount_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<QueuedTask> queue_;
    std::vector<std::thread> threads_;
    std::map<std::string, std::shared_ptr<CancellationToken>> liveTokens_;
    bool running_ = false;
    bool stopRequested_ = false;
};

}  // namespace trinity::jobs
