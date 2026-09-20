#pragma once

// Workflow DAG primitives. Mirrors src/workflows/dag.py: batch
// topological ordering with duplicate-ID and missing-dependency/cycle
// detection. Workflows, nodes and edges are strongly typed and JSON
// serializable for persistence, logs and future LLM communication.

#include <string>
#include <vector>

#include "../core/Json.hpp"

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
    std::vector<std::string> dependencies;
    NodeStatus status = NodeStatus::Queued;
    // Extended core fields (defaulted so legacy aggregate init keeps working).
    std::string nodeId;  // canonical UUID alias; falls back to id when empty
    std::string name;
    NodeType type = NodeType::Engine;

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
    WorkflowStatus status = WorkflowStatus::Draft;
    std::vector<WorkflowNode> nodes;
    std::vector<WorkflowEdge> edges;
    std::string createdAt;
    std::string updatedAt;

    bool succeeded() const noexcept { return status == WorkflowStatus::Completed; }

    core::Json toJson() const;
    static Workflow fromJson(const core::Json& json);
};

// Returns nodes in dependency order, preserving input order within
// each ready batch. Throws RequestValidationError on duplicate IDs
// or missing dependency/cycle (std::invalid_argument kept as the
// carrier for backward compatibility with existing tests).
std::vector<WorkflowNode> ordered(const std::vector<WorkflowNode>& nodes);

}  // namespace trinity::workflows
