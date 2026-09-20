#pragma once

// Planner: the model → tools → engines loop.
//
// Safety contract (enforced, not advisory):
//   - Models emit data only. The ONLY thing that ever executes is a
//     validated ToolCall struct {engine, operation, parameters}.
//   - Every call is checked against the EngineRegistry (known engine +
//     listed capability) before execution; anything else becomes a
//     structured error record and never runs.
//   - Execution goes through JobManager::runSync, so every step is a
//     tracked job with artifacts and validation rows — deterministic,
//     sequential, no auto-retry, no free-text parsing.
//   - A model refusal (success=false) executes nothing.

#include <string>
#include <vector>

#include "../core/Json.hpp"
#include "IModelProvider.hpp"
#include "ToolCall.hpp"

namespace trinity::engines {
class EngineRegistry;
}
namespace trinity::jobs {
class JobManager;
}

namespace trinity::intelligence {

struct PlanStepResult {
    ToolCall toolCall;
    std::string jobId;  // empty when the step never executed
    bool executed = false;
    bool skipped = false;
    bool success = false;
    core::Json response = core::Json::object();  // job envelope or error record

    core::Json toJson() const;
    static PlanStepResult fromJson(const core::Json& json);
};

struct PlanResult {
    bool success = false;
    std::string requestId;
    std::vector<PlanStepResult> steps;
    core::Json error = nullptr;  // aggregate error when success == false

    core::Json toJson() const;
    static PlanResult fromJson(const core::Json& json);
};

class Planner {
public:
    Planner(IModelProvider& model, jobs::JobManager& jobs, engines::EngineRegistry& registry);

    PlanResult planAndExecute(const ModelRequest& request);

private:
    IModelProvider* model_;
    jobs::JobManager* jobs_;
    engines::EngineRegistry* registry_;
};

}  // namespace trinity::intelligence
