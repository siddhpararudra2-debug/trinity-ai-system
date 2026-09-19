#include "trinity/workflows/Workflow.hpp"

#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace trinity::workflows {

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
