#include "trinity/jobs/Job.hpp"

#include <filesystem>

#include "trinity/core/Logger.hpp"
#include "trinity/core/Time.hpp"
#include "trinity/core/Uuid.hpp"

namespace trinity::jobs {
namespace fs = std::filesystem;

namespace {

std::string sqlQuote(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out += '\'';
    for (char c : value) {
        if (c == '\'') {
            out += '\'';
        }
        out += c;
    }
    out += '\'';
    return out;
}

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

core::Json Job::toJson() const {
    return core::Json{{"job_id", jobId},
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

    db_->exec("INSERT INTO jobs (job_id, engine, operation, status, progress, request, "
              "result, error, created_at, updated_at) VALUES (" +
              sqlQuote(jobId) + ", " + sqlQuote(engineName) + ", " + sqlQuote(operation) +
              ", 'queued', 0.0, " + sqlQuote(parameters.dump()) + ", NULL, NULL, " +
              sqlQuote(now) + ", " + sqlQuote(now) + ");");
    db_->exec("UPDATE jobs SET status = 'running', progress = 0.1, updated_at = " +
              sqlQuote(core::utcNowIso()) + " WHERE job_id = " + sqlQuote(jobId) + ";");
    log.info("jobs", "job started",
             core::Json{{"job_id", jobId}, {"engine", engineName}, {"operation", operation}});

    std::shared_ptr<engines::IEngine> engine;
    try {
        engine = registry_->get(engineName);
    } catch (const core::TrinityError& exc) {
        db_->exec("UPDATE jobs SET status = 'failed', progress = 1.0, error = " +
                  sqlQuote(exc.toJson().dump()) + ", updated_at = " +
                  sqlQuote(core::utcNowIso()) + " WHERE job_id = " + sqlQuote(jobId) + ";");
        throw;
    }

    engines::EngineResult engineResult;
    try {
        engineResult = engine->execute(operation, parameters);
    } catch (const core::TrinityError& exc) {
        db_->exec("UPDATE jobs SET status = 'failed', progress = 1.0, error = " +
                  sqlQuote(exc.toJson().dump()) + ", updated_at = " +
                  sqlQuote(core::utcNowIso()) + " WHERE job_id = " + sqlQuote(jobId) + ";");
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
        const core::Json err = core::Json{{"code", "engine_execution_error"},
                                          {"message", exc.what()},
                                          {"details", core::Json::object()}};
        db_->exec("UPDATE jobs SET status = 'failed', progress = 1.0, error = " +
                  sqlQuote(err.dump()) + ", updated_at = " + sqlQuote(core::utcNowIso()) +
                  " WHERE job_id = " + sqlQuote(jobId) + ";");
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
        const auto& validation = *engineResult.validation;
        validationJson = validation.toJson();
        db_->exec("INSERT INTO validations (validation_id, job_id, engine, status, checks, "
                  "created_at) VALUES (" +
                  sqlQuote(core::newUuid()) + ", " + sqlQuote(jobId) + ", " +
                  sqlQuote(engineName) + ", " + sqlQuote(toString(validation.status)) + ", " +
                  sqlQuote(validation.checks.dump()) + ", " + sqlQuote(core::utcNowIso()) +
                  ");");
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

    db_->exec("UPDATE jobs SET status = '" +
              std::string(engineResult.success ? "completed" : "failed") +
              "', progress = 1.0, result = " + sqlQuote(engineResult.result.dump()) +
              ", updated_at = " + sqlQuote(core::utcNowIso()) + " WHERE job_id = " +
              sqlQuote(jobId) + ";");
    log.info("jobs", "job finished",
             core::Json{{"job_id", jobId}, {"success", engineResult.success}});
    return response;
}

Job JobManager::get(const std::string& jobId) const {
    const auto rows = db_->query(
        "SELECT job_id, engine, operation, status, progress, request, result, error, "
        "created_at, updated_at FROM jobs WHERE job_id = " +
        sqlQuote(jobId) + ";");
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
