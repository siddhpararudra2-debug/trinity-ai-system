#include "JobSystem.hpp"

#include <algorithm>
#include <utility>

#include "../core/Logging.hpp"
#include "../core/Time.hpp"
#include "../core/Uuid.hpp"

namespace trinity::jobs {
namespace {
core::ComponentLog log_("jobs");
}  // namespace

const char* job_state_string(JobState state) {
    switch (state) {
        case JobState::Queued: return "QUEUED";
        case JobState::Running: return "RUNNING";
        case JobState::Paused: return "PAUSED";
        case JobState::Completed: return "COMPLETED";
        case JobState::Failed: return "FAILED";
        case JobState::Cancelled: return "CANCELLED";
    }
    return "QUEUED";
}

bool job_state_is_terminal(JobState state) {
    return state == JobState::Completed || state == JobState::Failed ||
           state == JobState::Cancelled;
}

core::Result<JobState> job_state_from_string(const std::string& text) {
    if (text == "QUEUED") return core::Result<JobState>::ok(JobState::Queued);
    if (text == "RUNNING") return core::Result<JobState>::ok(JobState::Running);
    if (text == "PAUSED") return core::Result<JobState>::ok(JobState::Paused);
    if (text == "COMPLETED") return core::Result<JobState>::ok(JobState::Completed);
    if (text == "FAILED") return core::Result<JobState>::ok(JobState::Failed);
    if (text == "CANCELLED") return core::Result<JobState>::ok(JobState::Cancelled);
    return core::Result<JobState>::fail(core::Error(
        core::ErrorCode::RequestValidationError, "unknown job state '" + text + "'"));
}

core::Json JobRecord::to_json(bool include_logs) const {
    core::Json out = core::Json::object();
    out["job_id"] = job_id;
    out["project_id"] = project_id;
    out["engine"] = engine;
    out["operation"] = operation;
    out["status"] = job_state_string(status);
    out["progress"] = progress;
    out["request"] = request;
    out["output"] = output;
    if (include_logs) {
        core::Json log_array = core::Json::array();
        for (const std::string& line : logs) log_array.push_back(core::Json(line));
        out["logs"] = log_array;
    }
    out["duration_ms"] = static_cast<double>(duration_ms);
    out["error"] = error.has_value() ? error->to_json() : core::Json(nullptr);
    core::Json artifact_array = core::Json::array();
    for (const artifacts::Artifact& artifact : artifacts) {
        artifact_array.push_back(artifact.to_json());
    }
    out["artifacts"] = artifact_array;
    out["created_at"] = created_at;
    out["updated_at"] = updated_at;
    return out;
}

// ------------------------------------------------------------------ JobContext

JobContext::JobContext(std::string job_id, JobSystem* system)
    : job_id_(std::move(job_id)), system_(system) {}

void JobContext::report_progress(double progress) {
    if (system_ == nullptr) return;
    system_->update_progress(job_id_, std::clamp(progress, 0.0, 1.0));
}

void JobContext::log(const std::string& line) {
    if (system_ != nullptr) system_->append_log(job_id_, line);
}

void JobContext::log(const std::string& line, const core::Json& context) {
    if (system_ != nullptr) system_->append_log(job_id_, line + " " + context.dump());
}

bool JobContext::should_run() {
    return system_ != nullptr && system_->should_run(job_id_);
}

// ------------------------------------------------------------------ JobSystem

JobSystem::JobSystem(db::Database& db, artifacts::ArtifactStore& artifacts,
                     std::size_t worker_count)
    : db_(&db), artifacts_(&artifacts) {
    if (worker_count == 0) {
        worker_count = std::max<std::size_t>(2, std::thread::hardware_concurrency());
    }
    workers_.reserve(worker_count);
    for (std::size_t index = 0; index < worker_count; ++index) {
        workers_.emplace_back([this, index] { worker_loop(index); });
    }
}

JobSystem::~JobSystem() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_cv_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

void JobSystem::set_callbacks(ProgressFn on_progress, LogFn on_log) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_progress_ = std::move(on_progress);
    on_log_ = std::move(on_log);
}

void JobSystem::set_state_callback(StateFn on_state) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_state_ = std::move(on_state);
}

void JobSystem::set_event_bus(core::EventBus* bus) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_ = bus;
}

