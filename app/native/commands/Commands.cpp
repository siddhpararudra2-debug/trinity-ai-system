#include "Commands.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

#include "../core/Logging.hpp"
#include "../engines/Engine.hpp"

namespace trinity::commands {
namespace {
core::ComponentLog log_("commands");

std::string to_lower(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

}  // namespace

const char* command_verb_string(CommandVerb verb) {
    switch (verb) {
        case CommandVerb::Create: return "CREATE";
        case CommandVerb::Open: return "OPEN";
        case CommandVerb::Import: return "IMPORT";
        case CommandVerb::Export: return "EXPORT";
        case CommandVerb::Calculate: return "CALCULATE";
        case CommandVerb::Validate: return "VALIDATE";
        case CommandVerb::Simulate: return "SIMULATE";
        case CommandVerb::Search: return "SEARCH";
        case CommandVerb::Analyze: return "ANALYZE";
        case CommandVerb::Generate: return "GENERATE";
        case CommandVerb::Compare: return "COMPARE";
        case CommandVerb::Optimize: return "OPTIMIZE";
        case CommandVerb::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

CommandVerb command_verb_from_string(const std::string& text) {
    const std::string lower = to_lower(text);
    if (lower == "create") return CommandVerb::Create;
    if (lower == "open") return CommandVerb::Open;
    if (lower == "import") return CommandVerb::Import;
    if (lower == "export") return CommandVerb::Export;
    if (lower == "calculate") return CommandVerb::Calculate;
    if (lower == "validate") return CommandVerb::Validate;
    if (lower == "simulate") return CommandVerb::Simulate;
    if (lower == "search") return CommandVerb::Search;
    if (lower == "analyze") return CommandVerb::Analyze;
    if (lower == "generate") return CommandVerb::Generate;
    if (lower == "compare") return CommandVerb::Compare;
    if (lower == "optimize") return CommandVerb::Optimize;
    return CommandVerb::Unknown;
}

Command parse_command(const std::string& text) {
    Command command;
    command.raw_text = text;
    const std::string lower = to_lower(text);

    // -- CREATE ... quadcopter frame with size (V1 router port) -------------
    static const std::regex kFrameRegex(
        R"(create|generate)\s+(?:a\s+)?(\d+(?:\.\d+)?)\s*mm\s+(?:quad(?:copter|rotor)|drone)\s+frame",
        std::regex::icase);
    std::smatch match;
    if (std::regex_search(lower, match, kFrameRegex)) {
        command.verb = lower.rfind("generate", 0) == 0 ? CommandVerb::Generate
                                                        : CommandVerb::Create;
        command.object = "quadcopter_frame";
        command.parameters["overall_size"] = std::stod(match[2].str());
        command.matched = true;
        return command;
    }

    // -- CALCULATE / evaluate an expression ---------------------------------
    static const std::regex kCalcRegex(R"(calculate|evaluate|compute)\s+(.+)", std::regex::icase);
    if (std::regex_match(lower, match, kCalcRegex)) {
        command.verb = CommandVerb::Calculate;
        command.object = "expression";
        command.parameters["expression"] = match[2].str();
        command.matched = true;
        return command;
    }

    // -- project lifecycle verbs --------------------------------------------
    static const std::regex kNewProjectRegex(R"(new project\s+(.+)|create project\s+(.+))",
                                             std::regex::icase);
    if (std::regex_search(lower, match, kNewProjectRegex)) {
        command.verb = CommandVerb::Create;
        command.object = "project";
        command.parameters["name"] = match[1].matched ? match[1].str() : match[2].str();
        command.matched = true;
        return command;
    }

    // -- explicit verb-first fallback: "<verb> ..." with no object -----------
    static const std::regex kVerbRegex(
        R"^(create|open|import|export|calculate|validate|simulate|search|analyze|generate|compare|optimize)\b\s*(.*)^",
        std::regex::icase);
    if (std::regex_match(lower, match, kVerbRegex)) {
        command.verb = command_verb_from_string(match[1].str());
        command.parameters["text"] = match[2].str();
        command.matched = true;
        return command;
    }

    return command;  // matched=false: no deterministic interpretation
}

core::Json Plan::to_json() const {
    core::Json out = core::Json::object();
    out["valid"] = valid;
    out["explanation"] = explanation;
    // Serialises the *member* step list (the previous version iterated a local
    // shadowing Json array, so every plan serialised as zero steps).
    core::Json steps_json = core::Json::array();
    for (const PlanStep& step : steps) {
        core::Json entry = core::Json::object();
        entry["step_id"] = step.step_id;
        entry["engine"] = step.engine;
        entry["operation"] = step.operation;
        entry["parameters"] = step.parameters;
        core::Json deps = core::Json::array();
        for (const std::string& dep : step.depends_on) deps.push_back(core::Json(dep));
        entry["depends_on"] = deps;
        entry["description"] = step.description;
        steps_json.push_back(entry);
    }
    out["step_count"] = static_cast<double>(steps.size());
    out["steps"] = steps_json;
    return out;
}

Plan ScriptedPlanner::plan(const Command& command) {
    Plan plan;
    if (!command.matched) {
        plan.valid = false;
        plan.explanation = "no deterministic plan for this command";
        return plan;
    }

    switch (command.verb) {
        case CommandVerb::Create:
        case CommandVerb::Generate: {
            if (command.object == "quadcopter_frame") {
                PlanStep step;
                step.step_id = "generate_frame";
                step.engine = "cad";
                step.operation = "generate";
                step.parameters["type"] = "quadcopter_frame";
                step.parameters["parameters"] = command.parameters;
                step.parameters["outputs"] = [&] {
                    core::Json outputs = core::Json::array();
                    outputs.push_back(core::Json("stl"));
                    outputs.push_back(core::Json("json"));
                    return outputs;
                }();
                step.description = "generate parametric quadcopter frame (STL + JSON spec)";
                plan.steps.push_back(std::move(step));
                plan.valid = true;
                plan.explanation = "deterministic CAD generation via the mesh kernel";
            } else if (command.object == "project") {
                // Project creation is handled by the shell layer, not engines.
                plan.valid = false;
                plan.explanation = "project creation is handled by the shell; use 'new project'";
            } else {
                plan.valid = false;
                plan.explanation = "no deterministic CAD plan for object '" + command.object + "'";
            }
            break;
        }
        case CommandVerb::Calculate: {
            PlanStep step;
            step.step_id = "math_evaluate";
            step.engine = "math";
            step.operation = "evaluate";
            step.parameters["expression"] = command.parameters.contains("expression")
                                                ? *command.parameters.find("expression")
                                                : core::Json("");
            step.description = "deterministic math evaluation";
            plan.steps.push_back(std::move(step));
            plan.valid = true;
            plan.explanation = "deterministic math evaluation";
            break;
        }
        case CommandVerb::Validate: {
            PlanStep step;
            step.step_id = "validate";
            step.engine = "cad";
            step.operation = "validate";
            step.parameters = command.parameters;
            step.description = "re-validate a stored CAD spec";
            plan.steps.push_back(std::move(step));
            plan.valid = true;
            plan.explanation = "deterministic re-validation";
            break;
        }
        default: {
            plan.valid = false;
            plan.explanation = std::string("verb ") + command_verb_string(command.verb) +
                               " has no scripted plan yet; it is reserved for the future model layer";
            break;
        }
    }
    return plan;
}

core::Result<ExecutionOutcome> ToolExecutor::execute(const Plan& plan,
                                                     const std::string& project_id) {
    if (!plan.valid || plan.steps.empty()) {
        return core::Result<ExecutionOutcome>::fail(core::Error(
            core::ErrorCode::RequestValidationError,
            plan.explanation.empty() ? "invalid plan" : plan.explanation));
    }

    ExecutionOutcome outcome;
    for (const PlanStep& step : plan.steps) {
        auto engine = engines::EngineRegistry::instance().get(step.engine);
        if (engine.is_error()) return core::Result<ExecutionOutcome>::fail(engine.take_error());
        if (engine.value()->health() == engines::EngineHealth::Scaffolded) {
            return core::Result<ExecutionOutcome>::fail(core::Error(
                core::ErrorCode::CapabilityUnavailableError,
                "engine '" + step.engine + "' is scaffolded; cannot execute '" +
                    step.operation + "'"));
        }

        const core::Json request = step.parameters;
        auto submitted = job_system_->submit(
            step.engine, step.operation, request,
            [engine = engine.value(), operation = step.operation](
                jobs::JobContext& context, const core::Json& job_request) {
                    context.log("executing " + operation);
                    context.report_progress(0.25);
                    engines::ExecutionOutput output = engine->execute(operation, job_request);
                    context.report_progress(0.75);
                    core::Json result = output.result;
                    result["validation_status"] = output.validation_status;
                    result["validation_checks"] = output.validation_checks;
                    // Pending artifacts are persisted by the shell layer after
                    // job completion (single-writer rule via ArtifactStore).
                    core::Json pending = core::Json::array();
                    for (const auto& [path, type] : output.pending_artifacts) {
                        core::Json entry = core::Json::object();
                        entry["path"] = path;
                        entry["type"] = type;
                        pending.push_back(entry);
                    }
                    result["pending_artifacts"] = pending;
                    context.report_progress(0.9);
                    return result;
                },
            project_id);
        if (submitted.is_error()) {
            return core::Result<ExecutionOutcome>::fail(submitted.take_error());
        }
        outcome.job_id = submitted.value();
        outcome.submitted = true;
        log_.info("plan step submitted", [&] {
            core::Json ctx = core::Json::object();
            ctx["job_id"] = outcome.job_id;
            ctx["engine"] = step.engine;
            ctx["operation"] = step.operation;
            return ctx;
        }());
    }
    outcome.explanation = plan.explanation;
    return core::Result<ExecutionOutcome>::ok(std::move(outcome));
}

core::Result<ExecutionOutcome> ToolExecutor::run_text(const std::string& text,
                                                      const std::string& project_id) {
    const Command command = parse_command(text);
    const ScriptedPlanner planner;
    const Plan plan = planner.plan(command);
    return execute(plan, project_id);
}

}  // namespace trinity::commands
