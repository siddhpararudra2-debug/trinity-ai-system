#include "BuiltinCommands.hpp"

#include "../artifacts/Artifact.hpp"
#include "../engines/Engine.hpp"

namespace trinity::commands {
namespace {

std::string argument_string(const core::Json& arguments, const char* key) {
    const core::Json* value = arguments.find(key);
    return value != nullptr ? value->as_string() : std::string();
}

std::string require_string(const core::Json& arguments, const char* key) {
    const std::string value = argument_string(arguments, key);
    if (value.empty()) {
        throw core::TrinityException(core::Error(
            core::ErrorCode::RequestValidationError,
            std::string("command argument '") + key + "' is required"));
    }
    return value;
}

}  // namespace

core::Result<core::Json> NewProjectCommand::execute(const core::Json& arguments) {
    const std::string name = require_string(arguments, "name");
    const std::string description = argument_string(arguments, "description");
    auto created = projects_->create(name, description);
    if (created.is_error()) return core::Result<core::Json>::fail(created.take_error());
    return core::Result<core::Json>::ok(created.value().to_json());
}

core::Result<core::Json> ListProjectsCommand::execute(const core::Json& arguments) {
    const core::Json* include_archived = arguments.find("include_archived");
    const bool archived = include_archived != nullptr && include_archived->as_bool(false);
    auto projects = projects_->list(archived);
    if (projects.is_error()) return core::Result<core::Json>::fail(projects.take_error());

    core::Json out = core::Json::object();
    core::Json items = core::Json::array();
    for (const auto& project : projects.value()) items.push_back(project.to_json());
    out["count"] = static_cast<double>(projects.value().size());
    out["projects"] = items;
    return core::Result<core::Json>::ok(std::move(out));
}

core::Result<core::Json> RunEngineeringCommand::execute(const core::Json& arguments) {
    const std::string text = require_string(arguments, "text");
    const std::string project_id = argument_string(arguments, "project_id");
    auto outcome = executor_->run_text(text, project_id);
    if (outcome.is_error()) return core::Result<core::Json>::fail(outcome.take_error());

    core::Json out = core::Json::object();
    out["job_id"] = outcome.value().job_id;
    out["submitted"] = outcome.value().submitted;
    out["explanation"] = outcome.value().explanation;
    return core::Result<core::Json>::ok(std::move(out));
}

core::Result<core::Json> ValidateArtifactCommand::execute(const core::Json& arguments) {
    const std::string artifact_id = require_string(arguments, "artifact_id");
    std::string engine = argument_string(arguments, "engine");
    if (engine.empty()) engine = "cad";
    const core::Json* verified_argument = arguments.find("verified");
    const bool verified = verified_argument != nullptr && verified_argument->as_bool(false);

    auto state = validation_->apply_checks(artifact_id, engine, core::Json::object(), true, verified);
    if (state.is_error()) return core::Result<core::Json>::fail(state.take_error());

    core::Json out = core::Json::object();
    out["artifact_id"] = artifact_id;
    out["engine"] = engine;
    out["state"] = artifacts::validation_state_string(state.value());
    return core::Result<core::Json>::ok(std::move(out));
}

core::Result<core::Json> ListEnginesCommand::execute(const core::Json& arguments) {
    (void)arguments;
    core::Json out = core::Json::object();
    core::Json items = core::Json::array();
    for (const engines::EngineDescriptor& descriptor : engines::EngineRegistry::instance().list()) {
        items.push_back(descriptor.to_json());
    }
    out["count"] = static_cast<double>(engines::EngineRegistry::instance().count());
    out["engines"] = items;
    return core::Result<core::Json>::ok(std::move(out));
}

std::vector<std::shared_ptr<ICommand>> builtin_commands(projects::ProjectStore& projects,
                                                        ToolExecutor& executor,
                                                        validation::ValidationEngine& validation) {
    std::vector<std::shared_ptr<ICommand>> commands;
    commands.push_back(std::make_shared<NewProjectCommand>(projects));
    commands.push_back(std::make_shared<ListProjectsCommand>(projects));
    commands.push_back(std::make_shared<RunEngineeringCommand>(executor));
    commands.push_back(std::make_shared<ValidateArtifactCommand>(validation));
    commands.push_back(std::make_shared<ListEnginesCommand>());
    return commands;
}

}  // namespace trinity::commands
