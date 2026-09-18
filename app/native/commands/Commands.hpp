// Trinity — Engineering Command system (brief §Command System).
//
//   CommandParser   text -> Command (deterministic grammar)
//   CommandPlanner  Command -> Plan (interface; ScriptedPlanner ships now,
//                   the future model plugs in here without app changes)
//   ToolExecutor    Plan -> engine operations -> jobs + artifacts
//
// No LLM anywhere in this pipeline. Verbs: CREATE OPEN IMPORT EXPORT
// CALCULATE VALIDATE SIMULATE SEARCH ANALYZE GENERATE COMPARE OPTIMIZE.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../core/Error.hpp"
#include "../core/Json.hpp"
#include "../jobs/JobSystem.hpp"

namespace trinity::commands {

enum class CommandVerb {
    Create, Open, Import, Export, Calculate, Validate,
    Simulate, Search, Analyze, Generate, Compare, Optimize, Unknown
};

const char* command_verb_string(CommandVerb verb);
CommandVerb command_verb_from_string(const std::string& text);

struct Command {
    CommandVerb verb = CommandVerb::Unknown;
    std::string object;                    // "quadcopter_frame", "project", ...
    core::Json parameters = core::Json::object();
    std::string raw_text;
    bool matched = false;
};

// Deterministic parser. Seeded from the V1 Python router:
//   "create a 50 mm quadcopter frame" -> {verb: Create, object: quadcopter_frame,
//                                          parameters: {overall_size: 50}}
// plus the CALCULATE verb ("calculate 20% of 50" / "evaluate <expr>") and
// project lifecycle verbs. Unrecognised text yields matched=false (no guessing).
Command parse_command(const std::string& text);

// ------------------------------------------------------------------ planner

struct PlanStep {
    std::string engine;
    std::string operation;
    core::Json parameters;
    std::vector<std::string> depends_on;   // step ids for DAG ordering
    std::string step_id;
    std::string description;
};

struct Plan {
    bool valid = false;
    std::string explanation;               // why the plan is what it is
    std::vector<PlanStep> steps;
    core::Json to_json() const;
};

class ICommandPlanner {
public:
    virtual ~ICommandPlanner() = default;
    virtual std::string planner_id() const = 0;
    virtual Plan plan(const Command& command) = 0;
};

// Deterministic planner shipping now. Produces real engine plans for the
// supported command set and explicit invalid plans otherwise.
class ScriptedPlanner : public ICommandPlanner {
public:
    std::string planner_id() const override { return "scripted"; }
    Plan plan(const Command& command) override;
};

// ---------------------------------------------------------------- executor

struct ExecutionOutcome {
    std::string job_id;
    bool submitted = false;
    std::string explanation;
};

class ToolExecutor {
public:
    explicit ToolExecutor(jobs::JobSystem& job_system) : job_system_(&job_system) {}

    // Executes a plan by submitting one job per step (dependency order is
    // preserved because jobs are queued in topological order). Returns the
    // final step's job id.
    core::Result<ExecutionOutcome> execute(const Plan& plan, const std::string& project_id);

    // Convenience: parse + plan + execute in one call.
    core::Result<ExecutionOutcome> run_text(const std::string& text,
                                            const std::string& project_id);

private:
    jobs::JobSystem* job_system_;
};

}  // namespace trinity::commands
