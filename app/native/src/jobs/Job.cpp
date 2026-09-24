#include "trinity/jobs/Job.hpp"

#include <filesystem>

#include "trinity/core/Error.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/core/Time.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/storage/Migrations.hpp"

namespace trinity::jobs {
namespace fs = std::filesystem;

namespace {

core::Json parseOrNull(const std::string& text) {
    if (text.empty()) {
        return core::Json(nullptr);
    }
    return core::Json::parse(text, nullptr, false);
}

core::Json parseOr(const std::string& text, core::Json fallback) {
    if (text.empty()) {
        return fallback;
    }
    core::Json parsed = core::Json::parse(text, nullptr, false);
    if (parsed.is_discarded()) {
        return fallback;
    }
    return parsed;
}

std::vector<std::string> parseStringArray(const std::string& text) {
    core::Json v = parseOr(text, core::Json::array());
    std::vector<std::string> out;
    if (v.is_array()) {
        for (const auto& item : v) {
            if (item.is_string()) {
                out.push_back(item.get<std::string>());
            }
        }
    }
    return out;
}

// SELECT order must match rowToJob below.
constexpr const char* kJobColumns =
    "job_id, engine, operation, status, progress, request, result, error, "
    "created_at, updated_at, request_id, workflow_id, started_at, completed_at, "
    "artifacts, metadata, timeout_ms, retry_count, max_retries, retry_delay_ms";

Job rowToJob(const std::vector<std::string>& row) {
    Job job;
    auto at = [&](size_t i) -> std::string { return i < row.size() ? row[i] : ""; };
    job.jobId = at(0);
    job.engine = at(1);
    job.operation = at(2);
    job.status = fromString(at(3));
    job.request = parseOrNull(at(5));
    job.input = job.request;
    if (job.input.is_null()) {
        job.input = core::Json::object();
    }
    if (job.request.is_null()) {
        job.request = core::Json::object();
    }
    try {
        job.progress = at(4).empty() ? 0.0 : std::stod(at(4));
    } catch (...) {
        job.progress = 0.0;
    }
    job.result = parseOrNull(at(6));
    job.error = parseOrNull(at(7));
    job.createdAt = at(8);
    job.updatedAt = at(9);
    job.requestId = at(10);
    job.workflowId = at(11);
    job.startedAt = at(12);
    job.completedAt = at(13);
    job.artifactIds = parseStringArray(at(14));
    job.metadata = parseOr(at(15), core::Json::object());
    if (job.metadata.is_null()) {
        job.metadata = core::Json::object();
    }
    try {
        job.timeoutMs = at(16).empty() ? 0 : std::stoi(at(16));
    } catch (...) {
        job.timeoutMs = 0;
    }
    try {
        job.retry.attempt = at(17).empty() ? 0 : std::stoi(at(17));
        job.retry.maxRetries = at(18).empty() ? 0 : std::stoi(at(18));
        job.retry.delayMs = at(19).empty() ? 0 : std::stoi(at(19));
    } catch (...) {
    }
    // Keep retry copy inside metadata for traceability.
    return job;
}

bool hasColumnCached(storage::Database& db, const std::string& table, const std::string& col) {
    return storage::tableHasColumn(db, table, col);
}

}  // namespace

std::string toString(JobStatus status) {
    switch (status) {
        case JobStatus::Queued:
            return "queued";
        case JobStatus::Running:
            return "running";
        case JobStatus::Completed:
            return "completed";
        case JobStatus::Failed:
            return "failed";
        case JobStatus::Cancelled:
            return "cancelled";
        case JobStatus::Retrying:
            return "retrying";
        case JobStatus::WaitingApproval:
            return "waiting_approval";
    }
    return "failed";
}

JobStatus fromString(const std::string& status) {
    if (status == "queued" || status == "QUEUED") return JobStatus::Queued;
    if (status == "running" || status == "RUNNING") return JobStatus::Running;
    if (status == "completed" || status == "COMPLETED") return JobStatus::Completed;
    if (status == "cancelled" || status == "CANCELLED") return JobStatus::Cancelled;
    if (status == "retrying" || status == "RETRYING") return JobStatus::Retrying;
    if (status == "waiting_approval" || status == "WAITING_APPROVAL")
        return JobStatus::WaitingApproval;
    return JobStatus::Failed;
}

bool isTerminal(JobStatus status) noexcept {
    return status == JobStatus::Completed || status == JobStatus::Failed ||
           status == JobStatus::Cancelled;
}

bool canTransition(JobStatus from, JobStatus to) noexcept {
    // Reserved future states are never valid targets in V1.
    if (to == JobStatus::Retrying || to == JobStatus::WaitingApproval) {
        return false;
    }
    if (from == to) {
        return false;
    }
    switch (from) {
        case JobStatus::Queued:
            return to == JobStatus::Running || to == JobStatus::Cancelled;
        case JobStatus::Running:
            return to == JobStatus::Completed || to == JobStatus::Failed ||
                   to == JobStatus::Cancelled;
        case JobStatus::Completed:
        case JobStatus::Failed:
        case JobStatus::Cancelled:
            return false;
        case JobStatus::Retrying:
            return to == JobStatus::Running || to == JobStatus::Cancelled;
        case JobStatus::WaitingApproval:
            return to == JobStatus::Queued || to == JobStatus::Cancelled;
    }
    return false;
}

std::string jobTypeToString(JobType type) {
    switch (type) {
        case JobType::Engine:
            return "engine";
        case JobType::Workflow:
            return "workflow";
        case JobType::Validation:
            return "validation";
    }
    return "engine";
}

JobType jobTypeFromString(const std::string& type) {
    if (type == "workflow") return JobType::Workflow;
    if (type == "validation") return JobType::Validation;
    return JobType::Engine;
}

core::Json RetryPolicy::toJson() const {
    return core::Json{{"attempt", attempt}, {"max_retries", maxRetries}, {"delay_ms", delayMs}};
}

RetryPolicy RetryPolicy::fromJson(const core::Json& json) {
    RetryPolicy out;
    out.attempt = json.value("attempt", 0);
    out.maxRetries = json.value("max_retries", json.value("maxRetries", 0));
    out.delayMs = json.value("delay_ms", json.value("delayMs", 0));
    return out;
}

core::Json Job::toJson() const {
    core::Json artifacts = core::Json::array();
    for (const auto& id : artifactIds) {
        artifacts.push_back(id);
    }
    core::Json meta = metadata;
    if (meta.is_null()) {
        meta = core::Json::object();
    }
    return core::Json{{"job_id", jobId},
                      {"request_id", requestId},
                      {"workflow_id", workflowId},
                      {"engine", engine},
                      {"operation", operation},
                      {"status", toString(status)},
                      {"progress", progress},
                      {"input", input},
                      {"request", request},
                      {"result", result},
                      {"error", error},
                      {"created_at", createdAt},
                      {"started_at", startedAt},
                      {"completed_at", completedAt},
                      {"updated_at", updatedAt},
                      {"artifact_ids", artifacts},
                      {"artifacts", artifacts},
                      {"metadata", meta},
                      {"timeout_ms", timeoutMs},
                      {"retry", retry.toJson()}};
}

Job Job::fromJson(const core::Json& json) {
    Job job;
    job.jobId = json.value("job_id", "");
    job.requestId = json.value("request_id", "");
    job.workflowId = json.value("workflow_id", "");
    job.engine = json.value("engine", "");
    job.operation = json.value("operation", "");
    job.status = fromString(json.value("status", "failed"));
    job.progress = json.value("progress", 0.0);
    // input canonical, request legacy alias.
    if (json.contains("input") && !json["input"].is_null()) {
        job.input = json["input"];
    } else {
        job.input = json.value("request", core::Json::object());
    }
    if (json.contains("request") && !json["request"].is_null()) {
        job.request = json["request"];
    } else {
        job.request = job.input;
    }
    job.result = json.value("result", core::Json(nullptr));
    job.error = json.value("error", core::Json(nullptr));
    job.createdAt = json.value("created_at", "");
    job.startedAt = json.value("started_at", "");
    job.completedAt = json.value("completed_at", "");
    job.updatedAt = json.value("updated_at", "");
    if (json.contains("artifact_ids") && json["artifact_ids"].is_array()) {
        for (const auto& item : json["artifact_ids"]) {
            if (item.is_string()) {
                job.artifactIds.push_back(item.get<std::string>());
            }
        }
    } else if (json.contains("artifacts") && json["artifacts"].is_array()) {
        for (const auto& item : json["artifacts"]) {
            if (item.is_string()) {
                job.artifactIds.push_back(item.get<std::string>());
            } else if (item.is_object() && item.contains("artifact_id")) {
                job.artifactIds.push_back(item.value("artifact_id", ""));
            }
        }
    }
    job.metadata = json.value("metadata", core::Json::object());
    if (job.metadata.is_null()) {
        job.metadata = core::Json::object();
    }
    job.timeoutMs = json.value("timeout_ms", 0);
    if (json.contains("retry")) {
        job.retry = RetryPolicy::fromJson(json["retry"]);
    }
    return job;
}

JobManager::JobManager(std::shared_ptr<storage::Database> db,
                       std::shared_ptr<engines::EngineRegistry> registry,
                       std::shared_ptr<artifacts::ArtifactManager> artifacts)
    : db_(std::move(db)), registry_(std::move(registry)), artifacts_(std::move(artifacts)) {
}

Job JobManager::createJob(const std::string& engineName, const std::string& operation,
                          const core::Json& input, const JobCreateOptions& options) {
    auto& log = core::Logger::instance();
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string jobId = core::newUuid();
    const std::string requestId =
        options.requestId.empty() ? core::newUuid() : options.requestId;
    const std::string now = core::utcNowIso();
    core::Json params = input;
    if (params.is_null()) {
        params = core::Json::object();
    }
    core::Json meta = options.metadata;
    if (meta.is_null()) {
        meta = core::Json::object();
    }
    if (options.timeoutMs > 0) {
        meta["timeout_ms"] = options.timeoutMs;
    }
    meta["retry"] = options.retry.toJson();

    core::Json artifactsArr = core::Json::array();
    storage::Transaction tx(*db_);
    if (hasColumnCached(*db_, "jobs", "request_id")) {
        db_->execute(
            "INSERT INTO jobs (job_id, engine, operation, status, progress, request, "
            "result, error, created_at, updated_at, request_id, workflow_id, started_at, "
            "completed_at, artifacts, metadata, timeout_ms, retry_count, max_retries, "
            "retry_delay_ms) VALUES (?, ?, ?, 'queued', 0.0, ?, NULL, NULL, ?, ?, ?, ?, '', "
            "'', ?, ?, ?, ?, ?, ?);",
            {jobId, engineName, operation, params.dump(), now, now, requestId,
             options.workflowId, artifactsArr.dump(), meta.dump(), std::int64_t(options.timeoutMs),
             std::int64_t(options.retry.attempt), std::int64_t(options.retry.maxRetries),
             std::int64_t(options.retry.delayMs)});
    } else {
        // Pre-migration fallback (tests creating raw schema).
        db_->execute(
            "INSERT INTO jobs (job_id, engine, operation, status, progress, request, "
            "result, error, created_at, updated_at) VALUES (?, ?, ?, 'queued', 0.0, ?, "
            "NULL, NULL, ?, ?);",
            {jobId, engineName, operation, params.dump(), now, now});
    }
    tx.commit();

    log.info("jobs", "job created",
             core::Json{{"job_id", jobId},
                        {"request_id", requestId},
                        {"engine", engineName},
                        {"operation", operation},
                        {"workflow_id", options.workflowId}});
    log.info("jobs", "job queued", core::Json{{"job_id", jobId}});

    Job job;
    job.jobId = jobId;
    job.requestId = requestId;
    job.workflowId = options.workflowId;
    job.engine = engineName;
    job.operation = operation;
    job.status = JobStatus::Queued;
    job.progress = 0.0;
    job.input = params;
    job.request = params;
    job.result = nullptr;
    job.error = nullptr;
    job.createdAt = now;
    job.updatedAt = now;
    job.metadata = meta;
    job.timeoutMs = options.timeoutMs;
    job.retry = options.retry;
    return job;
}

void JobManager::persistLocked(const Job& job) {
    core::Json artifactsArr = core::Json::array();
    for (const auto& id : job.artifactIds) {
        artifactsArr.push_back(id);
    }
    core::Json meta = job.metadata;
    if (meta.is_null()) {
        meta = core::Json::object();
    }
    const std::string now = core::utcNowIso();
    if (hasColumnCached(*db_, "jobs", "request_id")) {
        db_->execute(
            "UPDATE jobs SET engine=?, operation=?, status=?, progress=?, request=?, "
            "result=?, error=?, updated_at=?, request_id=?, workflow_id=?, started_at=?, "
            "completed_at=?, artifacts=?, metadata=?, timeout_ms=?, retry_count=?, "
            "max_retries=?, retry_delay_ms=? WHERE job_id=?;",
            {job.engine, job.operation, toString(job.status), job.progress,
             job.request.dump(), job.result.dump(), job.error.dump(),
             job.updatedAt.empty() ? now : job.updatedAt, job.requestId, job.workflowId,
             job.startedAt, job.completedAt, artifactsArr.dump(), meta.dump(),
             std::int64_t(job.timeoutMs), std::int64_t(job.retry.attempt),
             std::int64_t(job.retry.maxRetries), std::int64_t(job.retry.delayMs), job.jobId});
    } else {
        db_->execute(
            "UPDATE jobs SET engine=?, operation=?, status=?, progress=?, request=?, "
            "result=?, error=?, updated_at=? WHERE job_id=?;",
            {job.engine, job.operation, toString(job.status), job.progress,
             job.request.dump(), job.result.dump(), job.error.dump(),
             job.updatedAt.empty() ? now : job.updatedAt, job.jobId});
    }
}

void JobManager::setStatusLocked(Job& job, JobStatus status) {
    if (!canTransition(job.status, status)) {
        throw core::RequestValidationError(
            "Invalid job status transition from '" + toString(job.status) + "' to '" +
                toString(status) + "'",
            {{"job_id", job.jobId}}, "jobs");
    }
    job.status = status;
    job.updatedAt = core::utcNowIso();
    if (status == JobStatus::Running && job.startedAt.empty()) {
        job.startedAt = job.updatedAt;
    }
    if (isTerminal(status) && job.completedAt.empty()) {
        job.completedAt = job.updatedAt;
    }
}

Job JobManager::getLocked(const std::string& jobId) const {
    std::string sql;
    if (hasColumnCached(*db_, "jobs", "request_id")) {
        sql = std::string("SELECT ") + kJobColumns + " FROM jobs WHERE job_id = ?;";
    } else {
        sql =
            "SELECT job_id, engine, operation, status, progress, request, result, error, "
            "created_at, updated_at FROM jobs WHERE job_id = ?;";
    }
    const auto rows = db_->queryParams(sql, {jobId});
    if (rows.empty()) {
        throw core::JobNotFoundError("No job with id '" + jobId + "'");
    }
    return rowToJob(rows[0]);
}

core::Json JobManager::executeJob(const std::string& jobId,
                                  std::shared_ptr<CancellationToken> token) {
    auto& log = core::Logger::instance();
    Job job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job = getLocked(jobId);
        if (job.status == JobStatus::Cancelled || cancelRequested_.count(jobId) != 0) {
            if (canTransition(job.status, JobStatus::Cancelled)) {
                setStatusLocked(job, JobStatus::Cancelled);
                job.progress = 1.0;
                persistLocked(job);
                log.info("jobs", "job cancelled before start", core::Json{{"job_id", jobId}});
            }
            return core::Json{{"success", false},
                              {"engine", job.engine},
                              {"operation", job.operation},
                              {"result", core::Json::object()},
                              {"artifacts", core::Json::array()},
                              {"validation", nullptr},
                              {"errors",
                               core::Json::array({core::makeError(core::ErrorCode::TrinityError,
                                                                 "Job cancelled", "jobs")
                                                      .toJson()})},
                              {"job_id", jobId}};
        }
        setStatusLocked(job, JobStatus::Running);
        job.progress = 0.1;
        persistLocked(job);
    }
    log.info("jobs", "job execution started",
             core::Json{{"job_id", jobId}, {"engine", job.engine}, {"operation", job.operation}});

