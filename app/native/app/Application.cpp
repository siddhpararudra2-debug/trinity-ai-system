#include "Application.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>
#include <utility>

#include "../core/FileSystem.hpp"

#include "../artifacts/Artifact.hpp"
#include "../commands/BuiltinCommands.hpp"
#include "../commands/CommandRegistry.hpp"
#include "../commands/Commands.hpp"
#include "../core/EventBus.hpp"
#include "../core/Logging.hpp"
#include "../core/Time.hpp"
#include "../db/Database.hpp"
#include "../db/Schema.hpp"
#include "../engines/Engine.hpp"
#include "../jobs/JobSystem.hpp"
#include "../monitoring/ResourceMonitor.hpp"
#include "../platform/Platform.hpp"
#include "../plugins/PluginManager.hpp"
#include "../projects/Project.hpp"
#include "../settings/Settings.hpp"
#include "../storage/Storage.hpp"
#include "../validation/ValidationEngine.hpp"
#include "../workflows/Workflow.hpp"

namespace trinity::app {
namespace {

core::ComponentLog log_("app");

core::LogLevel parse_log_level(const std::string& text) {
    std::string upper;
    upper.reserve(text.size());
    for (const char c : text) upper.push_back(static_cast<char>(std::toupper(c)));
    if (upper == "TRACE") return core::LogLevel::Trace;
    if (upper == "DEBUG") return core::LogLevel::Debug;
    if (upper == "WARNING" || upper == "WARN") return core::LogLevel::Warning;
    if (upper == "ERROR") return core::LogLevel::Error;
    if (upper == "CRITICAL") return core::LogLevel::Critical;
    return core::LogLevel::Info;
}

std::vector<std::string> split_directories(const std::string& value) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : value) {
        if (c == ';' || c == '\n') {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

}  // namespace

// ------------------------------------------------------------------------- Impl

struct Application::Impl {
    ApplicationConfig config;
    storage::StorageLayout layout;
    platform::PlatformInfo platform;

    // Declaration order == destruction order (reverse). Jobs are declared after
    // the stores they write through so they are torn down first.
    core::EventBus events;
    std::unique_ptr<db::Database> database;
    std::unique_ptr<settings::SettingsStore> settings;
    std::unique_ptr<projects::ProjectStore> projects;
    std::unique_ptr<artifacts::ArtifactStore> artifacts;
    std::unique_ptr<jobs::JobSystem> jobs;
    std::unique_ptr<validation::ValidationEngine> validation;
    std::unique_ptr<workflows::WorkflowRunner> workflows;
    std::unique_ptr<commands::ToolExecutor> executor;
    std::unique_ptr<commands::CommandRegistry> commands;
    std::unique_ptr<plugins::PluginManager> plugins;
    std::unique_ptr<monitoring::ResourceMonitor> monitor;

    bool initialized = false;
    std::size_t registered_engines = 0;
    std::size_t loaded_plugins = 0;
};

// ------------------------------------------------------------- construction

Application::Application() : impl_(std::make_unique<Impl>()) {}

Application::~Application() { shutdown(); }

bool Application::initialized() const { return impl_->initialized; }

core::Status Application::initialize(const ApplicationConfig& config) {
    if (impl_->initialized) return core::Status::ok();

    impl_->config = config;

    // 1. Platform + crash handling.
    if (config.install_terminate_handler) platform::install_terminate_handler();
    const platform::SystemEnvironment environment;
    impl_->platform = platform::describe_platform(environment);

    // 2. Storage layout (never hardcoded user paths).
    impl_->layout = storage::StorageLayout::resolve_with_overrides(
        environment, config.data_dir, config.projects_dir, platform::executable_directory());
    std::error_code ec;
    if (!impl_->layout.ensure_directories(ec)) {
        return core::Status::fail(core::Error(
            core::ErrorCode::PathValidationError,
            "cannot create the Trinity data layout: " + ec.message()));
    }

    // 3. Logging.
    core::Logger::instance().set_level(parse_log_level(config.log_level));
    if (config.enable_file_logging) {
        if (!core::Logger::instance().open_file_sink(impl_->layout.log_file)) {
            log_.warning("file log sink could not be opened",
                         [&] {
                             core::Json ctx = core::Json::object();
                             ctx["path"] = impl_->layout.log_file;
                             return ctx;
                         }());
        }
    }

    // 4. Database (open + migrate) — with legacy backend/storage fallback (D8 inversion).
    // If native DB does not exist but a legacy backend/storage/trinity.db does, copy it
    // before open_and_migrate so existing V1 data is preserved (single writer after).
    {
        std::error_code fs_ec;
        if (!std::filesystem::exists(impl_->layout.database_file, fs_ec) || fs_ec) {
            // Candidate legacy locations (relative to exe dir or cwd)
            std::vector<std::string> candidates;
            std::string exeDir = platform::executable_directory();
            if (!exeDir.empty()) {
                candidates.push_back(exeDir + "/../backend/storage/trinity.db");
                candidates.push_back(exeDir + "/../../backend/storage/trinity.db");
                candidates.push_back(exeDir + "/../../trinity-ai-system/backend/storage/trinity.db");
            }
            candidates.push_back("backend/storage/trinity.db");
            candidates.push_back("../backend/storage/trinity.db");
            candidates.push_back("storage/trinity.db");
            for (auto& cand : candidates) {
                std::error_code ec2;
                auto norm = core::FileSystem::normalise(cand).string();
                if (std::filesystem::exists(norm, ec2) && !ec2 && std::filesystem::file_size(norm, ec2) > 0 && !ec2) {
                    // Copy legacy DB into native location (best-effort, never overwrite native)
                    std::filesystem::copy_file(norm, impl_->layout.database_file, std::filesystem::copy_options::overwrite_existing, ec2);
                    if (!ec2) {
                        log_.info("migrated legacy backend DB", [&]{ core::Json c=core::Json::object(); c["from"]=norm; c["to"]=impl_->layout.database_file; return c; }());
                    }
                    break;
                }
            }
        }
    }
    auto opened = db::open_and_migrate(impl_->layout.database_file);
    if (opened.is_error()) return core::Status::fail(opened.take_error());
    impl_->database = std::make_unique<db::Database>(opened.take_value());

    // 5. Settings manager.
    impl_->settings = std::make_unique<settings::SettingsStore>(*impl_->database);
    if (const core::Status seeded = impl_->settings->seed_defaults(); !seeded.is_ok()) {
        return seeded;
    }

    // 6. Engine registry (CAD, math, scaffolded domains).
    impl_->registered_engines = engines::bootstrap_builtin_engines().size();

    // 7. Workspace root: explicit config, else the configured setting, else the
    //    resolved default.
    std::string projects_root = impl_->layout.projects_dir;
    if (auto configured = impl_->settings->workspace_root();
        configured.is_ok() && !configured.value().empty()) {
        projects_root = configured.value();
    }
    impl_->layout.projects_dir = projects_root;

    // 8. Artifacts (single writer) then jobs (they write artifacts + DB).
    impl_->artifacts =
        std::make_unique<artifacts::ArtifactStore>(*impl_->database, impl_->layout.artifacts_dir);
    impl_->jobs = std::make_unique<jobs::JobSystem>(*impl_->database, *impl_->artifacts,
                                                    config.worker_threads);
    impl_->jobs->set_event_bus(&impl_->events);

    // 9. Projects, validation, workflows, commands.
    impl_->projects = std::make_unique<projects::ProjectStore>(*impl_->database, projects_root);
    impl_->validation =
        std::make_unique<validation::ValidationEngine>(*impl_->database, *impl_->artifacts);
    impl_->workflows =
        std::make_unique<workflows::WorkflowRunner>(*impl_->jobs, *impl_->database);
    impl_->executor = std::make_unique<commands::ToolExecutor>(*impl_->jobs);

    impl_->commands = std::make_unique<commands::CommandRegistry>(&impl_->events);
    for (const auto& command : commands::builtin_commands(*impl_->projects, *impl_->executor,
                                                          *impl_->validation)) {
        if (const core::Status status = impl_->commands->register_command(command);
            !status.is_ok()) {
            log_.warning("built-in command not registered", status.error().to_json());
        }
    }

    // 10. Plugins: manifest discovery only (never executes third-party code).
    impl_->plugins = std::make_unique<plugins::PluginManager>(&impl_->events);
    const bool plugins_enabled =
        config.enable_plugins ||
        (impl_->settings->get("plugins.enabled").value_or(std::string("false")) == "true");
    if (plugins_enabled) {
        std::vector<std::string> directories{impl_->layout.plugins_dir};
        for (const std::string& configured :
             split_directories(impl_->settings->get("plugins.directories").value_or(std::string()))) {
            directories.push_back(configured);
        }
        for (const std::string& directory : directories) {
            if (auto discovered = impl_->plugins->discover(directory); discovered.is_ok()) {
                impl_->loaded_plugins += discovered.value();
            }
        }
        impl_->plugins->load_all();
    }

    // 11. Monitoring.
    impl_->monitor = std::make_unique<monitoring::ResourceMonitor>();

    impl_->initialized = true;

    core::Json payload = core::Json::object();
    payload["data_dir"] = impl_->layout.data_dir;
    payload["database"] = impl_->layout.database_file;
    payload["projects_dir"] = impl_->layout.projects_dir;
    payload["engines"] = static_cast<double>(impl_->registered_engines);
    payload["plugins"] = static_cast<double>(impl_->loaded_plugins);
    payload["worker_threads"] = static_cast<double>(impl_->config.worker_threads);
    impl_->events.publish(core::topics::kApplicationStarted, payload);
    log_.info("trinity core initialized", payload);
    return core::Status::ok();
}

void Application::shutdown() {
    if (!impl_ || !impl_->initialized) {
        if (impl_) impl_->initialized = false;
        return;
    }
    impl_->initialized = false;

    core::Json payload = core::Json::object();
    payload["database"] = impl_->layout.database_file;
    impl_->events.publish(core::topics::kApplicationStopped, payload);

    // Drain in-flight jobs before the DB and artifact store they write to go away.
    if (impl_->jobs) impl_->jobs->wait_for_idle();
    if (impl_->plugins) impl_->plugins->unload_all();

    impl_->monitor.reset();
    impl_->plugins.reset();
    impl_->commands.reset();
    impl_->executor.reset();
    impl_->workflows.reset();
    impl_->validation.reset();
    impl_->jobs.reset();
    impl_->artifacts.reset();
    impl_->projects.reset();
    impl_->settings.reset();
    impl_->database.reset();

    core::Logger::instance().close_file_sink();
    log_.info("trinity core shut down", payload);
}

// ------------------------------------------------------------------ accessors

core::EventBus& Application::events() { return impl_->events; }

db::Database& Application::database() {
    if (!impl_->database) {
        throw core::TrinityException(core::Error(core::ErrorCode::DatabaseError,
                                                 "application is not initialized"));
    }
    return *impl_->database;
}

settings::SettingsStore& Application::settings() {
    if (!impl_->settings) {
        throw core::TrinityException(core::Error(core::ErrorCode::TrinityError,
                                                 "application is not initialized"));
    }
    return *impl_->settings;
}

projects::ProjectStore& Application::projects() {
    if (!impl_->projects) {
        throw core::TrinityException(core::Error(core::ErrorCode::TrinityError,
                                                 "application is not initialized"));
    }
    return *impl_->projects;
}

artifacts::ArtifactStore& Application::artifacts() {
    if (!impl_->artifacts) {
        throw core::TrinityException(core::Error(core::ErrorCode::TrinityError,
                                                 "application is not initialized"));
    }
    return *impl_->artifacts;
}

jobs::JobSystem& Application::jobs() {
    if (!impl_->jobs) {
        throw core::TrinityException(core::Error(core::ErrorCode::TrinityError,
                                                 "application is not initialized"));
    }
    return *impl_->jobs;
}

validation::ValidationEngine& Application::validation() {
    if (!impl_->validation) {
        throw core::TrinityException(core::Error(core::ErrorCode::TrinityError,
                                                 "application is not initialized"));
    }
    return *impl_->validation;
}

workflows::WorkflowRunner& Application::workflows() {
    if (!impl_->workflows) {
        throw core::TrinityException(core::Error(core::ErrorCode::TrinityError,
                                                 "application is not initialized"));
    }
    return *impl_->workflows;
}

commands::ToolExecutor& Application::executor() {
    if (!impl_->executor) {
        throw core::TrinityException(core::Error(core::ErrorCode::TrinityError,
                                                 "application is not initialized"));
    }
    return *impl_->executor;
}

commands::CommandRegistry& Application::commands() {
    if (!impl_->commands) {
        throw core::TrinityException(core::Error(core::ErrorCode::TrinityError,
                                                 "application is not initialized"));
    }
    return *impl_->commands;
}

plugins::PluginManager& Application::plugins() {
    if (!impl_->plugins) {
        throw core::TrinityException(core::Error(core::ErrorCode::TrinityError,
                                                 "application is not initialized"));
    }
    return *impl_->plugins;
}

monitoring::ResourceMonitor& Application::monitor() {
    if (!impl_->monitor) {
        throw core::TrinityException(core::Error(core::ErrorCode::TrinityError,
                                                 "application is not initialized"));
    }
    return *impl_->monitor;
}

const storage::StorageLayout& Application::layout() const { return impl_->layout; }

core::Json Application::status() {
    core::Json out = core::Json::object();
    out["initialized"] = impl_->initialized;
    out["platform"] = impl_->platform.to_json();
    out["layout"] = impl_->layout.to_json();
    out["registered_engines"] = static_cast<double>(impl_->registered_engines);

    core::Json engine_items = core::Json::array();
    for (const engines::EngineDescriptor& descriptor : engines::EngineRegistry::instance().list()) {
        engine_items.push_back(descriptor.to_json());
    }
    out["engines"] = engine_items;

    if (impl_->jobs) {
        core::Json job_stats = core::Json::object();
        job_stats["queued"] = static_cast<double>(impl_->jobs->queued_count());
        job_stats["active"] = static_cast<double>(impl_->jobs->active_count());
        out["jobs"] = job_stats;
    }
    if (impl_->commands) {
        out["commands"] = static_cast<double>(impl_->commands->count());
    }
    if (impl_->plugins) out["plugins"] = impl_->plugins->status();
    if (impl_->monitor) {
        const monitoring::ResourceSample sample = impl_->monitor->sample();
        core::Json resources = core::Json::object();
        resources["cpu_percent"] = sample.process_cpu_percent;
        resources["working_set_bytes"] = static_cast<double>(sample.process_working_set_bytes);
        resources["thread_count"] = static_cast<double>(sample.process_thread_count);
        out["resources"] = resources;
    }
    out["timestamp"] = core::iso_utc_now();
    return out;
}

}  // namespace trinity::app
