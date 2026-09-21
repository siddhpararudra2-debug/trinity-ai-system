#include "trinity/workflows/Workflow.hpp"

#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "trinity/engines/EngineRegistry.hpp"

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

core::Json WorkflowNode::effectiveInput() const {
    if (!input.is_null() && input.is_object() && !input.empty()) {
        return input;
    }
    if (!parameters.is_null() && parameters.is_object()) {
        return parameters;
    }
    return core::Json::object();
}

core::Json WorkflowNode::toJson() const {
    core::Json deps = core::Json::array();
    for (const auto& d : dependencies) {
        deps.push_back(d);
    }
    return core::Json{{"id", id},
                      {"node_id", nodeId.empty() ? id : nodeId},
                      {"name", name},
                      {"type", toString(type)},
                      {"engine", effectiveEngine()},
                      {"tool", tool.empty() ? engine : tool},
                      {"operation", operation},
                      {"input", effectiveInput()},
                      {"parameters", effectiveInput()},
                      {"dependencies", deps},
                      {"status", toString(status)},
                      {"result", result},
                      {"error", error},
                      {"retry_count", retryCount},
                      {"max_retries", maxRetries},
                      {"retry_delay_ms", retryDelayMs},
                      {"allow_failure", allowFailure},
                      {"input_from", inputFrom.is_null() ? core::Json::object() : inputFrom},
                      {"timeout_ms", timeoutMs}};
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
    node.engine = json.value("engine", json.value("tool", ""));
    node.tool = json.value("tool", node.engine);
    node.operation = json.value("operation", "");
    // input canonical, parameters legacy.
    if (json.contains("input") && !json["input"].is_null()) {
        node.input = json["input"];
    } else {
        node.input = core::Json::object();
    }
    if (json.contains("parameters") && !json["parameters"].is_null()) {
        node.parameters = json["parameters"];
        if (node.input.is_null() || (node.input.is_object() && node.input.empty())) {
            node.input = node.parameters;
        }
    } else {
        node.parameters = node.input;
    }
    if (node.parameters.is_null()) {
        node.parameters = core::Json::object();
    }
    if (node.input.is_null()) {
        node.input = core::Json::object();
    }
    if (json.contains("dependencies") && json["dependencies"].is_array()) {
        for (const auto& d : json["dependencies"]) {
            if (d.is_string()) {
                node.dependencies.push_back(d.get<std::string>());
            }
        }
    }
    node.status = nodeStatusFromString(json.value("status", "queued"));
    node.result = json.value("result", core::Json(nullptr));
    node.error = json.value("error", core::Json(nullptr));
    node.retryCount = json.value("retry_count", 0);
    node.maxRetries = json.value("max_retries", 0);
    node.retryDelayMs = json.value("retry_delay_ms", 0);
    node.allowFailure = json.value("allow_failure", json.value("allowFailure", false));
    node.inputFrom = json.value("input_from", json.value("inputFrom", core::Json::object()));
    if (node.inputFrom.is_null()) {
        node.inputFrom = core::Json::object();
    }
    node.timeoutMs = json.value("timeout_ms", 0);
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
    edge.fromNode = json.value("from_node", json.value("from", ""));
    edge.toNode = json.value("to_node", json.value("to", ""));
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
    core::Json meta = metadata;
    if (meta.is_null()) {
        meta = core::Json::object();
    }
    return core::Json{{"workflow_id", workflowId},
                      {"name", name},
                      {"description", description},
                      {"status", toString(status)},
                      {"nodes", nodesJson},
                      {"edges", edgesJson},
                      {"created_at", createdAt},
                      {"started_at", startedAt},
                      {"completed_at", completedAt},
                      {"updated_at", updatedAt},
                      {"metadata", meta}};
}

Workflow Workflow::fromJson(const core::Json& json) {
    Workflow workflow;
    workflow.workflowId = json.value("workflow_id", "");
    workflow.name = json.value("name", "");
    workflow.description = json.value("description", "");
    workflow.status = workflowStatusFromString(json.value("status", "draft"));
    workflow.createdAt = json.value("created_at", "");
    workflow.startedAt = json.value("started_at", "");
    workflow.completedAt = json.value("completed_at", "");
    workflow.updatedAt = json.value("updated_at", "");
    workflow.metadata = json.value("metadata", core::Json::object());
    if (workflow.metadata.is_null()) {
        workflow.metadata = core::Json::object();
    }
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
    // Edges authoritative: recompute dependencies on load.
    if (!workflow.edges.empty()) {
        deriveDependencies(workflow);
    }
    return workflow;
}

