#include "trinity/workflows/Workflow.hpp"

#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace trinity::workflows {

std::string toString(WorkflowStatus status) {
    switch (status) {
        case WorkflowStatus::Draft:
            return "draft";
        case WorkflowStatus::Queued:
            return "queued";
        case WorkflowStatus::Running:
            return "running";
        case WorkflowStatus::Completed:
            return "completed";
        case WorkflowStatus::Failed:
            return "failed";
        case WorkflowStatus::Cancelled:
            return "cancelled";
    }
    return "draft";
}

WorkflowStatus workflowStatusFromString(const std::string& status) {
    if (status == "queued") return WorkflowStatus::Queued;
    if (status == "running") return WorkflowStatus::Running;
    if (status == "completed") return WorkflowStatus::Completed;
    if (status == "failed") return WorkflowStatus::Failed;
    if (status == "cancelled") return WorkflowStatus::Cancelled;
    return WorkflowStatus::Draft;
}

std::string toString(NodeStatus status) {
    switch (status) {
        case NodeStatus::Queued:
            return "queued";
        case NodeStatus::Running:
            return "running";
        case NodeStatus::Completed:
            return "completed";
        case NodeStatus::Failed:
            return "failed";
        case NodeStatus::Skipped:
            return "skipped";
    }
    return "queued";
}

NodeStatus nodeStatusFromString(const std::string& status) {
    if (status == "running") return NodeStatus::Running;
    if (status == "completed") return NodeStatus::Completed;
    if (status == "failed") return NodeStatus::Failed;
    if (status == "skipped") return NodeStatus::Skipped;
    return NodeStatus::Queued;
}

std::string toString(EdgeType type) {
    switch (type) {
        case EdgeType::DependsOn:
            return "depends_on";
        case EdgeType::DataFlow:
            return "data_flow";
    }
    return "depends_on";
}

EdgeType edgeTypeFromString(const std::string& type) {
    if (type == "data_flow") return EdgeType::DataFlow;
    return EdgeType::DependsOn;
}

std::string toString(NodeType type) {
    switch (type) {
        case NodeType::Engine:
            return "engine";
        case NodeType::Validation:
            return "validation";
        case NodeType::Branch:
            return "branch";
    }
    return "engine";
}

NodeType nodeTypeFromString(const std::string& type) {
    if (type == "validation") return NodeType::Validation;
    if (type == "branch") return NodeType::Branch;
    return NodeType::Engine;
}

core::Json WorkflowNode::toJson() const {
    return core::Json{{"id", id},
                      {"node_id", nodeId.empty() ? id : nodeId},
                      {"name", name},
                      {"type", toString(type)},
                      {"engine", engine},
                      {"operation", operation},
                      {"parameters", parameters},
                      {"dependencies", dependencies},
                      {"status", toString(status)}};
}

WorkflowNode WorkflowNode::fromJson(const core::Json& json) {
    WorkflowNode node;
    node.id = json.value("id", json.value("node_id", ""));
    node.nodeId = json.value("node_id", node.id);
    if (node.nodeId.empty()) {
        node.nodeId = node.id;
    }
    node.name = json.value("name", "");
    node.type = nodeTypeFromString(json.value("type", "engine"));
    node.engine = json.value("engine", "");
    node.operation = json.value("operation", "");
    node.parameters = json.value("parameters", core::Json::object());
    node.dependencies = json.value("dependencies", std::vector<std::string>{});
    node.status = nodeStatusFromString(json.value("status", "queued"));
    return node;
}

core::Json WorkflowEdge::toJson() const {
    return core::Json{{"edge_id", edgeId},
                      {"from_node", fromNode},
                      {"to_node", toNode},
                      {"type", toString(type)},
                      {"condition", condition}};
}

WorkflowEdge WorkflowEdge::fromJson(const core::Json& json) {
    WorkflowEdge edge;
    edge.edgeId = json.value("edge_id", "");
    edge.fromNode = json.value("from_node", "");
    edge.toNode = json.value("to_node", "");
    edge.type = edgeTypeFromString(json.value("type", "depends_on"));
    edge.condition = json.value("condition", core::Json(nullptr));
    return edge;
}

core::Json Workflow::toJson() const {
    core::Json nodesJson = core::Json::array();
    for (const auto& node : nodes) {
        nodesJson.push_back(node.toJson());
    }
    core::Json edgesJson = core::Json::array();
    for (const auto& edge : edges) {
        edgesJson.push_back(edge.toJson());
    }
    return core::Json{{"workflow_id", workflowId},
                      {"name", name},
                      {"status", toString(status)},
                      {"nodes", nodesJson},
                      {"edges", edgesJson},
                      {"created_at", createdAt},
                      {"updated_at", updatedAt}};
}

Workflow Workflow::fromJson(const core::Json& json) {
    Workflow workflow;
    workflow.workflowId = json.value("workflow_id", "");
    workflow.name = json.value("name", "");
    workflow.status = workflowStatusFromString(json.value("status", "draft"));
    workflow.createdAt = json.value("created_at", "");
    workflow.updatedAt = json.value("updated_at", "");
    if (json.contains("nodes") && json["nodes"].is_array()) {
        for (const auto& item : json["nodes"]) {
            workflow.nodes.push_back(WorkflowNode::fromJson(item));
        }
    }
    if (json.contains("edges") && json["edges"].is_array()) {
        for (const auto& item : json["edges"]) {
            workflow.edges.push_back(WorkflowEdge::fromJson(item));
        }
    }
    return workflow;
}

std::vector<WorkflowNode> ordered(const std::vector<WorkflowNode>& nodes) {
    std::unordered_set<std::string> ids;
    for (const WorkflowNode& node : nodes) {
        if (!ids.insert(node.id).second) {
            throw std::invalid_argument("Workflow has duplicate node IDs");
        }
    }

    std::unordered_map<std::string, WorkflowNode> remaining;
    for (const WorkflowNode& node : nodes) {
        remaining.emplace(node.id, node);
    }

    std::vector<WorkflowNode> result;
    std::unordered_set<std::string> done;
    result.reserve(nodes.size());

    while (!remaining.empty()) {
        std::vector<WorkflowNode> ready;
        for (const WorkflowNode& node : nodes) {
            if (remaining.find(node.id) == remaining.end()) {
                continue;
            }
            bool satisfied = true;
            for (const std::string& dep : node.dependencies) {
                if (done.find(dep) == done.end()) {
                    satisfied = false;
                    break;
                }
            }
            if (satisfied) {
                ready.push_back(node);
            }
        }
        if (ready.empty()) {
            throw std::invalid_argument("Workflow has a missing dependency or cycle");
        }
        for (const WorkflowNode& node : ready) {
            result.push_back(node);
            remaining.erase(node.id);
            done.insert(node.id);
        }
    }
    return result;
}

}  // namespace trinity::workflows
