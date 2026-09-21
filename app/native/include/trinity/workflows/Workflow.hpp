#pragma once

// Workflow DAG primitives. Mirrors src/workflows/dag.py: batch
// topological ordering with duplicate-ID and missing-dependency/cycle
// detection. Workflows, nodes and edges are strongly typed and JSON
// serializable for persistence, logs and future LLM communication.

#include <map>
#include <string>
#include <vector>

#include "../core/Json.hpp"

namespace trinity::engines {
class EngineRegistry;
}

namespace trinity::workflows {

enum class WorkflowStatus {
    Draft,
    Queued,
    Running,
    Completed,
    Failed,
    Cancelled,
};

enum class NodeStatus {
    Queued,
    Running,
    Completed,
    Failed,
    Skipped,
};

enum class EdgeType {
    DependsOn,
    DataFlow,
};

enum class NodeType {
    Engine,
    Validation,
    Branch,
};

std::string toString(WorkflowStatus status);
WorkflowStatus workflowStatusFromString(const std::string& status);
std::string toString(NodeStatus status);
NodeStatus nodeStatusFromString(const std::string& status);
std::string toString(EdgeType type);
EdgeType edgeTypeFromString(const std::string& type);
std::string toString(NodeType type);
NodeType nodeTypeFromString(const std::string& type);

struct WorkflowNode {
    std::string id;
    std::string engine;
    std::string operation;
    core::Json parameters = core::Json::object();
    // Derived from incoming edges (edges authoritative). Stored for
    // compat but recomputed by deriveDependencies() on validate/load.
    std::vector<std::string> dependencies;
    NodeStatus status = NodeStatus::Queued;
    // Extended core fields (defaulted so legacy aggregate init keeps working).
    std::string nodeId;  // canonical UUID alias; falls back to id when empty
    std::string name;
    NodeType type = NodeType::Engine;
    // --- §1/§4/§9/§10 extensions (appended to preserve aggregate init) ---
    std::string tool;  // alias for engine (tool/engine naming); kept in sync
    // `input` is canonical; `parameters` is the legacy alias.
    core::Json input = core::Json::object();
    core::Json result = nullptr;
    core::Json error = nullptr;
    // Retry metadata (§10): scaffold only, no auto-retry policy yet.
    int retryCount = 0;
    int maxRetries = 0;
    int retryDelayMs = 0;
    // Dependency policy: when true, this node runs even if a dependency failed.
    bool allowFailure = false;
    // Explicit structured result propagation (§9): maps this node's input
    // keys to "{{sourceNode.json.pointer}}" templates.
    core::Json inputFrom = core::Json::object();
    int timeoutMs = 0;

    std::string effectiveId() const { return nodeId.empty() ? id : nodeId; }
    std::string effectiveEngine() const { return engine.empty() ? tool : engine; }
    core::Json effectiveInput() const;

    core::Json toJson() const;
    static WorkflowNode fromJson(const core::Json& json);
};

struct WorkflowEdge {
    std::string edgeId;
    std::string fromNode;
    std::string toNode;
    EdgeType type = EdgeType::DependsOn;
    core::Json condition = nullptr;

    core::Json toJson() const;
    static WorkflowEdge fromJson(const core::Json& json);
};

struct Workflow {
    std::string workflowId;
    std::string name;
    std::string description;
    WorkflowStatus status = WorkflowStatus::Draft;
    std::vector<WorkflowNode> nodes;
    std::vector<WorkflowEdge> edges;
    std::string createdAt;
    std::string startedAt;
    std::string completedAt;
    std::string updatedAt;
    core::Json metadata = core::Json::object();

    bool succeeded() const noexcept { return status == WorkflowStatus::Completed; }

    core::Json toJson() const;
    static Workflow fromJson(const core::Json& json);
};

// Returns nodes in dependency order, preserving input order within
// each ready batch. Throws RequestValidationError on duplicate IDs
// or missing dependency/cycle (std::invalid_argument kept as the
// carrier for backward compatibility with existing tests).
std::vector<WorkflowNode> ordered(const std::vector<WorkflowNode>& nodes);
// Edge-authoritative ordering: dependencies derived from edges.
std::vector<WorkflowNode> ordered(const Workflow& workflow);
// Derive node.dependencies from incoming edges (single source of truth).
void deriveDependencies(Workflow& workflow);
// Full DAG validation (§5): cycles, missing nodes, invalid deps,
// duplicate IDs, unknown engines, unsupported ops. Throws
// std::invalid_argument with a descriptive message.
void validateDag(Workflow& workflow);
// Registry-aware overload (checks engines/operations when provided).
void validateDag(Workflow& workflow, const engines::EngineRegistry* registry);
// Resolve "{{nodeId.pointer}}" templates in node.inputFrom against
// completed node results (§9). Throws on missing source/invalid pointer.
core::Json resolveNodeInput(const WorkflowNode& node,
                            const std::map<std::string, core::Json>& results);

}  // namespace trinity::workflows
