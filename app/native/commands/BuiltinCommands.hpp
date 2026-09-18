// Trinity — built-in deterministic commands.
//
// These are the palette entries that work today without any model: they wire
// the command registry to the project store, the engine pipeline and the
// validation lifecycle. Each returns a structured JSON result so the UI can
// render it without parsing prose.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "CommandRegistry.hpp"
#include "Commands.hpp"
#include "../projects/Project.hpp"
#include "../validation/ValidationEngine.hpp"

namespace trinity::commands {

// Creates a project (name required, description optional).
class NewProjectCommand : public ICommand {
public:
    explicit NewProjectCommand(projects::ProjectStore& projects) : projects_(&projects) {}

    std::string command_id() const override { return "workspace.new_project"; }
    std::string title() const override { return "New Project"; }
    std::string category() const override { return "Workspace"; }
    std::vector<std::string> keywords() const override { return {"project", "create"}; }
    core::Result<core::Json> execute(const core::Json& arguments) override;

private:
    projects::ProjectStore* projects_;
};

// Lists projects (archived ones included when requested).
class ListProjectsCommand : public ICommand {
public:
    explicit ListProjectsCommand(projects::ProjectStore& projects) : projects_(&projects) {}

    std::string command_id() const override { return "workspace.list_projects"; }
    std::string title() const override { return "List Projects"; }
    std::string category() const override { return "Workspace"; }
    std::vector<std::string> keywords() const override { return {"project", "list"}; }
    core::Result<core::Json> execute(const core::Json& arguments) override;

private:
    projects::ProjectStore* projects_;
};

// Runs the deterministic text pipeline: parse -> plan -> submit job(s).
class RunEngineeringCommand : public ICommand {
public:
    explicit RunEngineeringCommand(ToolExecutor& executor) : executor_(&executor) {}

    std::string command_id() const override { return "engine.run_command"; }
    std::string title() const override { return "Run Engineering Command"; }
    std::string category() const override { return "Engines"; }
    std::vector<std::string> keywords() const override { return {"engine", "command", "run"}; }
    core::Result<core::Json> execute(const core::Json& arguments) override;

private:
    ToolExecutor* executor_;
};

// Applies engine checks to an artifact and advances the validation lifecycle.
class ValidateArtifactCommand : public ICommand {
public:
    explicit ValidateArtifactCommand(validation::ValidationEngine& validation)
        : validation_(&validation) {}

    std::string command_id() const override { return "artifact.validate"; }
    std::string title() const override { return "Validate Artifact"; }
    std::string category() const override { return "Validation"; }
    std::vector<std::string> keywords() const override { return {"artifact", "validate", "verify"}; }
    core::Result<core::Json> execute(const core::Json& arguments) override;

private:
    validation::ValidationEngine* validation_;
};

// Reports the engine registry (id, version, capabilities, health).
class ListEnginesCommand : public ICommand {
public:
    std::string command_id() const override { return "engine.list"; }
    std::string title() const override { return "List Engines"; }
    std::string category() const override { return "Engines"; }
    std::vector<std::string> keywords() const override { return {"engine", "capabilities"}; }
    core::Result<core::Json> execute(const core::Json& arguments) override;
};

// The full built-in set, ready to register with a CommandRegistry.
std::vector<std::shared_ptr<ICommand>> builtin_commands(projects::ProjectStore& projects,
                                                        ToolExecutor& executor,
                                                        validation::ValidationEngine& validation);

}  // namespace trinity::commands
