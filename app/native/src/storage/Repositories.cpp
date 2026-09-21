#include "trinity/storage/Repositories.hpp"

#include <map>

#include "trinity/core/Error.hpp"
#include "trinity/core/Time.hpp"
#include "trinity/storage/Migrations.hpp"

namespace trinity::storage {

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
    core::Json v = core::Json::parse(text, nullptr, false);
    if (v.is_discarded()) {
        return fallback;
    }
    return v;
}

jobs::Job jobFromRow(const Database::Row& row) {
    jobs::Job job;
    auto at = [&](size_t i) -> std::string { return i < row.size() ? row[i] : ""; };
    if (row.size() <= 10) {
        job.jobId = at(0);
        job.engine = at(1);
        job.operation = at(2);
        job.status = jobs::fromString(at(3));
        try {
            job.progress = at(4).empty() ? 0.0 : std::stod(at(4));
        } catch (...) {
            job.progress = 0.0;
        }
        job.request = parseOrNull(at(5));
        job.input = job.request.is_null() ? core::Json::object() : job.request;
        if (job.request.is_null()) {
            job.request = core::Json::object();
        }
        job.result = parseOrNull(at(6));
        job.error = parseOrNull(at(7));
        job.createdAt = at(8);
        job.updatedAt = at(9);
        return job;
    }
    job.jobId = at(0);
    job.engine = at(1);
    job.operation = at(2);
    job.status = jobs::fromString(at(3));
    try {
        job.progress = at(4).empty() ? 0.0 : std::stod(at(4));
    } catch (...) {
        job.progress = 0.0;
    }
    job.request = parseOrNull(at(5));
    job.input = job.request.is_null() ? core::Json::object() : job.request;
    if (job.request.is_null()) {
        job.request = core::Json::object();
    }
    job.result = parseOrNull(at(6));
    job.error = parseOrNull(at(7));
    job.createdAt = at(8);
    job.updatedAt = at(9);
    job.requestId = at(10);
    job.workflowId = at(11);
    job.startedAt = at(12);
    job.completedAt = at(13);
    core::Json arr = parseOr(at(14), core::Json::array());
    if (arr.is_array()) {
        for (const auto& item : arr) {
            if (item.is_string()) {
                job.artifactIds.push_back(item.get<std::string>());
            }
        }
    }
    job.metadata = parseOr(at(15), core::Json::object());
    if (job.metadata.is_null()) {
        job.metadata = core::Json::object();
    }
    try {
        job.timeoutMs = at(16).empty() ? 0 : std::stoi(at(16));
        job.retry.attempt = at(17).empty() ? 0 : std::stoi(at(17));
        job.retry.maxRetries = at(18).empty() ? 0 : std::stoi(at(18));
        job.retry.delayMs = at(19).empty() ? 0 : std::stoi(at(19));
    } catch (...) {
    }
    return job;
}

}  // namespace

JobRepository::JobRepository(std::shared_ptr<Database> db) : db_(std::move(db)) {}