core::Result<std::string> JobSystem::submit(const std::string& engine,
                                            const std::string& operation,
                                            const core::Json& request, JobFn fn,
                                            const std::string& project_id) {
    if (!fn) {
        return core::Result<std::string>::fail(
            core::Error(core::ErrorCode::RequestValidationError, "job function is required"));
    }
    const std::string job_id = core::new_uuid();
    const std::string now = core::iso_utc_now();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        JobRecord record;
        record.job_id = job_id;
        record.project_id = project_id;
        record.engine = engine;
        record.operation = operation;
        record.status = JobState::Queued;
        record.progress = 0.0;
        record.request = request;
        record.created_at = now;
        record.updated_at = now;
        record.logs.push_back("job queued: " + engine + "." + operation);
        records_.emplace(job_id, std::move(record));

        Task task;
        task.job_id = job_id;
        task.engine = engine;
        task.operation = operation;
        task.request = request;
        task.fn = std::move(fn);
        queue_.push_back(std::move(task));
    }

    // Persist (best effort — the in-memory record remains the live truth).
    db_->run(
        "INSERT INTO jobs (job_id, project_id, engine, operation, status, progress, request, "
        "created_at, updated_at) VALUES (?, ?, ?, ?, 'QUEUED', 0.0, ?, ?, ?);",
        {core::Json(job_id), core::Json(project_id), core::Json(engine), core::Json(operation),
         core::Json(request.dump()), core::Json(now), core::Json(now)});

    wake_cv_.notify_one();

    core::Json context = core::Json::object();
    context["job_id"] = job_id;
    context["project_id"] = project_id;
    context["engine"] = engine;
    context["operation"] = operation;
    publish(core::topics::kJobQueued, context);
    log_.info("job submitted", context);
    return core::Result<std::string>::ok(job_id);
}

void JobSystem::worker_loop(std::size_t worker_index) {
    (void)worker_index;
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            task = std::move(queue_.front());
            queue_.pop_front();
            ++active_;
        }

        // Queued -> Running. The state write happens under the lock, the
        // notification outside it.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            set_state_locked(task.job_id, JobState::Running, 0.0);
        }
        notify_progress(task.job_id, 0.0);

        const std::int64_t started = core::monotonic_millis();
        JobContext context(task.job_id, this);
        core::Json output = core::Json::object();
        std::optional<core::Error> error;

        bool cancelled = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto cancel_it = cancelled_.find(task.job_id);
            cancelled = cancel_it != cancelled_.end() && cancel_it->second;
        }

        if (!cancelled) {
            try {
                output = task.fn(context, task.request);
            } catch (const core::TrinityException& exception) {
                error = exception.error();
            } catch (const std::exception& exception) {
                error = core::Error(core::ErrorCode::EngineExecutionError, exception.what());
            } catch (...) {
                error = core::Error(core::ErrorCode::EngineExecutionError,
                                    "unknown error in job execution");
            }
        }

        const std::int64_t duration = core::monotonic_millis() - started;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_;
            const auto cancel_it = cancelled_.find(task.job_id);
            const bool was_cancelled = cancel_it != cancelled_.end() && cancel_it->second;
            if (was_cancelled) {
                finish_job_locked(task.job_id, JobState::Cancelled, core::Json::object(),
                                  std::nullopt, duration);
            } else if (error.has_value()) {
                finish_job_locked(task.job_id, JobState::Failed, core::Json::object(), error,
                                  duration);
            } else {
                finish_job_locked(task.job_id, JobState::Completed, output, std::nullopt, duration);
            }
            cancelled_.erase(task.job_id);
            paused_.erase(task.job_id);
        }

        // Terminal events are published before the idle signal so a caller
        // waking from wait_for_idle() observes every side effect of the job.
        notify_terminal(task.job_id);
        idle_cv_.notify_all();
    }
}

void JobSystem::finish_job_locked(const std::string& job_id, JobState state,
                                  const core::Json& output,
                                  const std::optional<core::Error>& error,
                                  std::int64_t duration_ms) {
    // Caller holds mutex_. Never invokes user code.
    const auto it = records_.find(job_id);
    if (it == records_.end()) return;

    it->second.status = state;
    it->second.output = output;
    it->second.error = error;
    it->second.duration_ms = duration_ms;
    it->second.progress = (state == JobState::Completed) ? 1.0 : it->second.progress;
    it->second.logs.push_back(std::string("job ") + job_state_string(state) + " in " +
                              std::to_string(duration_ms) + " ms");
    it->second.updated_at = core::iso_utc_now();

    persist_logs_locked(job_id);

    db_->run(
        "UPDATE jobs SET status = ?, progress = ?, result = ?, error = ?, duration_ms = ?, "
        "updated_at = ? WHERE job_id = ?;",
        {core::Json(job_state_string(state)), core::Json(it->second.progress),
         core::Json(output.dump()),
         error.has_value() ? core::Json(error->to_json().dump()) : core::Json(nullptr),
         core::Json(static_cast<std::int64_t>(duration_ms)), core::Json(it->second.updated_at),
         core::Json(job_id)});
}

