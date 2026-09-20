#include "trinity/intelligence/Planner.hpp"

#include "trinity/core/Logger.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/jobs/Job.hpp"

namespace trinity::intelligence {

core::Json PlanStepResult::toJson() const {
    return core::Json{{"tool_call", toolCall.toJson()},
                      {"job_id", jobId},
                      {"executed", executed},
                      {"skipped", skipped},
                      {"success", success},
                      {"response", response}};
}

PlanStepResult PlanStepResult::fromJson(const core::Json& json) {
    PlanStepResult step;
    if (json.contains("tool_call")) {
        step.toolCall = ToolCall::fromJson(json["tool_call"]);
    }
    step.jobId = json.value("job_id", "");
    step.executed = json.value("executed", false);
    step.skipped = json.value("skipped", false);
    step.success = json.value("success", false);
    step.response = json.value("response", core::Json::object());
    return step;
}

core::Json PlanResult::toJson() const {
    core::Json stepsJson = core::Json::array();
    for (const auto& step : steps) {
        stepsJson.push_back(step.toJson());
    }
    return core::Json{
        {"success", success}, {"request_id", requestId}, {"steps", stepsJson}, {"error", error}};
}

PlanResult PlanResult::fromJson(const core::Json& json) {
    PlanResult result;
    result.success = json.value("success", false);
    result.requestId = json.value("request_id", "");
    result.error = json.value("error", core::Json(nullptr));
    if (json.contains("steps") && json["steps"].is_array()) {
        for (const auto& item : json["steps"]) {
            result.steps.push_back(PlanStepResult::fromJson(item));
        }
    }
    return result;
}

Planner::Planner(IModelProvider& model, jobs::JobManager& jobs,
                 engines::EngineRegistry& registry)
    : model_(&model), jobs_(&jobs), registry_(&registry) {}

PlanResult Planner::planAndExecute(const ModelRequest& request) {
    auto& log = core::Logger::instance();
    PlanResult plan;
    plan.requestId = request.requestId;

    const ModelResponse response = model_->generate(request);
    if (!response.success) {
        // Model refusal: execute nothing.
        plan.success = false;
        plan.error = response.error;
        log.info("planner", "plan refused; executed nothing",
                 core::Json{{"request_id", request.requestId}});
        return plan;
    }

    std::vector<ToolCall> calls = response.toolCalls;
    if (calls.empty() && response.hasToolCall) {
        calls.push_back(response.toolCall);
    }
    if (calls.empty()) {
        plan.success = true;
        log.info("planner", "plan complete; no tool calls",
                 core::Json{{"request_id", request.requestId}});
        return plan;
    }

    bool failed = false;
    for (size_t i = 0; i < calls.size(); ++i) {
        PlanStepResult step;
        step.toolCall = calls[i];
        if (failed) {
            step.skipped = true;
            step.response = core::Json{{"skipped", true},
                                       {"reason", "earlier step failed"}};
            plan.steps.push_back(std::move(step));
            continue;
        }
        // Validate against the registry before anything executes.
        std::string problem;
        core::Json details = calls[i].toJson();
        if (calls[i].engine.empty() || calls[i].operation.empty()) {
            problem = "Tool call must name an engine and an operation";
        } else if (!registry_->has(calls[i].engine)) {
            problem = "Unknown engine '" + calls[i].engine + "'";
        } else {
            bool supported = false;
            try {
                for (const auto& cap : registry_->listCapabilities(calls[i].engine)) {
                    if (cap == calls[i].operation) {
                        supported = true;
                        break;
                    }
                }
            } catch (...) {
                supported = false;
            }
            if (!supported) {
                problem = "Operation '" + calls[i].operation + "' is not supported by engine '" +
                          calls[i].engine + "'";
            }
        }
        if (!problem.empty()) {
            step.executed = false;
            step.success = false;
            step.response = core::makeError(core::ErrorCode::RequestValidationError, problem,
                                            "planner", details)
                                .toJson();
            plan.steps.push_back(std::move(step));
            failed = true;
            continue;
        }
        try {
            const core::Json envelope =
                jobs_->runSync(calls[i].engine, calls[i].operation, calls[i].parameters);
            step.executed = true;
            step.success = envelope.value("success", false);
            step.jobId = envelope.value("job_id", "");
            step.response = envelope;
            if (!step.success) {
                failed = true;
            }
        } catch (const core::TrinityError& exc) {
            step.executed = true;
            step.success = false;
            step.response = exc.toJson();
            failed = true;
        } catch (const std::exception& exc) {
            step.executed = true;
            step.success = false;
            step.response =
                core::makeError(core::ErrorCode::EngineExecutionError, exc.what(), "planner",
                                details)
                    .toJson();
            failed = true;
        }
        plan.steps.push_back(std::move(step));
    }

    plan.success = !failed;
    if (failed) {
        plan.error = core::makeError(core::ErrorCode::EngineExecutionError,
                                     "Plan finished with step failures", "planner",
                                     {{"request_id", request.requestId}})
                         .toJson();
    }
    log.info("planner", "plan finished",
             core::Json{{"request_id", request.requestId},
                        {"success", plan.success},
                        {"steps", plan.steps.size()}});
    return plan;
}

}  // namespace trinity::intelligence
