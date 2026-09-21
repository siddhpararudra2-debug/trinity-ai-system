#pragma once

// Job model and manager. Mirrors src/jobs/manager.py: every unit of
// engine work is a tracked job going queued -> running ->
// completed|failed, with timing, validation records, and a stable
// job_id for artifact lookups.
//
// Lifecycle:
//   Request -> Create Job -> QUEUED -> RUNNING -> Engine Execution
//   -> Validation -> COMPLETED / FAILED
//   QUEUED/RUNNING -> CANCELLED
// A job is COMPLETED only when engine execution and required
// validation actually succeed.
//
// Future states RETRYING / WAITING_APPROVAL are reserved in the enum
// (string round-trip supported) so they can be enabled later without
// a schema break. Transitions into them are rejected for now.

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
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
    // Reserved for future phases (see §10). Persisted as strings so
    // old rows keep loading; transitions into them are rejected until
    // retry/approval policies are implemented.
    Retrying,
    WaitingApproval,
};

std::string toString(JobStatus status);
JobStatus fromString(const std::string& status);
/// True when status is terminal (no further transitions allowed).
bool isTerminal(JobStatus status) noexcept;
/// True when QUEUED/RUNNING/etc may move to `next`. Reserved future
/// states always return false for now.
bool canTransition(JobStatus from, JobStatus to) noexcept;

enum class JobType {
    Engine,
    Workflow,
    Validation,
};

std::string jobTypeToString(JobType type);
JobType jobTypeFromString(const std::string& type);

/// Future retry policy (§10). Stored in Job::metadata + Job::retry so
/// policies can be added later without changing the job table again.
struct RetryPolicy {
    int attempt = 0;
    int maxRetries = 0;
    int delayMs = 0;

    core::Json toJson() const;
    static RetryPolicy fromJson(const core::Json& json);
};

/// Cooperative cancellation token (§10). Queued jobs cancel reliably
/// by never starting. Running engines in V1 are synchronous, so the
/// token is checked before/after execution and between workflow nodes;
/// engines are NOT claimed to be instantly preemptible.
class CancellationToken {
public:
    CancellationToken() : flag_(std::make_shared<std::atomic_bool>(false)) {}
    void cancel() noexcept { flag_->store(true); }
    bool isCancelled() const noexcept { return flag_->load(); }

private:
    std::shared_ptr<std::atomic_bool> flag_;
};

struct JobCreateOptions {
    std::string requestId;
    std::string workflowId;
    core::Json metadata = core::Json::object();
    int timeoutMs = 0;
    RetryPolicy retry;
};

struct Job {
    std::string jobId;
    std::string requestId;
    std::string workflowId;
    std::string engine;
    std::string operation;
    JobStatus status = JobStatus::Queued;
    double progress = 0.0;
    // `input` is canonical; `request` is the legacy alias. Both are
    // kept in sync by JobManager so old rows/tests keep working.
    core::Json input = core::Json::object();
    core::Json request = core::Json::object();
    core::Json result = nullptr;
    core::Json error = nullptr;
    std::string createdAt;
    std::string startedAt;
    std::string completedAt;
    std::string updatedAt;
    std::vector<std::string> artifactIds;
    core::Json metadata = core::Json::object();
    int timeoutMs = 0;
    RetryPolicy retry;

    bool succeeded() const noexcept { return status == JobStatus::Completed; }
    bool canTransitionTo(JobStatus next) const noexcept { return canTransition(status, next); }

    core::Json toJson() const;
    static Job fromJson(const core::Json& json);
};

class JobManager {
public:
    JobManager(std::shared_ptr<storage::Database> db,
               std::shared_ptr<engines::EngineRegistry> registry,
               std::shared_ptr<artifacts::ArtifactManager> artifacts);

    // --- Lifecycle (§2/§3) ---
    // Create a QUEUED job row (transaction + prepared statement).
    Job createJob(const std::string& engineName, const std::string& operation,
                  const core::Json& input, const JobCreateOptions& options = {});
    // Execute a QUEUED job to a terminal state. Returns the
    // ToolResponse-style envelope. Unknown engines mark FAILED then throw
    // EngineNotFoundError. Only COMPLETED when engine + validation pass.
    core::Json executeJob(const std::string& jobId,
                          std::shared_ptr<CancellationToken> token = nullptr);
    // Legacy convenience: create + execute inline (used by tests/headless).
    core::Json runSync(const std::string& engineName, const std::string& operation,
                       const core::Json& parameters);

    // Reliable cancel for QUEUED; cooperative request for RUNNING.
    bool cancel(const std::string& jobId);
    bool isCancellationRequested(const std::string& jobId) const;

    void updateStatus(const std::string& jobId, JobStatus status);
    void updateResult(const std::string& jobId, const core::Json& result);
    void updateError(const std::string& jobId, const core::Json& error);
    void attachArtifacts(const std::string& jobId,
                         const std::vector<std::string>& artifactIds);

    Job get(const std::string& jobId) const;
    std::vector<Job> listRecent(int limit = 50) const;
    // Re-queue interrupted QUEUED/RUNNING rows on startup (§3).
    int recoverOnStartup();

private:
    Job getLocked(const std::string& jobId) const;
    void persistLocked(const Job& job);
    void setStatusLocked(Job& job, JobStatus status);

    std::shared_ptr<storage::Database> db_;
    std::shared_ptr<engines::EngineRegistry> registry_;
    std::shared_ptr<artifacts::ArtifactManager> artifacts_;
    mutable std::mutex mutex_;
    std::set<std::string> cancelRequested_;
};

}  // namespace trinity::jobs
