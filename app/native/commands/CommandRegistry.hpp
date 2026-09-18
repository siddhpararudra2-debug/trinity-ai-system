// Trinity — command registry (brief §Command System, §Core interfaces: ICommand).
//
// The existing pipeline (parse_command -> ICommandPlanner -> ToolExecutor) turns
// free text into engine work. This layer registers *named* commands with a
// stable id, a title and a category so the palette, menus and keyboard
// shortcuts dispatch through one table instead of ad-hoc switch statements.
//
// Commands are deterministic: they call core services and never a model. The
// future model layer plans *engine* work; it does not replace these.
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../core/Error.hpp"
#include "../core/EventBus.hpp"
#include "../core/Json.hpp"

namespace trinity::commands {

class ICommand {
public:
    virtual ~ICommand() = default;

    virtual std::string command_id() const = 0;   // "workspace.new_project"
    virtual std::string title() const = 0;        // "New Project"
    virtual std::string category() const = 0;     // "Workspace"
    virtual std::vector<std::string> keywords() const { return {}; }

    // Executes the command with structured arguments. Throwing
    // core::TrinityException is allowed and converted by the registry.
    virtual core::Result<core::Json> execute(const core::Json& arguments) = 0;

    // Palette metadata (id, title, category, keywords).
    virtual core::Json describe() const;
};

class CommandRegistry {
public:
    explicit CommandRegistry(core::EventBus* events = nullptr);

    core::Status register_command(std::shared_ptr<ICommand> command);

    core::Result<std::shared_ptr<ICommand>> find(const std::string& command_id) const;

    // Executes a registered command and publishes "command.executed".
    core::Result<core::Json> execute(const std::string& command_id,
                                     const core::Json& arguments = core::Json::object());

    // Palette catalogue (sorted by category, then title).
    std::vector<core::Json> catalogue() const;

    std::size_t count() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<ICommand>> commands_;
    core::EventBus* events_;
};

}  // namespace trinity::commands
