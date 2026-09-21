#include "trinity/workflows/Executor.hpp"

#include "trinity/core/Error.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/core/Time.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/storage/Repositories.hpp"

namespace trinity::workflows {

core::Json WorkflowResult::toJson() const {
    core::Json results = core::Json::object();
    for (const auto& [k, v] : nodeResults) {
        results[k] = v;
    }
    core::Json failed = core::Json::array();
    for (const auto& n : failedNodes) {
        failed.push_back(n);
    }
    return core::Json{{"success", success},
                      {"workflow", workflow.toJson()},
                      {"node_results", results},
                      {"failed_nodes", failed},
                      {"current_node", currentNode},
                      {"error", error}};
}

WorkflowExecutor::WorkflowExecutor(std::shared_ptr<jobs::JobManager> jobs,
                                   std::shared_ptr<engines::EngineRegistry> registry,
                                   std::shared_ptr<storage::WorkflowRepository> workflows)
    : jobs_(std::move(jobs)), registry_(std::move(registry)), workflows_(std::move(workflows)) {
}

void WorkflowExecutor::save(const Workflow& workflow) {
    workflows_->saveWorkflow(workflow);
}

Workflow WorkflowExecutor::get(const std::string& workflowId) {
    return workflows_->getWorkflow(workflowId);
}

std::vector<Workflow> WorkflowExecutor::listRecent(int limit) {
    return workflows_->listRecent(limit);
}

WorkflowResult WorkflowExecutor::runInline(
    Workflow workflow, std::shared_ptr<jobs::CancellationToken> token) {
    auto& log = core::Logger::instance();
    WorkflowResult out;
    out.error = nullptr;

    if (workflow.workflowId.empty()) {
        workflow.workflowId = core::newUuid();
    }
    if (workflow.createdAt.empty()) {
        workflow.createdAt = core::utcNowIso();
    }
    workflow.startedAt = core::utcNowIso();
    workflow.updatedAt = workflow.startedAt;
    workflow.status = WorkflowStatus::Running;

    log.info("workflow", "workflow started",
             core::Json{{"workflow_id", workflow.workflowId}, {"name", workflow.name}});

    // Validate DAG (edges authoritative, registry-aware for engines/ops).
    try {
        validateDag(workflow, registry_.get());
    } catch (const std::invalid_argument& exc) {
        workflow.status = WorkflowStatus::Failed;
        workflow.completedAt = core::utcNowIso();
        workflow.updatedAt = workflow.completedAt;
        out.success = false;
        out.workflow = workflow;
        out.error = core::makeError(core::ErrorCode::RequestValidationError, exc.what(),
                                    "workflow", {{"workflow_id", workflow.workflowId}})
                        .toJson();
        log.info("workflow", "workflow validation failed",
                 core::Json{{"workflow_id", workflow.workflowId}, {"error", exc.what()}});
        try {
            workflows_->saveWorkflow(workflow);
        } catch (...) {
        }
        return out;
    } catch (const core::TrinityError& exc) {
        workflow.status = WorkflowStatus::Failed;
        workflow.completedAt = core::utcNowIso();
        workflow.updatedAt = workflow.completedAt;
        out.success = false;
        out.workflow = workflow;
        out.error = exc.toJson();
        try {
            workflows_->saveWorkflow(workflow);
        } catch (...) {
        }
        return out;
    }

    std::vector<WorkflowNode> order;
    try {
        Workflow copy = workflow;
        order = ordered(copy);
        // Sync derived order back (validateDag already derived deps).
        workflow.nodes.clear();
        // Rebuild node list in topo order but keep full structs: map id->node.
        std::map<std::string, WorkflowNode> byId;
        // ordered() returns copies; workflow.nodes holds authoritative structs.
        // Reconstruct: find each ordered id in workflow.nodes.
        for (const auto& on : order) {
            byId[on.effectiveId()] = on;
        }
        // Preserve original vector but sorted: use order sequence.
        std::vector<WorkflowNode> sorted;
        for (const auto& on : order) {
            // Find authoritative node in workflow (with inputFrom etc).
            for (const auto& n : workflow.nodes) {
                // workflow.nodes not yet sorted; but `copy` derived deps.
                (void)n;
            }
            sorted.push_back(byId[on.effectiveId()]);
        }
        // Merge authoritative fields (inputFrom/allowFailure/etc) from original
        // workflow def into sorted copies (ordered() preserves them already
        // since it copies whole structs, so this is a no-op safety).
        workflow.nodes = sorted;
        order = sorted;
    } catch (const std::invalid_argument& exc) {
        workflow.status = WorkflowStatus::Failed;
        workflow.completedAt = core::utcNowIso();
        workflow.updatedAt = workflow.completedAt;
        out.success = false;
        out.workflow = workflow;
        out.error = core::makeError(core::ErrorCode::RequestValidationError, exc.what(),
                                    "workflow", {{"workflow_id", workflow.workflowId}})
                        .toJson();
        try {
            workflows_->saveWorkflow(workflow);
        } catch (...) {
        }
        return out;
    }

    // Persist RUNNING workflow upfront so UI polling sees it.
    try {
        workflows_->saveWorkflow(workflow);
    } catch (const std::exception& exc) {
        log.warning("workflow", "workflow persist failed",
                    core::Json{{"workflow_id", workflow.workflowId}, {"error", exc.what()}});
    }

    std::map<std::string, jobs::Job> nodeJobs;
    std::map<std::string, core::Json> results;
    std::map<std::string, bool> succeeded;
    bool cancelled = false;

    for (auto& node : workflow.nodes) {
        const std::string nid = node.effectiveId();
        out.currentNode = nid;

        if (token && token->isCancelled()) {
            node.status = NodeStatus::Skipped;
            workflow.status = WorkflowStatus::Cancelled;
            cancelled = true;
            log.info("workflow", "workflow cancelled",
                     core::Json{{"workflow_id", workflow.workflowId}, {"node", nid}});
            break;
        }
        if (jobs_->isCancellationRequested(workflow.workflowId)) {
            node.status = NodeStatus::Skipped;
            workflow.status = WorkflowStatus::Cancelled;
            cancelled = true;
            break;
        }

        // Dependency gating: failed dep blocks unless allowFailure.
        bool blocked = false;
        std::string blockingDep;
        for (const auto& dep : node.dependencies) {
            auto it = succeeded.find(dep);
            if (it == succeeded.end() || !it->second) {
                if (!node.allowFailure) {
                    blocked = true;
                    blockingDep = dep;
                    break;
                }
            }
        }
        if (blocked) {
            node.status = NodeStatus::Skipped;
            succeeded[nid] = false;
            out.failedNodes.push_back(nid);
            log.info("workflow", "workflow node skipped (dependency failed)",
                     core::Json{{"workflow_id", workflow.workflowId},
                                {"node", nid},
                                {"blocked_by", blockingDep}});
            try {
                workflows_->saveNode(workflow.workflowId, node);
            } catch (...) {
            }
            continue;
        }

        node.status = NodeStatus::Running;
        log.info("workflow", "workflow node started",
                 core::Json{{"workflow_id", workflow.workflowId}, {"node", nid}});
        try {
            workflows_->saveNode(workflow.workflowId, node);
        } catch (...) {
        }

        // Resolve structured inputs from prior results (§9).
        core::Json effectiveParams;
        try {
            effectiveParams = resolveNodeInput(node, results);
        } catch (const std::exception& exc) {
            node.status = NodeStatus::Failed;
            node.error =
                core::makeError(core::ErrorCode::RequestValidationError, exc.what(), "workflow")
                    .toJson();
            succeeded[nid] = false;
            out.failedNodes.push_back(nid);
            log.info("workflow", "workflow node failed (input resolution)",
                     core::Json{{"workflow_id", workflow.workflowId}, {"node", nid}});
            try {
                workflows_->saveNode(workflow.workflowId, node);
            } catch (...) {
            }
            continue;
        }

        const std::string engine = node.effectiveEngine();
        jobs::JobCreateOptions opts;
        opts.workflowId = workflow.workflowId;
        opts.metadata = core::Json{{"workflow_id", workflow.workflowId}, {"node_id", nid}};
        opts.timeoutMs = node.timeoutMs;
        opts.retry = jobs::RetryPolicy{node.retryCount, node.maxRetries, node.retryDelayMs};
        jobs::Job job = jobs_->createJob(engine, node.operation, effectiveParams, opts);
        nodeJobs.emplace(nid, job);
        core::Json envelope = jobs_->executeJob(job.jobId, token);
        const bool ok = envelope.value("success", false);
        if (ok) {
            node.status = NodeStatus::Completed;
            node.result = envelope.value("result", core::Json::object());
            node.error = nullptr;
            succeeded[nid] = true;
            results[nid] = node.result;
            log.info("workflow", "workflow node completed",
                     core::Json{{"workflow_id", workflow.workflowId}, {"node", nid}});
        } else {
            node.status = NodeStatus::Failed;
            core::Json errs = envelope.value("errors", core::Json::array());
            node.error = errs.is_array() && !errs.empty()
                             ? errs.front()
                             : core::makeError(core::ErrorCode::EngineExecutionError,
                                               "Node execution failed", "workflow")
                                   .toJson();
            node.result = envelope.value("result", core::Json::object());
            succeeded[nid] = false;
            out.failedNodes.push_back(nid);
            // Structured truthful failure: no fake result stored.
            log.info("workflow", "workflow node failed",
                     core::Json{{"workflow_id", workflow.workflowId}, {"node", nid}});
        }
        try {
            workflows_->saveNode(workflow.workflowId, node);
        } catch (...) {
        }
    }

    if (cancelled) {
        workflow.status = WorkflowStatus::Cancelled;
    } else if (out.failedNodes.empty()) {
        workflow.status = WorkflowStatus::Completed;
    } else {
        workflow.status = WorkflowStatus::Failed;
    }
    workflow.completedAt = core::utcNowIso();
    workflow.updatedAt = workflow.completedAt;
    try {
        workflows_->saveWorkflow(workflow);
    } catch (...) {
    }

    out.success = (workflow.status == WorkflowStatus::Completed);
    out.workflow = workflow;
    out.nodeResults = results;
    log.info("workflow", out.success ? "workflow completed" : "workflow finished",
             core::Json{{"workflow_id", workflow.workflowId},
                        {"status", toString(workflow.status)},
                        {"failed", static_cast<int>(out.failedNodes.size())}});
    return out;
}

}  // namespace trinity::workflows