    if (token && token->isCancelled()) {
        std::lock_guard<std::mutex> lock(mutex_);
        Job current = getLocked(jobId);
        if (canTransition(current.status, JobStatus::Cancelled)) {
            setStatusLocked(current, JobStatus::Cancelled);
            current.progress = 1.0;
            persistLocked(current);
            log.info("jobs", "job cancelled", core::Json{{"job_id", jobId}});
        }
        return core::Json{{"success", false},
                          {"engine", job.engine},
                          {"operation", job.operation},
                          {"result", core::Json::object()},
                          {"artifacts", core::Json::array()},
                          {"validation", nullptr},
                          {"errors",
                           core::Json::array({core::makeError(core::ErrorCode::TrinityError,
                                                             "Job cancelled", "jobs")
                                                  .toJson()})},
                          {"job_id", jobId}};
    }

    // Unknown engine: mark FAILED (do not leave RUNNING).
    try {
        (void)registry_->get(job.engine);
    } catch (const core::TrinityError& exc) {
        std::lock_guard<std::mutex> lock(mutex_);
        Job current = getLocked(jobId);
        if (canTransition(current.status, JobStatus::Failed)) {
            setStatusLocked(current, JobStatus::Failed);
            current.progress = 1.0;
            current.error = exc.toJson();
            persistLocked(current);
        }
        log.info("jobs", "job failed", core::Json{{"job_id", jobId}, {"error", exc.toJson()}});
        throw;
    }