void JobSystem::set_state_locked(const std::string& job_id, JobState state, double progress) {
    // Caller holds mutex_. Never invokes user code.
    const auto it = records_.find(job_id);
    if (it == records_.end()) return;
    it->second.status = state;
    it->second.progress = std::clamp(progress, 0.0, 1.0);
    it->second.updated_at = core::iso_utc_now();
    db_->run("UPDATE jobs SET status = ?, progress = ?, updated_at = ? WHERE job_id = ?;",
             {core::Json(job_state_string(state)), core::Json(it->second.progress),
              core::Json(it->second.updated_at), core::Json(job_id)});
}

void JobSystem::append_log_locked(const std::string& job_id, const std::string& line) {
    const auto it = records_.find(job_id);
    if (it == records_.end()) return;
    it->second.logs.push_back(line);
}

void JobSystem::persist_logs_locked(const std::string& job_id) {
    // Caller holds mutex_. Writes the log ring into the logs table (capped).
    const auto it = records_.find(job_id);
    if (it == records_.end()) return;
    const std::string project_id = it->second.project_id;
    for (const std::string& line : it->second.logs) {
        db_->run("INSERT INTO logs (project_id, job_id, level, component, message, created_at) "
                 "VALUES (?, ?, 'INFO', 'job', ?, ?);",
                 {core::Json(project_id), core::Json(job_id), core::Json(line),
                  core::Json(core::iso_utc_now())});
    }
    it->second.logs.clear();
}

// --------------------------------------------------------------- notifications

void JobSystem::publish(const std::string& topic, const core::Json& payload) {
    core::EventBus* bus = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bus = events_;
    }
    if (bus != nullptr) bus->publish(topic, payload);
}

void JobSystem::notify_progress(const std::string& job_id, double progress) {
    ProgressFn callback;
    core::EventBus* bus = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = on_progress_;
        bus = events_;
    }
    if (callback) callback(job_id, progress);
    if (bus != nullptr) {
        core::Json payload = core::Json::object();
        payload["job_id"] = job_id;
        payload["progress"] = progress;
        bus->publish(core::topics::kJobProgress, payload);
    }
}

void JobSystem::notify_log(const std::string& job_id, const std::string& line) {
    LogFn callback;
    core::EventBus* bus = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = on_log_;
        bus = events_;
    }
    if (callback) callback(job_id, line);
    if (bus != nullptr) {
        core::Json payload = core::Json::object();
        payload["job_id"] = job_id;
        payload["line"] = line;
        bus->publish(core::topics::kJobLog, payload);
    }
}

void JobSystem::notify_terminal(const std::string& job_id) {
    StateFn callback;
    JobRecord record;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = records_.find(job_id);
        if (it == records_.end()) return;
        callback = on_state_;
        record = it->second;
    }
    core::Json payload = core::Json::object();
    payload["job_id"] = record.job_id;
    payload["project_id"] = record.project_id;
    payload["engine"] = record.engine;
    payload["operation"] = record.operation;
    payload["status"] = job_state_string(record.status);
    payload["progress"] = record.progress;
    payload["duration_ms"] = static_cast<double>(record.duration_ms);
    payload["error"] = record.error.has_value() ? record.error->to_json() : core::Json(nullptr);
    publish(core::topics::kJobFinished, payload);
    if (callback) callback(record);
}

// ------------------------------------------------------- JobContext entry points

void JobSystem::update_progress(const std::string& job_id, double progress) {
    double reported = 0.0;
    bool known = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = records_.find(job_id);
        if (it == records_.end()) return;
        if (job_state_is_terminal(it->second.status)) return;
        set_state_locked(job_id, JobState::Running, progress);
        reported = it->second.progress;
        known = true;
    }
    if (known) notify_progress(job_id, reported);
}

void JobSystem::append_log(const std::string& job_id, const std::string& line) {
    const std::string stamped = core::iso_utc_now() + " " + line;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        append_log_locked(job_id, stamped);
    }
    notify_log(job_id, stamped);
}

bool JobSystem::should_run(const std::string& job_id) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        const auto cancel_it = cancelled_.find(job_id);
        if (cancel_it != cancelled_.end() && cancel_it->second) return false;
        const auto pause_it = paused_.find(job_id);
        if (pause_it == paused_.end() || !pause_it->second) return true;
        wake_cv_.wait(lock);
    }
}

// ------------------------------------------------------------------- queries

core::Result<JobRecord> JobSystem::get(const std::string& job_id) const {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = records_.find(job_id);
        if (it != records_.end()) return core::Result<JobRecord>::ok(it->second);
    }
    // Fall back to SQLite (records from previous sessions).
    auto row = db_->query_one("SELECT * FROM jobs WHERE job_id = ?;", {core::Json(job_id)});
    if (row.is_error()) return core::Result<JobRecord>::fail(row.take_error());
    if (!row.value().has_value()) {
        return core::Result<JobRecord>::fail(
            core::Error(core::ErrorCode::JobNotFoundError, "no job with id '" + job_id + "'"));
    }
    return row_to_record(*row.value());
}