void deriveDependencies(Workflow& workflow) {
    std::unordered_map<std::string, std::vector<std::string>> incoming;
    for (const auto& node : workflow.nodes) {
        incoming[node.effectiveId()] = {};
    }
    for (const auto& edge : workflow.edges) {
        auto it = incoming.find(edge.toNode);
        if (it != incoming.end()) {
            it->second.push_back(edge.fromNode);
        }
    }
    for (auto& node : workflow.nodes) {
        const std::string key = node.effectiveId();
        auto it = incoming.find(key);
        if (it != incoming.end() && !workflow.edges.empty()) {
            node.dependencies = it->second;
        }
        // When no edges exist (legacy workflows), keep manual dependencies.
    }
}

namespace {

std::vector<WorkflowNode> topoSort(const std::vector<WorkflowNode>& nodes) {
    std::unordered_set<std::string> ids;
    for (const WorkflowNode& node : nodes) {
        const std::string key = node.effectiveId().empty() ? node.id : node.effectiveId();
        if (key.empty()) {
            throw std::invalid_argument("Workflow node has empty ID");
        }
        if (!ids.insert(key).second) {
            throw std::invalid_argument("Workflow has duplicate node IDs");
        }
    }

    std::unordered_map<std::string, WorkflowNode> byId;
    for (const WorkflowNode& node : nodes) {
        const std::string key = node.effectiveId().empty() ? node.id : node.effectiveId();
        byId.emplace(key, node);
    }

    std::unordered_map<std::string, WorkflowNode> remaining = byId;
    std::vector<WorkflowNode> result;
    std::unordered_set<std::string> done;
    result.reserve(nodes.size());

    while (!remaining.empty()) {
        std::vector<WorkflowNode> ready;
        for (const WorkflowNode& node : nodes) {
            const std::string key = node.effectiveId().empty() ? node.id : node.effectiveId();
            if (remaining.find(key) == remaining.end()) {
                continue;
            }
            bool satisfied = true;
            // Use the authoritative dependencies on the (possibly derived) node copy.
            // Look up current remaining entry to get derived deps.
            const WorkflowNode& current = remaining.at(key);
            for (const std::string& dep : current.dependencies) {
                if (done.find(dep) == done.end()) {
                    satisfied = false;
                    break;
                }
                if (byId.find(dep) == byId.end()) {
                    satisfied = false;
                    break;
                }
            }
            // Also reject deps pointing at unknown nodes even if "done" somehow contains them.
            for (const std::string& dep : current.dependencies) {
                if (byId.find(dep) == byId.end()) {
                    throw std::invalid_argument(
                        "Workflow has missing node for dependency '" + dep + "'");
                }
            }
            if (satisfied) {
                ready.push_back(current);
            }
        }
        if (ready.empty()) {
            throw std::invalid_argument("Workflow has a missing dependency or cycle");
        }
        for (const WorkflowNode& node : ready) {
            const std::string key = node.effectiveId().empty() ? node.id : node.effectiveId();
            result.push_back(node);
            remaining.erase(key);
            done.insert(key);
        }
    }
    return result;
}

}  // namespace

std::vector<WorkflowNode> ordered(const std::vector<WorkflowNode>& nodes) {
    return topoSort(nodes);
}

std::vector<WorkflowNode> ordered(const Workflow& workflow) {
    Workflow copy = workflow;
    if (!copy.edges.empty()) {
        deriveDependencies(copy);
    }
    return topoSort(copy.nodes);
}

void validateDag(Workflow& workflow) {
    validateDag(workflow, nullptr);
}

