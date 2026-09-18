#include "Workflow.hpp"

#include <algorithm>

#include "../core/Logging.hpp"
#include "../core/Time.hpp"
#include "../core/Uuid.hpp"
#include "../db/Database.hpp"
#include "../engines/Engine.hpp"

namespace trinity::workflows {
namespace {
core::ComponentLog log_("workflows");
}

core::Result<std::vector<WorkflowNode>> topological_order(
    const std::vector<WorkflowNode>& nodes) {
    // Duplicate ids (test_workflow_rejects_duplicate_node_ids semantics).
    std::map<std::string, const WorkflowNode*> by_id;
    for (const WorkflowNode& node : nodes) {
        if (!by_id.emplace(node.id, &node).second) {
            return core::Result<std::vector<WorkflowNode>>::fail(core::Error(
                core::ErrorCode::RequestValidationError, "Workflow has duplicate node IDs"));
        }
    }
    // Missing dependencies.
    for (const WorkflowNode& node : nodes) {
        for (const std::string& dep : node.dependencies) {
            if (!by_id.count(dep)) {
                return core::Result<std::vector<WorkflowNode>>::fail(
                    core::Error(core::ErrorCode::RequestValidationError,
                                "Workflow has a missing dependency or cycle: '" + node.id +
                                    "' depends on unknown '" + dep + "'"));
            }
        }
    }
    // Kahn's algorithm with cycle detection.
    std::map<std::string, int> in_degree;
    std::map<std::string, std::vector<std::string>> dependents;
    for (const WorkflowNode& node : nodes) {
        in_degree.emplace(node.id, 0);
    }
    for (const WorkflowNode& node : nodes) {
        for (const std::string& dep : node.dependencies) {
            dependents[dep].push_back(node.id);
            in_degree[node.id] += 1;
        }
    }
    std::vector<WorkflowNode> ordered;
    std::vector<std::string> ready;
    for (const auto& [id, degree] : in_degree) {
        if (degree == 0) ready.push_back(id);
    }
    std::sort(ready.begin(), ready.end());
    while (!ready.empty()) {
        const std::string current = ready.back();
        ready.pop_back();
        ordered.push_back(*by_id.at(current));
        for (const std::string& next : dependents[current]) {
            if (--in_degree[next] == 0) ready.push_back(next);
        }
    }
    if (ordered.size() != nodes.size()) {
        return core::Result<std::vector<WorkflowNode>>::fail(core::Error(
            core::ErrorCode::RequestValidationError, "Workflow has a missing dependency or cycle"));
    }
    return core::Result<std::vector<WorkflowNode>>::ok(std::move(ordered));
}

WorkflowRunner::WorkflowRunner(jobs::JobSystem& job_system, db::Database& db)
    : job_system_(&job_system), db_(&db) {}

core::Result<WorkflowRunResult> WorkflowRunner::run(const std::string& project_id,
                                                    const std::string& name,
                                                    const std::vector<WorkflowNode>& nodes) {
    auto ordered = topological_order(nodes);
    if (ordered.is_error()) return core::Result<WorkflowRunResult>::fail(ordered.take_error());

    // Fail early on scaffolded engines (honest refusal before submission).
    for (const WorkflowNode& node : ordered.value()) {
        auto engine = engines::EngineRegistry::instance().get(node.engine);
        if (engine.is_error()) {
            return core::Result<WorkflowRunResult>::fail(engine.take_error());
        }
        if (engine.value()->health() == engines::EngineHealth::Scaffolded) {
            return core::Result<WorkflowRunResult>::fail(core::Error(
                core::ErrorCode::CapabilityUnavailableError,
                "workflow node '" + node.id + "' uses scaffolded engine '" + node.engine + "'"));
        }
    }

    const std::string workflow_id = core::new_uuid();
    core::Json definition = core::Json::array();
    for (const WorkflowNode& node : nodes) {
        core::Json entry = core::Json::object();
        entry["id"] = node.id;
        entry["engine"] = node.engine;
        entry["operation"] = node.operation;
        entry["parameters"] = node.parameters;
        core::Json deps = core::Json::array();
        for (const std::string& dep : node.dependencies) deps.push_back(core::Json(dep));
        entry["dependencies"] = deps;
        definition.push_back(entry);
    }

    // Persist the workflow definition first (status RUNNING).
    const std::string now = core::iso_utc_now();
    if (auto persisted = db_->run(
            "INSERT INTO workflows (workflow_id, project_id, name, definition, status, "
            "created_at, updated_at) VALUES (?, ?, ?, ?, 'RUNNING', ?, ?);",
            {core::Json(workflow_id), core::Json(project_id), core::Json(name),
             core::Json(definition.dump()), core::Json(now), core::Json(now)});
        persisted.is_error()) {
        return core::Result<WorkflowRunResult>::fail(persisted.take_error());
    }

    WorkflowRunResult result;
    result.workflow_id = workflow_id;
    for (const WorkflowNode& node : ordered.value()) {
        auto engine = engines::EngineRegistry::instance().get(node.engine);
        auto submitted = job_system_->submit(
            node.engine, node.operation, node.parameters,
            [engine = engine.value(), operation = node.operation](
                jobs::JobContext& context, const core::Json& request) {
                    context.log("workflow node executing " + operation);
                    engines::ExecutionOutput output = engine->execute(operation, request);
                    core::Json out = output.result;
                    out["validation_status"] = output.validation_status;
                    return out;
                },
            project_id);
        if (submitted.is_error()) {
            db_->run("UPDATE workflows SET status = 'FAILED', updated_at = ? WHERE workflow_id = ?;",
                     {core::Json(core::iso_utc_now()), core::Json(workflow_id)});
            return core::Result<WorkflowRunResult>::fail(submitted.take_error());
        }
        result.job_ids.push_back(submitted.value());
    }

    log_.info("workflow submitted", [&] {
        core::Json ctx = core::Json::object();
        ctx["workflow_id"] = workflow_id;
        ctx["nodes"] = static_cast<double>(result.job_ids.size());
        return ctx;
    }());
    return core::Result<WorkflowRunResult>::ok(std::move(result));
}

core::Result<std::vector<std::string>> WorkflowRunner::list_workflows() const {
    auto rows = db_->query("SELECT workflow_id FROM workflows ORDER BY created_at DESC;");
    if (rows.is_error()) return core::Result<std::vector<std::string>>::fail(rows.take_error());
    std::vector<std::string> ids;
    ids.reserve(rows.value().size());
    for (const auto& row : rows.value()) ids.push_back(row[0].as_string());
    return core::Result<std::vector<std::string>>::ok(std::move(ids));
}

}  // namespace trinity::workflows
