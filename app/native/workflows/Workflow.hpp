// Trinity — Workflow/DAG execution (brief §Workflow/DAG).
// Port of backend/app/workflows/dag.py semantics: topological ordering with
// duplicate-id, cycle and missing-dependency rejection — executed over the
// async job system so every node is a tracked job.
#pragma once

#include <string>
#include <vector>

#include "../core/Error.hpp"
#include "../core/Json.hpp"
#include "../jobs/JobSystem.hpp"

namespace trinity::workflows {

struct WorkflowNode {
    std::string id;
    std::string engine;
    std::string operation;
    core::Json parameters = core::Json::object();
    std::vector<std::string> dependencies;
};

// Topologically orders nodes; fails on duplicates, missing deps, cycles
// (same semantics as the Python ordered()).
core::Result<std::vector<WorkflowNode>> topological_order(const std::vector<WorkflowNode>& nodes);

struct WorkflowRunResult {
    std::string workflow_id;
    std::vector<std::string> job_ids;  // in topological execution order
};

// Canonical workflow interface (brief §Core interfaces: IWorkflow). A workflow
// is a named, dependency-ordered set of engine steps executed as tracked jobs.
class IWorkflow {
public:
    virtual ~IWorkflow() = default;

    virtual core::Result<WorkflowRunResult> run(const std::string& project_id,
                                                const std::string& name,
                                                const std::vector<WorkflowNode>& nodes) = 0;
    virtual core::Result<std::vector<std::string>> list_workflows() const = 0;
};

class WorkflowRunner : public IWorkflow {
public:
    WorkflowRunner(jobs::JobSystem& job_system, db::Database& db)
        : job_system_(&job_system), db_(&db) {}

    // Persists a workflow definition and submits its nodes as jobs.
    // Nodes run in topological order; a scaffolded engine fails the run
    // before anything is submitted.
    core::Result<WorkflowRunResult> run(const std::string& project_id, const std::string& name,
                                        const std::vector<WorkflowNode>& nodes) override;

    core::Result<std::vector<std::string>> list_workflows() const override;

private:
    jobs::JobSystem* job_system_;
    db::Database* db_;
};

}  // namespace trinity::workflows