    engines::EngineResult engineResult;
    try {
        engines::EngineRequest request;
        request.requestId = job.requestId.empty() ? core::newUuid() : job.requestId;
        request.engine = job.engine;
        request.operation = job.operation;
        request.parameters = job.input.is_null() ? job.request : job.input;
        if (request.parameters.is_null()) {
            request.parameters = core::Json::object();
        }
        if (token) {
            request.cancelCheck = [token]() { return token->isCancelled(); };
        } else {
            const std::string id = jobId;
            request.cancelCheck = [this, id]() { return isCancellationRequested(id); };
        }
        engineResult = registry_->execute(request);
        engineResult.jobId = jobId;
    } catch (const core::TrinityError& exc) {
        std::lock_guard<std::mutex> lock(mutex_);
        Job current = getLocked(jobId);
        if (cancelRequested_.count(jobId) != 0 && canTransition(current.status, JobStatus::Cancelled)) {
            setStatusLocked(current, JobStatus::Cancelled);
            current.progress = 1.0;
            persistLocked(current);
            log.info("jobs", "job cancelled during execution", core::Json{{"job_id", jobId}});
        } else if (canTransition(current.status, JobStatus::Failed)) {
            setStatusLocked(current, JobStatus::Failed);
            current.progress = 1.0;
            current.error = exc.toJson();
            persistLocked(current);
            log.info("jobs", "job failed", core::Json{{"job_id", jobId}});
        }
        return core::Json{{"success", false},
                          {"engine", job.engine},
                          {"operation", job.operation},
                          {"result", core::Json::object()},
                          {"artifacts", core::Json::array()},
                          {"validation", nullptr},
                          {"errors", core::Json::array({exc.toJson()})},
                          {"job_id", jobId}};
    } catch (const std::exception& exc) {
        const core::Json err =
            core::makeError(core::ErrorCode::EngineExecutionError, exc.what(), "jobs").toJson();
        std::lock_guard<std::mutex> lock(mutex_);
        Job current = getLocked(jobId);
        if (canTransition(current.status, JobStatus::Failed)) {
            setStatusLocked(current, JobStatus::Failed);
            current.progress = 1.0;
            current.error = err;
            persistLocked(current);
        }
        log.info("jobs", "job failed unexpectedly", core::Json{{"job_id", jobId}});
        return core::Json{{"success", false},
                          {"engine", job.engine},
                          {"operation", job.operation},
                          {"result", core::Json::object()},
                          {"artifacts", core::Json::array()},
                          {"validation", nullptr},
                          {"errors", core::Json::array({err})},
                          {"job_id", jobId}};
    }