void validateDag(Workflow& workflow, const engines::EngineRegistry* registry) {
    if (workflow.nodes.empty()) {
        throw std::invalid_argument("Workflow has no nodes");
    }
    // Duplicate IDs.
    {
        std::unordered_set<std::string> ids;
        for (const auto& node : workflow.nodes) {
            const std::string key = node.effectiveId().empty() ? node.id : node.effectiveId();
            if (key.empty()) {
                throw std::invalid_argument("Workflow node has empty ID");
            }
            if (!ids.insert(key).second) {
                throw std::invalid_argument("Workflow has duplicate node IDs");
            }
        }
    }
    std::unordered_set<std::string> idSet;
    for (const auto& node : workflow.nodes) {
        idSet.insert(node.effectiveId().empty() ? node.id : node.effectiveId());
    }
    // Edge validation (authoritative).
    for (const auto& edge : workflow.edges) {
        if (edge.fromNode.empty() || edge.toNode.empty()) {
            throw std::invalid_argument("Workflow edge has empty endpoint");
        }
        if (idSet.find(edge.fromNode) == idSet.end()) {
            throw std::invalid_argument("Workflow edge references missing node '" +
                                        edge.fromNode + "'");
        }
        if (idSet.find(edge.toNode) == idSet.end()) {
            throw std::invalid_argument("Workflow edge references missing node '" +
                                        edge.toNode + "'");
        }
        if (edge.fromNode == edge.toNode) {
            throw std::invalid_argument("Workflow edge is a self-loop on '" + edge.fromNode + "'");
        }
    }
    // Derive authoritative dependencies, then check manual deps consistency.
    // If both edges and manual dependencies exist and disagree, edges win
    // but we still surface the mismatch via derived state (no throw — edges
    // are truth). Missing deps without edges are validated below.
    if (!workflow.edges.empty()) {
        deriveDependencies(workflow);
    } else {
        // Legacy path: validate manual dependencies point at known nodes.
        for (const auto& node : workflow.nodes) {
            for (const auto& dep : node.dependencies) {
                if (idSet.find(dep) == idSet.end()) {
                    throw std::invalid_argument(
                        "Workflow has missing node for dependency '" + dep + "'");
                }
                if (dep == (node.effectiveId().empty() ? node.id : node.effectiveId())) {
                    throw std::invalid_argument("Workflow node depends on itself '" + dep + "'");
                }
            }
        }
    }
    // Engine / operation validation.
    if (registry != nullptr) {
        for (const auto& node : workflow.nodes) {
            const std::string eng = node.effectiveEngine();
            if (eng.empty()) {
                throw std::invalid_argument("Workflow node '" + node.effectiveId() +
                                            "' has empty engine");
            }
            if (!registry->has(eng)) {
                throw std::invalid_argument("Workflow node '" + node.effectiveId() +
                                            "' uses unknown engine '" + eng + "'");
            }
            if (node.operation.empty()) {
                throw std::invalid_argument("Workflow node '" + node.effectiveId() +
                                            "' has empty operation");
            }
            const auto caps = registry->listCapabilities(eng);
            bool ok = false;
            for (const auto& c : caps) {
                if (c == node.operation) {
                    ok = true;
                    break;
                }
            }
            if (!ok) {
                throw std::invalid_argument("Workflow node '" + node.effectiveId() +
                                            "' uses unsupported operation '" + node.operation +
                                            "' for engine '" + eng + "'");
            }
        }
    }
    // Topological sort detects cycles / missing deps.
    Workflow copy = workflow;
    (void)topoSort(copy.nodes);
}

