// Trinity — asynchronous job system (brief §Job System, §Jobs must never block
// the UI).
//
// States:
//   QUEUED -> RUNNING -> PAUSED -> RUNNING -> COMPLETED | FAILED | CANCELLED
// Artifacts additionally carry VALIDATING / VERIFIED via the validation layer.
//
// Every job has: uuid, timestamps, project, engine, input, output, logs,
// duration, status, error, artifacts. Jobs run on a worker thread pool; the UI
// thread never runs job code — it observes progress through EventBus events or
// the (optional) callbacks.
//
// Threading contract (enforced throughout the implementation):
//   - every public method is safe to call from any thread;
//   - user callbacks and event handlers are ALWAYS invoked with no internal
//     lock held, so a callback may call straight back into the job system
//     (query a record, cancel a sibling job) without deadlocking.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../artifacts/Artifact.hpp"
#include "../core/EventBus.hpp"
#include "../core/Json.hpp"
#include "../db/Database.hpp"

namespace trinity::jobs {

enum class JobState {
    Queued,
    Running,
    Paused,
    Completed,
    Failed,
    Cancelled,
};

const char* job_state_string(JobState state);
core::Result<JobState> job_state_from_string(const std::string& text);

// True for COMPLETED | FAILED | CANCELLED (no further transitions possible).
bool job_state_is_terminal(JobState state);

struct JobRecord {
    std::string job_id;
    std::string project_id;
    std::string engine;
    std::string operation;
    JobState status = JobState::Queued;
    double progress = 0.0;                 // 0..1
    core::Json request = core::Json::object();
    core::Json output = core::Json::object();
    std::vector<std::string> logs;
    std::int64_t duration_ms = 0;
    std::optional<core::Error> error;
    std::vector<artifacts::Artifact> artifacts;
    std::string created_at;
    std::string updated_at;

    core::Json to_json(bool include_logs = true) const;
};

// Progress/log callbacks receive copies; they must not block.
using ProgressFn = std::function<void(const std::string& job_id, double progress)>;
using LogFn = std::function<void(const std::string& job_id, const std::string& line)>;
// Fired once when a job reaches a terminal state.
using StateFn = std::function<void(const JobRecord& record)>;

// ------------------------------------------------------------- interfaces

// Canonical single-job handle (brief §Core interfaces: IJob). A thin, cheap
// view over one job owned by a manager; safe to copy and pass to the UI.
class IJob {
public:
    virtual ~IJob() = default;

    virtual std::string job_id() const = 0;
    virtual std::string engine() const = 0;
    virtual std::string operation() const = 0;
    virtual JobState state() const = 0;
    virtual bool terminal() const = 0;
    virtual core::Result<JobRecord> record() const = 0;
    virtual core::Status pause() = 0;
    virtual core::Status resume() = 0;
    virtual core::Status cancel() = 0;
};

// Execution context handed to job functions: report progress, append logs,
// and honour pause/cancel cooperatively. Every method locks internally.
class JobContext {
public:
    JobContext(std::string job_id, class JobSystem* system);

    const std::string& job_id() const { return job_id_; }

    void report_progress(double progress);              // clamps to [0,1]
    void log(const std::string& line);
    void log(const std::string& line, const core::Json& context);

    // False once the job has been cancelled; blocks while the job is paused.
    bool should_run();

private:
    std::string job_id_;
    JobSystem* system_;
};

// A job function performs the actual work. It receives the context and the
// request payload, and returns the output payload. Throwing
// core::TrinityException converts to a FAILED job carrying that error.
using JobFn = std::function<core::Json(JobContext& context, const core::Json& request)>;

// Canonical job-manager interface (brief §Core interfaces: IJob). Implemented
// by JobSystem and consumed by the UI/command layers, so a future out-of-process
// executor is a drop-in replacement.
class IJobManager {
public:
    virtual ~IJobManager() = default;

    virtual core::Result<std::string> submit(const std::string& engine, const std::string& operation,
                                             const core::Json& request, JobFn fn,
                                             const std::string& project_id = "") = 0;

    virtual core::Result<JobRecord> get(const std::string& job_id) const = 0;
    virtual core::Result<std::vector<JobRecord>> list_recent(std::size_t limit = 50) const = 0;

    virtual core::Status pause(const std::string& job_id) = 0;
    virtual core::Status resume(const std::string& job_id) = 0;
    virtual core::Status cancel(const std::string& job_id) = 0;

    virtual void wait_for_idle() = 0;
    virtual std::size_t queued_count() const = 0;
    virtual std::size_t active_count() const = 0;
};

// --------------------------------------------------------------- JobHandle

// Concrete IJob over any IJobManager. Each call queries the live record, so a
// handle never serves stale status.
class JobHandle : public IJob {
public:
    JobHandle(IJobManager* manager, std::string job_id)
        : manager_(manager), job_id_(std::move(job_id)) {}