    // Cooperative cancellation: if requested mid-flight, discard result honestly.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancelRequested_.count(jobId) != 0 || (token && token->isCancelled())) {
            Job current = getLocked(jobId);
            if (canTransition(current.status, JobStatus::Cancelled)) {
                setStatusLocked(current, JobStatus::Cancelled);
                current.progress = 1.0;
                persistLocked(current);
                log.info("jobs", "job cancelled during execution",
                         core::Json{{"job_id", jobId}});
            }
            return core::Json{{"success", false},
                              {"engine", job.engine},
                              {"operation", job.operation},
                              {"result", core::Json::object()},
                              {"artifacts", core::Json::array()},
                              {"validation", nullptr},
                              {"errors",
                               core::Json::array({core::makeError(core::ErrorCode::TrinityError,
                                                                 "Job cancelled", "jobs")
                                                      .toJson()})},
                              {"job_id", jobId}};
        }
    }

    // Promote engine scratch files into managed storage (single writer).
    for (const auto& [tempPath, artifactType] : engineResult.pendingArtifacts) {
        artifacts::Artifact ref = artifacts_->storeFile(tempPath, artifactType, jobId);
        engines::ArtifactRef out;
        out.artifactId = ref.artifactId;
        out.type = ref.type;
        out.path = ref.path;
        out.sizeBytes = ref.sizeBytes;
        out.checksum = ref.checksum;
        engineResult.artifacts.push_back(std::move(out));
    }
    if (!engineResult.pendingArtifacts.empty()) {
        std::error_code ec;
        fs::remove_all(fs::path(engineResult.pendingArtifacts[0].first).parent_path(), ec);
    }

    core::Json validationJson = nullptr;
    bool validationPassed = false;
    if (engineResult.validation.has_value()) {
        auto validation = *engineResult.validation;
        validation.jobId = jobId;
        validation.operation =
            engineResult.operation.empty() ? job.operation : engineResult.operation;
        validationJson = validation.toJson();
        validationPassed = validation.passed();
        log.info("jobs", "job validation",
                 core::Json{{"job_id", jobId},
                            {"status", validation::toString(validation.status)}});
        storage::Transaction tx(*db_);
        db_->execute(
            "INSERT INTO validations (validation_id, job_id, engine, status, checks, "
            "created_at) VALUES (?, ?, ?, ?, ?, ?);",
            {core::newUuid(), jobId, job.engine, validation::toString(validation.status),
             validation.checks.dump(), core::utcNowIso()});
        tx.commit();
    }

    // COMPLETED only when engine success AND required validation passed.
    const bool completed = engineResult.success && (!engineResult.validation.has_value() ||
                                                    validationPassed ||
                                                    // Engines without explicit validation
                                                    // (base validate returns Generated on
                                                    // success) still count when success=true
                                                    // and validation is Generated+ — treat
                                                    // Invalid as failure only.
                                                    validationPassed);

    // Recompute strictly: success must hold and validation (if present) must pass.
    bool actuallyCompleted = engineResult.success;
    if (engineResult.validation.has_value()) {
        actuallyCompleted = actuallyCompleted && engineResult.validation->passed();
    }

    core::Json artifactsJson = core::Json::array();
    std::vector<std::string> artifactIds;
    for (const auto& ref : engineResult.artifacts) {
        artifactsJson.push_back(ref.toJson());
        artifactIds.push_back(ref.artifactId);
    }
    core::Json response{{"success", actuallyCompleted},
                        {"engine", engineResult.engine},
                        {"operation", engineResult.operation},
                        {"result", engineResult.result},
                        {"artifacts", artifactsJson},
                        {"validation", validationJson},
                        {"errors", engineResult.errors},
                        {"job_id", jobId}};
    (void)completed;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        Job current = getLocked(jobId);
        // Cancellation wins over completion if requested late.
        if (cancelRequested_.count(jobId) != 0 || (token && token->isCancelled())) {
            if (canTransition(current.status, JobStatus::Cancelled)) {
                setStatusLocked(current, JobStatus::Cancelled);
                current.progress = 1.0;
                persistLocked(current);
                log.info("jobs", "job cancelled", core::Json{{"job_id", jobId}});
                response["success"] = false;
                return response;
            }
        }
        storage::Transaction tx(*db_);
        if (canTransition(current.status, actuallyCompleted ? JobStatus::Completed
                                                            : JobStatus::Failed)) {
            setStatusLocked(current, actuallyCompleted ? JobStatus::Completed : JobStatus::Failed);
        }
        current.progress = 1.0;
        current.result = engineResult.result;
        if (!engineResult.errors.empty()) {
            current.error = engineResult.errors.front();
        } else if (!actuallyCompleted) {
            current.error = core::makeError(core::ErrorCode::EngineExecutionError,
                                            "Engine execution failed", "jobs")
                                .toJson();
        } else {
            current.error = nullptr;
        }
        current.artifactIds = artifactIds;
        persistLocked(current);
        tx.commit();
    }
    log.info("jobs", "job execution completed",
             core::Json{{"job_id", jobId}, {"success", actuallyCompleted}});
    return response;
}