void JobRepository::save(const jobs::Job& job) {
    core::Json arr = core::Json::array();
    for (const auto& id : job.artifactIds) {
        arr.push_back(id);
    }
    core::Json meta = job.metadata;
    if (meta.is_null()) {
        meta = core::Json::object();
    }
    core::Json req = job.request.is_null() ? job.input : job.request;
    if (req.is_null()) {
        req = core::Json::object();
    }
    if (tableHasColumn(*db_, "jobs", "request_id")) {
        db_->execute(
            "INSERT INTO jobs (job_id, engine, operation, status, progress, request, result, "
            "error, created_at, updated_at, request_id, workflow_id, started_at, completed_at, "
            "artifacts, metadata, timeout_ms, retry_count, max_retries, retry_delay_ms) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(job_id) DO UPDATE SET engine=excluded.engine, "
            "operation=excluded.operation, status=excluded.status, progress=excluded.progress, "
            "request=excluded.request, result=excluded.result, error=excluded.error, "
            "updated_at=excluded.updated_at, request_id=excluded.request_id, "
            "workflow_id=excluded.workflow_id, started_at=excluded.started_at, "
            "completed_at=excluded.completed_at, artifacts=excluded.artifacts, "
            "metadata=excluded.metadata, timeout_ms=excluded.timeout_ms, "
            "retry_count=excluded.retry_count, max_retries=excluded.max_retries, "
            "retry_delay_ms=excluded.retry_delay_ms;",
            {job.jobId, job.engine, job.operation, jobs::toString(job.status), job.progress,
             req.dump(), job.result.dump(), job.error.dump(), job.createdAt, job.updatedAt,
             job.requestId, job.workflowId, job.startedAt, job.completedAt, arr.dump(),
             meta.dump(), std::int64_t(job.timeoutMs), std::int64_t(job.retry.attempt),
             std::int64_t(job.retry.maxRetries), std::int64_t(job.retry.delayMs)});
        return;
    }
    db_->execute(
        "INSERT INTO jobs (job_id, engine, operation, status, progress, request, result, "
        "error, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(job_id) DO UPDATE SET engine=excluded.engine, "
        "operation=excluded.operation, status=excluded.status, progress=excluded.progress, "
        "request=excluded.request, result=excluded.result, error=excluded.error, "
        "updated_at=excluded.updated_at;",
        {job.jobId, job.engine, job.operation, jobs::toString(job.status), job.progress,
         req.dump(), job.result.dump(), job.error.dump(), job.createdAt, job.updatedAt});
}

jobs::Job JobRepository::get(const std::string& jobId) const {
    const bool extended = tableHasColumn(*db_, "jobs", "request_id");
    const std::string sql =
        extended
            ? "SELECT job_id, engine, operation, status, progress, request, result, error, "
              "created_at, updated_at, request_id, workflow_id, started_at, completed_at, "
              "artifacts, metadata, timeout_ms, retry_count, max_retries, retry_delay_ms FROM "
              "jobs WHERE job_id = ?;"
            : "SELECT job_id, engine, operation, status, progress, request, result, error, "
              "created_at, updated_at FROM jobs WHERE job_id = ?;";
    const auto rows = db_->queryParams(sql, {jobId});
    if (rows.empty()) {
        throw core::JobNotFoundError("No job with id '" + jobId + "'", {}, "storage");
    }
    return jobFromRow(rows[0]);
}

