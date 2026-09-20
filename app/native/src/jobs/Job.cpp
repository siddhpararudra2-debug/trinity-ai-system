#include "trinity/jobs/Job.hpp"

#include <filesystem>

#include "trinity/core/Logger.hpp"
#include "trinity/core/Time.hpp"
#include "trinity/core/Uuid.hpp"

namespace trinity::jobs {
namespace fs = std::filesystem;

namespace {

core::Json parseOrNull(const std::string& text) {
    if (text.empty()) {
        return core::Json(nullptr);
    }
    return core::Json::parse(text, nullptr, false);
}

Job rowToJob(const std::vector<std::string>& row) {
    Job job;
    job.jobId = row[0];
    job.engine = row[1];
    job.operation = row[2];
    job.status = fromString(row[3]);
    job.progress = row[4].empty() ? 0.0 : std::stod(row[4]);
    job.request = parseOrNull(row[5]);
    job.result = parseOrNull(row[6]);
    job.error = parseOrNull(row[7]);
    job.createdAt = row[8];
    job.updatedAt = row[9];
    return job;
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
    }
    return "failed";
}

JobStatus fromString(const std::string& status) {
    if (status == "queued") return JobStatus::Queued;
    if (status == "running") return JobStatus::Running;
    if (status == "completed") return JobStatus::Completed;
    if (status == "cancelled") return JobStatus::Cancelled;
    return JobStatus::Failed;
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

core::Json Job::toJson() const {
    return core::Json{{"job_id", jobId},
                      {"workflow_id", workflowId},
                      {"engine", engine},
                      {"operation", operation},
                      {"status", toString(status)},
                      {"progress", progress},
                      {"request", request},
                      {"result", result},
                      {"error", error},
                      {"created_at", createdAt},
                      {"updated_at", updatedAt}};
}

Job Job::fromJson(const core::Json& json) {
    Job job;
    job.jobId = json.value("job_id", "");
    job.workflowId = json.value("workflow_id", "");
    job.engine = json.value("engine", "");
    job.operation = json.value("operation", "");
    job.status = fromString(json.value("status", "failed"));
    job.progress = json.value("progress", 0.0);
    job.request = json.value("request", core::Json::object());
    job.result = json.value("result", core::Json(nullptr));
    job.error = json.value("error", core::Json(nullptr));
    job.createdAt = json.value("created_at", "");
    job.updatedAt = json.value("updated_at", "");
    return job;
}

JobManager::JobManager(std::shared_ptr<storage::Database> db,
                       std::shared_ptr<engines::EngineRegistry> registry,
                       std::shared_ptr<artifacts::ArtifactManager> artifacts)
    : db_(std::move(db)), registry_(std::move(registry)), artifacts_(std::move(artifacts)) {
}

core::Json JobManager::runSync(const std::string& engineName, const std::string& operation,
                               const core::Json& parameters) {
    auto& log = core::Logger::instance();
    const std::string jobId = core::newUuid();
    const std::string now = core::utcNowIso();

    db_->execute(
        "INSERT INTO jobs (job_id, engine, operation, status, progress, request, "
        "result, error, created_at, updated_at) VALUES (?, ?, ?, 'queued', 0.0, ?, "
        "NULL, NULL, ?, ?);",
        {jobId, engineName, operation, parameters.dump(), now, now});
    db_->execute(
        "UPDATE jobs SET status = 'running', progress = 0.1, updated_at = ? WHERE "
        "job_id = ?;",
        {core::utcNowIso(), jobId});
    log.info("jobs", "job started",
             core::Json{{"job_id", jobId}, {"engine", engineName}, {"operation", operation}});

    std::shared_ptr<engines::IEngine> engine;
    try {
        engine = registry_->get(engineName);
    } catch (const core::TrinityError& exc) {
        db_->execute(
            "UPDATE jobs SET status = 'failed', progress = 1.0, error = ?, updated_at = "
            "? WHERE job_id = ?;",
            {exc.toJson().dump(), core::utcNowIso(), jobId});
        throw;
    }

    engines::EngineResult engineResult;
    try {
        engineResult = engine->execute(operation, parameters);
    } catch (const core::TrinityError& exc) {
        db_->execute(
            "UPDATE jobs SET status = 'failed', progress = 1.0, error = ?, updated_at = "
            "? WHERE job_id = ?;",
            {exc.toJson().dump(), core::utcNowIso(), jobId});
        log.info("jobs", "job failed",
                 core::Json{{"job_id", jobId}, {"error", exc.toJson()}});
        return core::Json{{"success", false},
                          {"engine", engineName},
                          {"operation", operation},
                          {"result", core::Json::object()},
                          {"artifacts", core::Json::array()},
                          {"validation", nullptr},
                          {"errors", core::Json::array({exc.toJson()})},
                          {"job_id", jobId}};
    } catch (const std::exception& exc) {
        const core::Json err = core::makeError(core::ErrorCode::EngineExecutionError,
                                               exc.what(), "jobs")
                                   .toJson();
        db_->execute(
            "UPDATE jobs SET status = 'failed', progress = 1.0, error = ?, updated_at = "
            "? WHERE job_id = ?;",
            {err.dump(), core::utcNowIso(), jobId});
        log.info("jobs", "job failed unexpectedly", core::Json{{"job_id", jobId}});
        return core::Json{{"success", false},
                          {"engine", engineName},
                          {"operation", operation},
                          {"result", core::Json::object()},
                          {"artifacts", core::Json::array()},
                          {"validation", nullptr},
                          {"errors", core::Json::array({err})},
                          {"job_id", jobId}};
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
    if (engineResult.validation.has_value()) {
        auto validation = *engineResult.validation;
        validation.jobId = jobId;
        validation.operation = engineResult.operation.empty() ? operation
                                                              : engineResult.operation;
        validationJson = validation.toJson();
        db_->execute(
            "INSERT INTO validations (validation_id, job_id, engine, status, checks, "
            "created_at) VALUES (?, ?, ?, ?, ?, ?);",
            {core::newUuid(), jobId, engineName, toString(validation.status),
             validation.checks.dump(), core::utcNowIso()});
    }

    core::Json artifactsJson = core::Json::array();
    for (const auto& ref : engineResult.artifacts) {
        artifactsJson.push_back(ref.toJson());
    }
    core::Json response{{"success", engineResult.success},
                        {"engine", engineResult.engine},
                        {"operation", engineResult.operation},
                        {"result", engineResult.result},
                        {"artifacts", artifactsJson},
                        {"validation", validationJson},
                        {"errors", engineResult.errors},
                        {"job_id", jobId}};

    db_->execute(
        "UPDATE jobs SET status = ?, progress = 1.0, result = ?, updated_at = ? WHERE "
        "job_id = ?;",
        {std::string(engineResult.success ? "completed" : "failed"),
         engineResult.result.dump(), core::utcNowIso(), jobId});
    log.info("jobs", "job finished",
             core::Json{{"job_id", jobId}, {"success", engineResult.success}});
    return response;
}

Job JobManager::get(const std::string& jobId) const {
    const auto rows = db_->queryParams(
        "SELECT job_id, engine, operation, status, progress, request, result, error, "
        "created_at, updated_at FROM jobs WHERE job_id = ?;",
        {jobId});
    if (rows.empty() || rows[0].size() < 10) {
        throw core::JobNotFoundError("No job with id '" + jobId + "'");
    }
    return rowToJob(rows[0]);
}

std::vector<Job> JobManager::listRecent(int limit) const {
    const auto rows = db_->query(
        "SELECT job_id, engine, operation, status, progress, request, result, error, "
        "created_at, updated_at FROM jobs ORDER BY created_at DESC LIMIT " +
        std::to_string(limit) + ";");
    std::vector<Job> jobs;
    for (const auto& row : rows) {
        if (row.size() >= 10) {
            jobs.push_back(rowToJob(row));
        }
    }
    return jobs;
}

}  // namespace trinity::jobs