core::Json JobManager::runSync(const std::string& engineName, const std::string& operation,
                               const core::Json& parameters) {
    Job job = createJob(engineName, operation, parameters);
    return executeJob(job.jobId);
}

bool JobManager::cancel(const std::string& jobId) {
    auto& log = core::Logger::instance();
    std::lock_guard<std::mutex> lock(mutex_);
    Job job = getLocked(jobId);
    if (job.status == JobStatus::Queued) {
        cancelRequested_.insert(jobId);
        setStatusLocked(job, JobStatus::Cancelled);
        job.progress = 1.0;
        persistLocked(job);
        log.info("jobs", "job cancelled", core::Json{{"job_id", jobId}});
        return true;
    }
    if (job.status == JobStatus::Running) {
        // Cooperative: record request; worker/executor checks token between
        // steps. Do NOT claim instant preemption.
        cancelRequested_.insert(jobId);
        job.metadata["cancellation_requested"] = true;
        job.metadata["cancellation_requested_at"] = core::utcNowIso();
        persistLocked(job);
        log.info("jobs", "job cancellation requested (running)",
                 core::Json{{"job_id", jobId}});
        return true;
    }
    return false;
}

bool JobManager::isCancellationRequested(const std::string& jobId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cancelRequested_.count(jobId) != 0;
}