std::vector<jobs::Job> JobRepository::listRecent(int limit) const {
    const bool extended = tableHasColumn(*db_, "jobs", "request_id");
    const std::string sql =
        extended
            ? "SELECT job_id, engine, operation, status, progress, request, result, error, "
              "created_at, updated_at, request_id, workflow_id, started_at, completed_at, "
              "artifacts, metadata, timeout_ms, retry_count, max_retries, retry_delay_ms FROM "
              "jobs ORDER BY created_at DESC LIMIT " +
              std::to_string(limit) + ";"
            : "SELECT job_id, engine, operation, status, progress, request, result, error, "
              "created_at, updated_at FROM jobs ORDER BY created_at DESC LIMIT " +
              std::to_string(limit) + ";";
    const auto rows = db_->query(sql);
    std::vector<jobs::Job> out;
    for (const auto& row : rows) {
        out.push_back(jobFromRow(row));
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
    // Overlay live node rows (status/result updated during execution) onto
    // the definition nodes (which carry edges/name/type/inputFrom).
    const auto liveNodes = listNodes(workflowId);
    if (!liveNodes.empty()) {
        std::map<std::string, workflows::WorkflowNode> liveById;
        for (const auto& n : liveNodes) {
            liveById[n.effectiveId()] = n;
        }
        for (auto& n : workflow.nodes) {
            auto it = liveById.find(n.effectiveId());
            if (it != liveById.end()) {
                n.status = it->second.status;
                n.result = it->second.result;
                n.error = it->second.error;
                n.retryCount = it->second.retryCount;
                n.maxRetries = it->second.maxRetries;
                n.retryDelayMs = it->second.retryDelayMs;
                // dependencies stay edge-authoritative; live row deps match.
            }
        }
    }
    if (!workflow.edges.empty()) {
        workflows::deriveDependencies(workflow);
    }
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
    core::Json params = node.parameters.is_null() ? node.input : node.parameters;
    if (params.is_null()) {
        params = core::Json::object();
    }
    if (tableHasColumn(*db_, "workflow_nodes", "result")) {
        core::Json deps = core::Json::array();
        for (const auto& d : node.dependencies) {
            deps.push_back(d);
        }
        core::Json inputFrom = node.inputFrom;
        if (inputFrom.is_null()) {
            inputFrom = core::Json::object();
        }
        db_->execute(
            "INSERT INTO workflow_nodes (node_id, workflow_id, engine, operation, parameters, "
            "status, created_at, updated_at, result, error, retry_count, max_retries, "
            "retry_delay_ms, input_from, allow_failure, dependencies) VALUES (?, ?, ?, ?, ?, "
            "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(node_id) DO UPDATE SET engine=excluded.engine, "
            "operation=excluded.operation, parameters=excluded.parameters, "
            "status=excluded.status, updated_at=excluded.updated_at, result=excluded.result, "
            "error=excluded.error, retry_count=excluded.retry_count, "
            "max_retries=excluded.max_retries, retry_delay_ms=excluded.retry_delay_ms, "
            "input_from=excluded.input_from, allow_failure=excluded.allow_failure, "
            "dependencies=excluded.dependencies;",
            {nodeId, workflowId, node.effectiveEngine(), node.operation, params.dump(),
             workflows::toString(node.status), core::utcNowIso(), core::utcNowIso(),
             node.result.dump(), node.error.dump(), std::int64_t(node.retryCount),
             std::int64_t(node.maxRetries), std::int64_t(node.retryDelayMs), inputFrom.dump(),
             std::int64_t(node.allowFailure ? 1 : 0), deps.dump()});
        return;
    }
    db_->execute(
        "INSERT INTO workflow_nodes (node_id, workflow_id, engine, operation, parameters, "
        "status, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(node_id) DO UPDATE SET engine=excluded.engine, "
        "operation=excluded.operation, parameters=excluded.parameters, "
        "status=excluded.status, updated_at=excluded.updated_at;",
        {nodeId, workflowId, node.effectiveEngine(), node.operation, params.dump(),
         workflows::toString(node.status), core::utcNowIso(), core::utcNowIso()});
}

std::vector<workflows::WorkflowNode> WorkflowRepository::listNodes(
    const std::string& workflowId) const {
    const bool extended = tableHasColumn(*db_, "workflow_nodes", "result");
    const std::string sql =
        extended
            ? "SELECT node_id, engine, operation, parameters, status, result, error, "
              "retry_count, max_retries, retry_delay_ms, input_from, allow_failure, "
              "dependencies FROM workflow_nodes WHERE workflow_id = ? ORDER BY node_id;"
            : "SELECT node_id, engine, operation, parameters, status FROM workflow_nodes WHERE "
              "workflow_id = ? ORDER BY node_id;";
    const auto rows = db_->queryParams(sql, {workflowId});
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
        node.input = node.parameters.is_null() ? core::Json::object() : node.parameters;
        if (node.parameters.is_null()) {
            node.parameters = core::Json::object();
        }
        node.status = workflows::nodeStatusFromString(row[4]);
        if (extended && row.size() >= 13) {
            node.result = parseOrNull(row[5]);
            node.error = parseOrNull(row[6]);
            try {
                node.retryCount = row[7].empty() ? 0 : std::stoi(row[7]);
                node.maxRetries = row[8].empty() ? 0 : std::stoi(row[8]);
                node.retryDelayMs = row[9].empty() ? 0 : std::stoi(row[9]);
            } catch (...) {
            }
            node.inputFrom = parseOr(row[10], core::Json::object());
            if (node.inputFrom.is_null()) {
                node.inputFrom = core::Json::object();
            }
            node.allowFailure = row[11] == "1";
            core::Json deps = parseOr(row[12], core::Json::array());
            if (deps.is_array()) {
                for (const auto& d : deps) {
                    if (d.is_string()) {
                        node.dependencies.push_back(d.get<std::string>());
                    }
                }
            }
        }
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
