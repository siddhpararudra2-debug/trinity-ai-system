#pragma once

// Job model and manager. Mirrors src/jobs/manager.py: every unit of
// engine work is a tracked job going queued -> running ->
// completed|failed, with timing, validation records, and a stable
// job_id for artifact lookups. Synchronous by design in V1.

#include <memory>
#include <string>
#include <vector>

#include "../artifacts/Artifact.hpp"
#include "../core/Json.hpp"
#include "../engines/EngineRegistry.hpp"
#include "../storage/Database.hpp"

namespace trinity::jobs {

enum class JobStatus {
    Queued,
    Running,
    Completed,
    Failed,
    Cancelled,
};

std::string toString(JobStatus status);
JobStatus fromString(const std::string& status);

enum class JobType {
    Engine,
    Workflow,
    Validation,
};

std::string jobTypeToString(JobType type);
JobType jobTypeFromString(const std::string& type);

struct Job {
    std::string jobId;
    std::string workflowId;
    std::string engine;
    std::string operation;
    JobStatus status = JobStatus::Queued;
    double progress = 0.0;
    core::Json request = core::Json::object();
    core::Json result = nullptr;
    core::Json error = nullptr;
    std::string createdAt;
    std::string updatedAt;

    bool succeeded() const noexcept { return status == JobStatus::Completed; }

    core::Json toJson() const;
    static Job fromJson(const core::Json& json);
};

class JobManager {
public:
    JobManager(std::shared_ptr<storage::Database> db,
               std::shared_ptr<engines::EngineRegistry> registry,
               std::shared_ptr<artifacts::ArtifactManager> artifacts);

    // Execute an engine call as a tracked job and return the
    // ToolResponse-style envelope. Unknown engines throw
    // EngineNotFoundError after the job row is marked failed.
    core::Json runSync(const std::string& engineName, const std::string& operation,
                       const core::Json& parameters);

    Job get(const std::string& jobId) const;
    std::vector<Job> listRecent(int limit = 50) const;

private:
    std::shared_ptr<storage::Database> db_;
    std::shared_ptr<engines::EngineRegistry> registry_;
    std::shared_ptr<artifacts::ArtifactManager> artifacts_;
};

}  // namespace trinity::jobs