void JobManager::updateStatus(const std::string& jobId, JobStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);
    Job job = getLocked(jobId);
    setStatusLocked(job, status);
    persistLocked(job);
    core::Logger::instance().info(
        "jobs", "job status updated",
        core::Json{{"job_id", jobId}, {"status", toString(status)}});
}

void JobManager::updateResult(const std::string& jobId, const core::Json& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    Job job = getLocked(jobId);
    job.result = result;
    job.updatedAt = core::utcNowIso();
    persistLocked(job);
}

void JobManager::updateError(const std::string& jobId, const core::Json& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    Job job = getLocked(jobId);
    job.error = error;
    job.updatedAt = core::utcNowIso();
    persistLocked(job);
}

void JobManager::attachArtifacts(const std::string& jobId,
                                 const std::vector<std::string>& artifactIds) {
    std::lock_guard<std::mutex> lock(mutex_);
    Job job = getLocked(jobId);
    for (const auto& id : artifactIds) {
        job.artifactIds.push_back(id);
    }
    job.updatedAt = core::utcNowIso();
    persistLocked(job);
}

Job JobManager::get(const std::string& jobId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return getLocked(jobId);
}

std::vector<Job> JobManager::listRecent(int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string sql;
    if (hasColumnCached(*db_, "jobs", "request_id")) {
        sql = std::string("SELECT ") + kJobColumns +
              " FROM jobs ORDER BY created_at DESC LIMIT " + std::to_string(limit) + ";";
    } else {
        sql =
            "SELECT job_id, engine, operation, status, progress, request, result, error, "
            "created_at, updated_at FROM jobs ORDER BY created_at DESC LIMIT " +
            std::to_string(limit) + ";";
    }
    const auto rows = db_->query(sql);
    std::vector<Job> jobs;
    for (const auto& row : rows) {
        jobs.push_back(rowToJob(row));
    }
    return jobs;
}