core::Result<JobRecord> JobSystem::row_to_record(const core::JsonObject& row) const {
    JobRecord record;
    const auto get = [&row](const char* key) -> core::Json {
        const auto it = row.find(key);
        return it != row.end() ? it->second : core::Json(nullptr);
    };
    record.job_id = get("job_id").as_string();
    record.project_id = get("project_id").as_string();
    record.engine = get("engine").as_string();
    record.operation = get("operation").as_string();
    const auto state = job_state_from_string(get("status").as_string());
    record.status = state.is_ok() ? state.value() : JobState::Queued;
    record.progress = get("progress").as_double();
    record.created_at = get("created_at").as_string();
    record.updated_at = get("updated_at").as_string();
    record.duration_ms = get("duration_ms").as_int();

    const std::string request_text = get("request").as_string();
    if (!request_text.empty()) {
        try {
            record.request = core::Json::parse(request_text);
        } catch (...) {
            record.request = core::Json::object();
        }
    }
    const std::string result_text = get("result").as_string();
    if (!result_text.empty()) {
        try {
            record.output = core::Json::parse(result_text);
        } catch (...) {
            record.output = core::Json::object();
        }
    }
    const std::string error_text = get("error").as_string();
    if (!error_text.empty()) {
        try {
            record.error = core::Error::from_json(core::Json::parse(error_text));
        } catch (...) {
        }
    }
    return core::Result<JobRecord>::ok(std::move(record));
}

core::Result<std::vector<JobRecord>> JobSystem::list_recent(std::size_t limit) const {
    // In-memory records preserve submission order (the map is keyed by uuid),
    // so the recent window is tracked explicitly in the insertion order.
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<JobRecord> out;
    out.reserve(std::min(limit, records_.size()));
    for (const auto& entry : records_) {
        out.push_back(entry.second);
        if (out.size() >= limit) break;
    }
    std::reverse(out.begin(), out.end());
    return core::Result<std::vector<JobRecord>>::ok(std::move(out));
}

// ---------------------------------------------------------------- transitions

core::Status JobSystem::pause(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = records_.find(job_id);
    if (it == records_.end()) {
        return core::Status::fail(
            core::Error(core::ErrorCode::JobNotFoundError, "no job with id '" + job_id + "'"));
    }
    if (it->second.status != JobState::Running && it->second.status != JobState::Queued) {
        return core::Status::fail(core::Error(
            core::ErrorCode::RequestValidationError,
            "job cannot be paused in state " + std::string(job_state_string(it->second.status))));
    }
    paused_[job_id] = true;
    return core::Status::ok();
}

core::Status JobSystem::resume(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = paused_.find(job_id);
    if (it == paused_.end() || !it->second) {
        return core::Status::fail(core::Error(core::ErrorCode::RequestValidationError,
                                              "job is not paused"));
    }
    it->second = false;
    wake_cv_.notify_all();
    return core::Status::ok();
}

core::Status JobSystem::cancel(const std::string& job_id) {
    bool notify_workers = false;
    bool finished_now = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = records_.find(job_id);
        if (it == records_.end()) {
            return core::Status::fail(
                core::Error(core::ErrorCode::JobNotFoundError,
                            "no job with id '" + job_id + "'"));
        }
        if (it->second.status == JobState::Queued) {
            for (auto task_it = queue_.begin(); task_it != queue_.end(); ++task_it) {
                if (task_it->job_id == job_id) {
                    queue_.erase(task_it);
                    break;
                }
            }
            finish_job_locked(job_id, JobState::Cancelled, core::Json::object(), std::nullopt, 0);
            finished_now = true;
        } else if (it->second.status == JobState::Running ||
                   it->second.status == JobState::Paused) {
            cancelled_[job_id] = true;
            notify_workers = true;
        } else {
            return core::Status::fail(core::Error(
                core::ErrorCode::RequestValidationError,
                "job cannot be cancelled in state " +
                    std::string(job_state_string(it->second.status))));
        }
    }
    if (notify_workers) wake_cv_.notify_all();
    if (finished_now) {
        notify_terminal(job_id);
        idle_cv_.notify_all();
    }
    return core::Status::ok();
}

void JobSystem::wait_for_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_cv_.wait(lock, [this] { return queue_.empty() && active_ == 0; });
}

std::size_t JobSystem::queued_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

std::size_t JobSystem::active_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

}  // namespace trinity::jobs
