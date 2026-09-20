#include "trinity/storage/Repositories.hpp"

#include "trinity/core/Error.hpp"
#include "trinity/core/Time.hpp"

namespace trinity::storage {

namespace {

core::Json parseOrNull(const std::string& text) {
    if (text.empty()) {
        return core::Json(nullptr);
    }
    return core::Json::parse(text, nullptr, false);
}

jobs::Job jobFromRow(const Database::Row& row) {
    jobs::Job job;
    job.jobId = row[0];
    job.engine = row[1];
    job.operation = row[2];
    job.status = jobs::fromString(row[3]);
    job.progress = row[4].empty() ? 0.0 : std::stod(row[4]);
    job.request = parseOrNull(row[5]);
    job.result = parseOrNull(row[6]);
    job.error = parseOrNull(row[7]);
    job.createdAt = row[8];
    job.updatedAt = row[9];
    return job;
}

}  // namespace

JobRepository::JobRepository(std::shared_ptr<Database> db) : db_(std::move(db)) {}

void JobRepository::save(const jobs::Job& job) {
    db_->execute(
        "INSERT INTO jobs (job_id, engine, operation, status, progress, request, result, "
        "error, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(job_id) DO UPDATE SET engine=excluded.engine, "
        "operation=excluded.operation, status=excluded.status, progress=excluded.progress, "
        "request=excluded.request, result=excluded.result, error=excluded.error, "
        "updated_at=excluded.updated_at;",
        {job.jobId, job.engine, job.operation, jobs::toString(job.status), job.progress,
         job.request.dump(), job.result.dump(), job.error.dump(), job.createdAt,
         job.updatedAt});
}

jobs::Job JobRepository::get(const std::string& jobId) const {
    const auto rows = db_->queryParams(
        "SELECT job_id, engine, operation, status, progress, request, result, error, "
        "created_at, updated_at FROM jobs WHERE job_id = ?;",
        {jobId});
    if (rows.empty() || rows[0].size() < 10) {
        throw core::JobNotFoundError("No job with id '" + jobId + "'", {}, "storage");
    }
    return jobFromRow(rows[0]);
}

std::vector<jobs::Job> JobRepository::listRecent(int limit) const {
    const auto rows = db_->query(
        "SELECT job_id, engine, operation, status, progress, request, result, error, "
        "created_at, updated_at FROM jobs ORDER BY created_at DESC LIMIT " +
        std::to_string(limit) + ";");
    std::vector<jobs::Job> out;
    for (const auto& row : rows) {
        if (row.size() >= 10) {
            out.push_back(jobFromRow(row));
        }
    }
    return out;
}

void JobRepository::remove(const std::string& jobId) {
    db_->execute("DELETE FROM jobs WHERE job_id = ?;", {jobId});
}

WorkflowRepository::WorkflowRepository(std::shared_ptr<Database> db) : db_(std::move(db)) {
}

void WorkflowRepository::saveWorkflow(const workflows::Workflow& workflow) {
    Transaction tx(*db_);
    db_->execute(
        "INSERT INTO workflows (workflow_id, name, status, definition, created_at, "
        "updated_at) VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(workflow_id) DO UPDATE SET "
        "name=excluded.name, status=excluded.status, definition=excluded.definition, "
        "updated_at=excluded.updated_at;",
        {workflow.workflowId, workflow.name, workflows::toString(workflow.status),
         workflow.toJson().dump(), workflow.createdAt, workflow.updatedAt});
    db_->execute("DELETE FROM workflow_nodes WHERE workflow_id = ?;",
                 {workflow.workflowId});
    for (const auto& node : workflow.nodes) {
        saveNode(workflow.workflowId, node);
    }
    tx.commit();
}

workflows::Workflow WorkflowRepository::getWorkflow(const std::string& workflowId) const {
    const auto rows = db_->queryParams(
        "SELECT workflow_id, name, status, definition, created_at, updated_at FROM "
        "workflows WHERE workflow_id = ?;",
        {workflowId});
    if (rows.empty() || rows[0].size() < 6) {
        throw core::TrinityError("No workflow with id '" + workflowId + "'", {},
                                 core::ErrorCode::TrinityError, "storage");
    }
    workflows::Workflow workflow =
        workflows::Workflow::fromJson(core::Json::parse(rows[0][3]));
    workflow.workflowId = rows[0][0];
    workflow.name = rows[0][1];
    workflow.status = workflows::workflowStatusFromString(rows[0][2]);
    workflow.createdAt = rows[0][4];
    workflow.updatedAt = rows[0][5];
    workflow.nodes = listNodes(workflowId);
    return workflow;
}

std::vector<workflows::Workflow> WorkflowRepository::listRecent(int limit) const {
    const auto rows = db_->query(
        "SELECT workflow_id, name, status, definition, created_at, updated_at FROM "
        "workflows ORDER BY created_at DESC LIMIT " +
        std::to_string(limit) + ";");
    std::vector<workflows::Workflow> out;
    for (const auto& row : rows) {
        if (row.size() < 6) {
            continue;
        }
        workflows::Workflow workflow =
            workflows::Workflow::fromJson(core::Json::parse(row[3]));
        workflow.workflowId = row[0];
        workflow.name = row[1];
        workflow.status = workflows::workflowStatusFromString(row[2]);
        workflow.createdAt = row[4];
        workflow.updatedAt = row[5];
        out.push_back(std::move(workflow));
    }
    return out;
}

void WorkflowRepository::saveNode(const std::string& workflowId,
                                  const workflows::WorkflowNode& node) {
    const std::string nodeId = node.nodeId.empty() ? node.id : node.nodeId;
    db_->execute(
        "INSERT INTO workflow_nodes (node_id, workflow_id, engine, operation, parameters, "
        "status, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(node_id) DO UPDATE SET engine=excluded.engine, "
        "operation=excluded.operation, parameters=excluded.parameters, "
        "status=excluded.status, updated_at=excluded.updated_at;",
        {nodeId, workflowId, node.engine, node.operation, node.parameters.dump(),
         workflows::toString(node.status), core::utcNowIso(), core::utcNowIso()});
}

std::vector<workflows::WorkflowNode> WorkflowRepository::listNodes(
    const std::string& workflowId) const {
    const auto rows = db_->queryParams(
        "SELECT node_id, engine, operation, parameters, status FROM workflow_nodes WHERE "
        "workflow_id = ? ORDER BY node_id;",
        {workflowId});
    std::vector<workflows::WorkflowNode> out;
    for (const auto& row : rows) {
        if (row.size() < 5) {
            continue;
        }
        workflows::WorkflowNode node;
        node.id = row[0];
        node.nodeId = row[0];
        node.engine = row[1];
        node.operation = row[2];
        node.parameters = parseOrNull(row[3]);
        node.status = workflows::nodeStatusFromString(row[4]);
        out.push_back(std::move(node));
    }
    return out;
}

ArtifactRepository::ArtifactRepository(std::shared_ptr<Database> db) : db_(std::move(db)) {
}

void ArtifactRepository::save(const artifacts::Artifact& artifact) {
    db_->execute(
        "INSERT INTO artifacts (artifact_id, job_id, type, path, size_bytes, checksum, "
        "created_at) VALUES (?, ?, ?, ?, ?, ?, ?) ON CONFLICT(artifact_id) DO UPDATE SET "
        "type=excluded.type, path=excluded.path, size_bytes=excluded.size_bytes, "
        "checksum=excluded.checksum;",
        {artifact.artifactId, artifact.jobId, artifact.type, artifact.path,
         static_cast<std::int64_t>(artifact.sizeBytes), artifact.checksum,
         artifact.createdAt});
}

artifacts::Artifact ArtifactRepository::get(const std::string& artifactId) const {
    const auto rows = db_->queryParams(
        "SELECT artifact_id, job_id, type, path, size_bytes, checksum, created_at FROM "
        "artifacts WHERE artifact_id = ?;",
        {artifactId});
    if (rows.empty() || rows[0].size() < 7) {
        throw core::ArtifactNotFoundError("No artifact with id '" + artifactId + "'", {},
                                          "storage");
    }
    artifacts::Artifact artifact;
    artifact.artifactId = rows[0][0];
    artifact.jobId = rows[0][1];
    artifact.type = rows[0][2];
    artifact.path = rows[0][3];
    artifact.sizeBytes = std::stoll(rows[0][4].empty() ? "0" : rows[0][4]);
    artifact.checksum = rows[0][5];
    artifact.createdAt = rows[0][6];
    return artifact;
}

std::vector<artifacts::Artifact> ArtifactRepository::listForJob(
    const std::string& jobId) const {
    const auto rows = db_->queryParams(
        "SELECT artifact_id, job_id, type, path, size_bytes, checksum, created_at FROM "
        "artifacts WHERE job_id = ? ORDER BY created_at;",
        {jobId});
    std::vector<artifacts::Artifact> out;
    for (const auto& row : rows) {
        if (row.size() < 7) {
            continue;
        }
        artifacts::Artifact artifact;
        artifact.artifactId = row[0];
        artifact.jobId = row[1];
        artifact.type = row[2];
        artifact.path = row[3];
        artifact.sizeBytes = std::stoll(row[4].empty() ? "0" : row[4]);
        artifact.checksum = row[5];
        artifact.createdAt = row[6];
        out.push_back(std::move(artifact));
    }
    return out;
}

}  // namespace trinity::storage
