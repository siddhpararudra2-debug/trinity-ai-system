// Trinity — application lifecycle (brief §Application lifecycle, §Configuration).
//
// The native core is a library: nothing in it needs a window, a server or a
// Python interpreter to run. Application is the composition root that wires the
// subsystems together in a defined order and tears them down in reverse, so the
// desktop shell (Qt or console) only has to call initialize() / shutdown() and
// then talk to the accessors.
//
//   initialize()
//     configuration -> storage layout -> logging -> crash handler
//     -> SQLite (open + migrate) -> settings -> engine registry
//     -> artifacts -> jobs (+event bus) -> projects -> validation
//     -> workflows -> commands -> plugins -> monitoring
//     -> publish app.started
//
//   shutdown()
//     publish app.stopped -> drain jobs -> unload plugins -> close logging
//     -> release subsystems (jobs before the DB they write to)
//
// Construction order is deliberate: every subsystem receives references to
// already-live dependencies, and the destruction order (reverse declaration) is
// asserted by the tests via ApplicationRoundTrip.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "../core/Error.hpp"
#include "../core/Json.hpp"

namespace trinity::core {
class EventBus;
}  // namespace trinity::core

namespace trinity::db {
class Database;
class Migrations;
}  // namespace trinity::db

namespace trinity::settings {
class SettingsStore;
}  // namespace trinity::settings

namespace trinity::projects {
class ProjectStore;
}  // namespace trinity::projects

namespace trinity::artifacts {
class ArtifactStore;
}  // namespace trinity::artifacts

namespace trinity::jobs {
class JobSystem;
}  // namespace trinity::jobs

namespace trinity::validation {
class ValidationEngine;
}  // namespace trinity::validation

namespace trinity::workflows {
class WorkflowRunner;
}  // namespace trinity::workflows

namespace trinity::commands {
class CommandRegistry;
class ToolExecutor;
}  // namespace trinity::commands

namespace trinity::plugins {
class PluginManager;
}  // namespace trinity::plugins

namespace trinity::monitoring {
class ResourceMonitor;
}  // namespace trinity::monitoring

namespace trinity::storage {
struct StorageLayout;
}  // namespace trinity::storage

namespace trinity::app {

// Configuration (brief §Configuration). Every field has a safe default; an
// empty path means "resolve from the environment" (never hardcoded).
struct ApplicationConfig {
    std::string data_dir;       // %LOCALAPPDATA%/Trinity by default
    std::string projects_dir;   // <Documents>/Trinity Projects by default
    std::string log_level = "INFO";
    bool enable_file_logging = true;
    bool enable_plugins = false;      // manifest discovery only; see PluginManager
    bool install_terminate_handler = true;
    std::size_t worker_threads = 0;   // 0 -> hardware concurrency
};

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Brings up the whole core. Idempotent: a second call is a no-op.
    core::Status initialize(const ApplicationConfig& config = ApplicationConfig());

    // Tears the core down. Safe to call multiple times and from the destructor.
    void shutdown();

    bool initialized() const;

    // ---- services (valid between initialize() and shutdown()) --------------
    core::EventBus& events();
    db::Database& database();
    settings::SettingsStore& settings();
    projects::ProjectStore& projects();
    artifacts::ArtifactStore& artifacts();
    jobs::JobSystem& jobs();
    validation::ValidationEngine& validation();
    workflows::WorkflowRunner& workflows();
    commands::ToolExecutor& executor();
    commands::CommandRegistry& commands();
    plugins::PluginManager& plugins();
    monitoring::ResourceMonitor& monitor();
    const storage::StorageLayout& layout() const;

    // Structured health/status document (status bar, Settings → General,
    // diagnostics bundle). Safe to call before initialize().
    core::Json status();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace trinity::app
