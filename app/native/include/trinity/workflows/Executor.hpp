#pragma once

// Deterministic sequential DAG executor (§6).
// Flow: Validate DAG -> Topological Order -> Create Jobs -> Execute Node
// -> Validate Result -> Pass Result To Dependent -> Continue.
// A failed node prevents dependents unless allowFailure is set.
// Engine selection stays centralized: Executor -> JobManager ->
// EngineRegistry -> IEngine (never instantiates engines directly).

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../core/Json.hpp"
#include "../jobs/Job.hpp"
#include "Workflow.hpp"

namespace trinity::engines {
class EngineRegistry;
}

namespace trinity::storage {
class WorkflowRepository;
}

namespace trinity::workflows {

struct WorkflowResult {
    bool success = false;
    Workflow workflow;
    std::map<std::string, core::Json> nodeResults;
    std::vector<std::string> failedNodes;
    std::string currentNode;
    core::Json error = nullptr;

    core::Json toJson() const;
};

class WorkflowExecutor {
public:
    WorkflowExecutor(std::shared_ptr<jobs::JobManager> jobs,
                     std::shared_ptr<engines::EngineRegistry> registry,
                     std::shared_ptr<storage::WorkflowRepository> workflows);

    // Blocking deterministic execution (tests, worker thread, headless).
    WorkflowResult runInline(Workflow workflow,
                             std::shared_ptr<jobs::CancellationToken> token = nullptr);
    // Persist-only helpers for the async path (UI polling).
    void save(const Workflow& workflow);
    Workflow get(const std::string& workflowId);
    std::vector<Workflow> listRecent(int limit = 50);

private:
    std::shared_ptr<jobs::JobManager> jobs_;
    std::shared_ptr<engines::EngineRegistry> registry_;
    std::shared_ptr<storage::WorkflowRepository> workflows_;
};

}  // namespace trinity::workflows
