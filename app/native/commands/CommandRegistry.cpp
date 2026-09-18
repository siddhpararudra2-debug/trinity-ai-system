#include "CommandRegistry.hpp"

#include <algorithm>
#include <exception>

#include "../core/Logging.hpp"

namespace trinity::commands {
namespace {
core::ComponentLog log_("commands");
}  // namespace

core::Json ICommand::describe() const {
    core::Json out = core::Json::object();
    out["command_id"] = command_id();
    out["title"] = title();
    out["category"] = category();
    core::Json keyword_array = core::Json::array();
    for (const std::string& keyword : keywords()) keyword_array.push_back(core::Json(keyword));
    out["keywords"] = keyword_array;
    return out;
}

CommandRegistry::CommandRegistry(core::EventBus* events) : events_(events) {}

core::Status CommandRegistry::register_command(std::shared_ptr<ICommand> command) {
    if (!command) {
        return core::Status::fail(core::Error(core::ErrorCode::RequestValidationError,
                                             "command instance is required"));
    }
    const std::string id = command->command_id();
    if (id.empty()) {
        return core::Status::fail(core::Error(core::ErrorCode::RequestValidationError,
                                             "command id is required"));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (commands_.count(id) > 0) {
        return core::Status::fail(core::Error(core::ErrorCode::RequestValidationError,
                                             "command '" + id + "' is already registered"));
    }
    commands_[id] = std::move(command);
    return core::Status::ok();
}

core::Result<std::shared_ptr<ICommand>> CommandRegistry::find(const std::string& command_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = commands_.find(command_id);
    if (it == commands_.end()) {
        return core::Result<std::shared_ptr<ICommand>>::fail(core::Error(
            core::ErrorCode::RequestValidationError, "no command registered as '" + command_id + "'"));
    }
    return core::Result<std::shared_ptr<ICommand>>::ok(it->second);
}

core::Result<core::Json> CommandRegistry::execute(const std::string& command_id,
                                                  const core::Json& arguments) {
    std::shared_ptr<ICommand> command;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = commands_.find(command_id);
        if (it == commands_.end()) {
            return core::Result<core::Json>::fail(core::Error(
                core::ErrorCode::RequestValidationError,
                "no command registered as '" + command_id + "'"));
        }
        command = it->second;
    }

    // Commands run with no registry lock held, so a command may itself execute
    // another registered command.
    core::Result<core::Json> result = core::Result<core::Json>::fail(
        core::Error(core::ErrorCode::TrinityError, "command produced no result"));
    try {
        result = command->execute(arguments);
    } catch (const core::TrinityException& exception) {
        result = core::Result<core::Json>::fail(exception.error());
    } catch (const std::exception& exception) {
        result = core::Result<core::Json>::fail(
            core::Error(core::ErrorCode::EngineExecutionError, exception.what()));
    } catch (...) {
        result = core::Result<core::Json>::fail(core::Error(
            core::ErrorCode::EngineExecutionError, "command '" + command_id + "' failed"));
    }

    if (events_ != nullptr) {
        core::Json payload = core::Json::object();
        payload["command_id"] = command_id;
        payload["ok"] = result.is_ok();
        payload["error"] = result.is_error() ? result.error().to_json() : core::Json(nullptr);
        events_->publish(core::topics::kCommandExecuted, payload);
    }
    if (result.is_error()) {
        log_.warning("command failed", [&] {
            core::Json ctx = core::Json::object();
            ctx["command_id"] = command_id;
            ctx["error"] = result.error().to_json();
            return ctx;
        }());
    }
    return result;
}

std::vector<core::Json> CommandRegistry::catalogue() const {
    std::vector<core::Json> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        out.reserve(commands_.size());
        for (const auto& entry : commands_) out.push_back(entry.second->describe());
    }
    std::sort(out.begin(), out.end(), [](const core::Json& left, const core::Json& right) {
        const core::Json* left_category = left.find("category");
        const core::Json* right_category = right.find("category");
        const std::string left_key =
            (left_category != nullptr ? left_category->as_string() : std::string()) + "\x1f" +
            (left.find("title") != nullptr ? left.find("title")->as_string() : std::string());
        const std::string right_key =
            (right_category != nullptr ? right_category->as_string() : std::string()) + "\x1f" +
            (right.find("title") != nullptr ? right.find("title")->as_string() : std::string());
        return left_key < right_key;
    });
    return out;
}

std::size_t CommandRegistry::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return commands_.size();
}

}  // namespace trinity::commands