int JobManager::recoverOnStartup() {
    auto& log = core::Logger::instance();
    std::lock_guard<std::mutex> lock(mutex_);
    std::string sql;
    if (hasColumnCached(*db_, "jobs", "request_id")) {
        sql = std::string("SELECT ") + kJobColumns +
              " FROM jobs WHERE status IN ('queued','running');";
    } else {
        sql =
            "SELECT job_id, engine, operation, status, progress, request, result, error, "
            "created_at, updated_at FROM jobs WHERE status IN ('queued','running');";
    }
    const auto rows = db_->query(sql);
    int recovered = 0;
    storage::Transaction tx(*db_);
    for (const auto& row : rows) {
        Job job = rowToJob(row);
        // Interrupted RUNNING jobs go back to QUEUED for re-execution;
        // QUEUED jobs stay QUEUED. Never auto-complete.
        if (job.status == JobStatus::Running) {
            job.status = JobStatus::Queued;
            job.progress = 0.0;
            job.updatedAt = core::utcNowIso();
            persistLocked(job);
            log.info("jobs", "job recovered to queued", core::Json{{"job_id", job.jobId}});
            ++recovered;
        } else {
            ++recovered;
            log.info("jobs", "job recovered", core::Json{{"job_id", job.jobId}});
        }
    }
    tx.commit();
    if (recovered > 0) {
        log.info("jobs", "job recovery completed", core::Json{{"recovered", recovered}});
    }
    return recovered;
}

}  // namespace trinity::jobs