    std::string job_id() const override { return job_id_; }
    std::string engine() const override { return snapshot().engine; }
    std::string operation() const override { return snapshot().operation; }
    JobState state() const override { return snapshot().status; }
    bool terminal() const override { return job_state_is_terminal(snapshot().status); }
    core::Result<JobRecord> record() const override { return snapshot(); }

    core::Status pause() override { return manager_ == nullptr
                                                ? core::Status::fail(no_manager())
                                                : manager_->pause(job_id_); }
    core::Status resume() override { return manager_ == nullptr
                                                 ? core::Status::fail(no_manager())
                                                 : manager_->resume(job_id_); }
    core::Status cancel() override { return manager_ == nullptr
                                                 ? core::Status::fail(no_manager())
                                                 : manager_->cancel(job_id_); }

private:
    static core::Error no_manager() {
        return core::Error(core::ErrorCode::JobNotFoundError,
                           "job handle is not bound to a job manager");
    }

    JobRecord snapshot() const {
        if (manager_ == nullptr) {
            JobRecord empty;
            empty.job_id = job_id_;
            empty.status = JobState::Failed;
            empty.error = no_manager();
            return empty;
        }
        auto record = manager_->get(job_id_);
        if (record.is_error()) {
            JobRecord failure;
            failure.job_id = job_id_;
            failure.status = JobState::Failed;
            failure.error = record.error();
            return failure;
        }
        return record.value();
    }

    IJobManager* manager_;
    std::string job_id_;
};

// --------------------------------------------------------------- JobSystem

class JobSystem : public IJobManager {
public:
    JobSystem(db::Database& db, artifacts::ArtifactStore& artifacts,
              std::size_t worker_count = 0 /* 0 = hardware concurrency */);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Optional observability hooks. All three fire outside the internal lock.
    void set_callbacks(ProgressFn on_progress, LogFn on_log);
    void set_state_callback(StateFn on_state);

    // Optional event bus receive: publishes job.queued / job.progress / job.log
    // / job.finished with structured JSON payloads. Non-owning.
    void set_event_bus(core::EventBus* bus);

    // ---- IJobManager -------------------------------------------------------
    core::Result<std::string> submit(const std::string& engine, const std::string& operation,
                                     const core::Json& request, JobFn fn,
                                     const std::string& project_id = "") override;

    core::Result<JobRecord> get(const std::string& job_id) const override;
    core::Result<std::vector<JobRecord>> list_recent(std::size_t limit = 50) const override;

    core::Status pause(const std::string& job_id) override;
    core::Status resume(const std::string& job_id) override;
    core::Status cancel(const std::string& job_id) override;

    void wait_for_idle() override;

    std::size_t queued_count() const override;
    std::size_t active_count() const override;

    // Convenience: an IJob handle for a submitted job.
    JobHandle handle(const std::string& job_id) { return JobHandle(this, job_id); }

private:
    friend class JobContext;

    struct Task {
        std::string job_id;
        std::string engine;
        std::string operation;
        core::Json request;
        JobFn fn;
    };

    void worker_loop(std::size_t worker_index);

    // Locked internals (caller holds mutex_).
    void set_state_locked(const std::string& job_id, JobState state, double progress);
    void append_log_locked(const std::string& job_id, const std::string& line);
    void finish_job_locked(const std::string& job_id, JobState state, const core::Json& output,
                           const std::optional<core::Error>& error, std::int64_t duration_ms);
    void persist_logs_locked(const std::string& job_id);
    core::Result<JobRecord> row_to_record(const core::JsonObject& row) const;

    // Lock-free notification helpers (snapshot callbacks, then invoke).
    void notify_progress(const std::string& job_id, double progress);
    void notify_log(const std::string& job_id, const std::string& line);
    void notify_terminal(const std::string& job_id);
    void publish(const std::string& topic, const core::Json& payload);

    // JobContext entry points (lock internally).
    void update_progress(const std::string& job_id, double progress);
    void append_log(const std::string& job_id, const std::string& line);
    bool should_run(const std::string& job_id);

    db::Database* db_;
    artifacts::ArtifactStore* artifacts_;

    mutable std::mutex mutex_;
    std::condition_variable wake_cv_;
    std::condition_variable idle_cv_;
    std::deque<Task> queue_;
    std::map<std::string, JobRecord> records_;          // in-memory live records
    std::map<std::string, bool> paused_;                // job_id -> paused
    std::map<std::string, bool> cancelled_;             // job_id -> cancelled
    std::size_t active_ = 0;
    bool stopping_ = false;
    std::vector<std::thread> workers_;

    ProgressFn on_progress_;
    LogFn on_log_;
    StateFn on_state_;
    core::EventBus* events_ = nullptr;
};

}  // namespace trinity::jobs