namespace {

core::Json lookupPointer(const std::string& srcNode, const std::string& pointer,
                         const std::map<std::string, core::Json>& results) {
    auto found = results.find(srcNode);
    if (found == results.end()) {
        throw std::invalid_argument("inputFrom references unknown node result '" + srcNode + "'");
    }
    core::Json value = found->second;
    if (pointer.empty()) {
        return value;
    }
    size_t start = 0;
    core::Json current = value;
    while (true) {
        const size_t next = pointer.find('.', start);
        const std::string token =
            next == std::string::npos ? pointer.substr(start) : pointer.substr(start, next - start);
        if (current.is_object()) {
            if (!current.contains(token)) {
                throw std::invalid_argument("inputFrom pointer '" + pointer + "' missing key '" +
                                            token + "'");
            }
            current = current[token];
        } else if (current.is_array()) {
            size_t idx = 0;
            try {
                idx = static_cast<size_t>(std::stoul(token));
            } catch (...) {
                throw std::invalid_argument("inputFrom pointer '" + pointer +
                                            "' invalid array index '" + token + "'");
            }
            if (idx >= current.size()) {
                throw std::invalid_argument("inputFrom pointer out of range '" + pointer + "'");
            }
            current = current[idx];
        } else {
            throw std::invalid_argument("inputFrom cannot traverse into scalar at '" + token + "'");
        }
        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }
    return current;
}

std::string jsonToString(const core::Json& v) {
    if (v.is_string()) {
        return v.get<std::string>();
    }
    if (v.is_boolean()) {
        return v.get<bool>() ? "true" : "false";
    }
    if (v.is_number_integer()) {
        return std::to_string(v.get<long long>());
    }
    if (v.is_number()) {
        core::Json tmp = v;
        std::string s = tmp.dump();
        return s;
    }
    if (v.is_null()) {
        return "null";
    }
    return v.dump();
}

// Resolve a template value recursively. Exact "{{a.b}}" returns raw typed
// JSON (structured propagation); embedded "x={{a.b}}" interpolates as string;
// objects/arrays recurse.
core::Json resolveTemplate(const core::Json& tmpl,
                           const std::map<std::string, core::Json>& results) {
    if (tmpl.is_string()) {
        const std::string s = tmpl.get<std::string>();
        // Exact single template?
        if (s.size() >= 5 && s.substr(0, 2) == "{{" && s.substr(s.size() - 2) == "}}" &&
            s.find("{{", 2) == std::string::npos) {
            const std::string inner = s.substr(2, s.size() - 4);
            const size_t dot = inner.find('.');
            const std::string src = dot == std::string::npos ? inner : inner.substr(0, dot);
            const std::string ptr = dot == std::string::npos ? "" : inner.substr(dot + 1);
            if (src.empty()) {
                throw std::invalid_argument("Invalid empty inputFrom reference in '" + s + "'");
            }
            return lookupPointer(src, ptr, results);
        }
        // Embedded interpolation (e.g. "{{A.value}} * 2").
        std::string out;
        size_t pos = 0;
        bool any = false;
        while (true) {
            const size_t open = s.find("{{", pos);
            if (open == std::string::npos) {
                out += s.substr(pos);
                break;
            }
            const size_t close = s.find("}}", open + 2);
            if (close == std::string::npos) {
                throw std::invalid_argument("Unclosed '{{' in inputFrom template '" + s + "'");
            }
            out += s.substr(pos, open - pos);
            const std::string inner = s.substr(open + 2, close - open - 2);
            const size_t dot = inner.find('.');
            const std::string src = dot == std::string::npos ? inner : inner.substr(0, dot);
            const std::string ptr = dot == std::string::npos ? "" : inner.substr(dot + 1);
            out += jsonToString(lookupPointer(src, ptr, results));
            any = true;
            pos = close + 2;
        }
        if (!any) {
            return tmpl;
        }
        return core::Json(out);
    }
    if (tmpl.is_object()) {
        core::Json out = core::Json::object();
        for (auto it = tmpl.begin(); it != tmpl.end(); ++it) {
            out[it.key()] = resolveTemplate(it.value(), results);
        }
        return out;
    }
    if (tmpl.is_array()) {
        core::Json out = core::Json::array();
        for (const auto& item : tmpl) {
            out.push_back(resolveTemplate(item, results));
        }
        return out;
    }
    return tmpl;
}

}  // namespace

core::Json resolveNodeInput(const WorkflowNode& node,
                            const std::map<std::string, core::Json>& results) {
    core::Json base = node.effectiveInput();
    if (base.is_null()) {
        base = core::Json::object();
    }
    if (!base.is_object()) {
        return base;
    }
    if (node.inputFrom.is_null() || !node.inputFrom.is_object() || node.inputFrom.empty()) {
        return base;
    }
    core::Json merged = base;
    for (auto it = node.inputFrom.begin(); it != node.inputFrom.end(); ++it) {
        merged[it.key()] = resolveTemplate(it.value(), results);
    }
    return merged;
}

}  // namespace trinity::workflows
