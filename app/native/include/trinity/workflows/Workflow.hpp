#pragma once

// Workflow DAG primitives. Mirrors src/workflows/dag.py: batch
// topological ordering with duplicate-ID and missing-dependency/cycle
// detection. No priorities or parallelism metadata in V1.

#include <string>
#include <vector>

#include "../core/Json.hpp"

namespace trinity::workflows {

struct WorkflowNode {
    std::string id;
    std::string engine;
    std::string operation;
    core::Json parameters = core::Json::object();
    std::vector<std::string> dependencies;
    std::string status = "queued";
};

// Returns nodes in dependency order, preserving input order within
// each ready batch. Throws RequestValidationError-equivalent
// (std::invalid_argument) on duplicate IDs or cycles.
std::vector<WorkflowNode> ordered(const std::vector<WorkflowNode>& nodes);

}  // namespace trinity::workflows
