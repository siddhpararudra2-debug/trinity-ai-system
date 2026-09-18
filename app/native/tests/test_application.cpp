// Trinity — Application (composition root) end-to-end tests.
//
// These prove the brief's central claim: the native core comes up, does real
// work and shuts down cleanly with no UI, no Next.js and no Python in the
// process. Interface conformance is asserted at compile time.
#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "../app/Application.hpp"
#include "../commands/BuiltinCommands.hpp"
#include "../core/FileSystem.hpp"
#include "../core/Uuid.hpp"
#include "../engines/cad/CadEngine.hpp"
#include "../engines/math/MathEngine.hpp"
#include "../interfaces/CoreInterfaces.hpp"

using namespace trinity;

// ---------------------------------------------------- interface conformance

static_assert(std::is_base_of<engines::IEngine, engines::cad::CadEngine>::value,
              "CadEngine must satisfy IEngine");
static_assert(std::is_base_of<engines::IEngine, engines::math::MathEngine>::value,
              "MathEngine must satisfy IEngine");
static_assert(std::is_base_of<jobs::IJobManager, jobs::JobSystem>::value,
              "JobSystem must satisfy IJobManager");
static_assert(std::is_base_of<jobs::IJob, jobs::JobHandle>::value,
              "JobHandle must satisfy IJob");
static_assert(std::is_base_of<artifacts::IArtifactStore, artifacts::ArtifactStore>::value,
              "ArtifactStore must satisfy IArtifactStore");
static_assert(std::is_base_of<projects::IProjectStore, projects::ProjectStore>::value,
              "ProjectStore must satisfy IProjectStore");
static_assert(std::is_base_of<validation::IValidator, validation::ValidationEngine>::value,
              "ValidationEngine must satisfy IValidator");
static_assert(std::is_base_of<workflows::IWorkflow, workflows::WorkflowRunner>::value,
              "WorkflowRunner must satisfy IWorkflow");
static_assert(std::is_base_of<plugins::IPlugin, plugins::ManifestPlugin>::value,
              "ManifestPlugin must satisfy IPlugin");
static_assert(std::is_base_of<commands::ICommand, commands::NewProjectCommand>::value,
              "NewProjectCommand must satisfy ICommand");
static_assert(std::is_base_of<model::IModelProvider, model::NullModelProvider>::value,
              "NullModelProvider must satisfy IModelProvider");

namespace {

std::string fresh_temp_dir(const std::string& label) {
    const std::filesystem::path base =
        std::filesystem::temp_directory_path() /
        ("trinity_" + label + "_" + core::new_uuid().substr(0, 8));
    std::error_code ec;
    core::FileSystem::ensure_directory(base.string(), ec);
    return base.string();
}

app::ApplicationConfig test_config(const std::string& root) {
    app::ApplicationConfig config;
    config.data_dir = root;
    config.projects_dir = root + "/projects";
    config.enable_file_logging = false;
    config.install_terminate_handler = false;
    config.worker_threads = 2;
    return config;
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms = 5000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

std::string create_project(app::Application& application, const std::string& name) {
    core::Json arguments = core::Json::object();
    arguments["name"] = name;
    auto created = application.commands().execute("workspace.new_project", arguments);
    if (created.is_error()) return std::string();
    const core::Json* project_id = created.value().find("project_id");
    return project_id != nullptr ? project_id->as_string() : std::string();
}

std::string run_engineering_command(app::Application& application, const std::string& text,
                                    const std::string& project_id) {
    core::Json arguments = core::Json::object();
    arguments["text"] = text;
    arguments["project_id"] = project_id;
    auto outcome = application.commands().execute("engine.run_command", arguments);
    if (outcome.is_error()) return std::string();
    const core::Json* job_id = outcome.value().find("job_id");
    return job_id != nullptr ? job_id->as_string() : std::string();
}

}  // namespace

TEST_CASE("Application brings the native core up without any UI") {
    const std::string root = fresh_temp_dir("app_boot");

    app::Application application;
    CHECK_FALSE(application.initialized());

    const app::ApplicationConfig config = test_config(root);
    REQUIRE(application.initialize(config).is_ok());
    CHECK(application.initialized());

    // Engines (CAD + math + scaffolded domains) and commands are registered.
    CHECK(engines::EngineRegistry::instance().count() >= 3);
    CHECK(application.commands().count() >= 5);

    // Settings manager is seeded and readable.
    auto level = application.settings().get("logging.level");
    REQUIRE(level.is_ok());
    CHECK_FALSE(level.value().empty());

    // The storage layout points inside the configured data directory.
    CHECK(application.layout().database_file.find(root) != std::string::npos);
    CHECK(std::filesystem::exists(application.layout().database_file));

    const core::Json status = application.status();
    REQUIRE(status.find("platform") != nullptr);
    CHECK(status.find("initialized")->as_bool());
    CHECK(status.find("engines")->is_array());
    CHECK(status.find("jobs") != nullptr);

    // Idempotent initialize; clean idempotent shutdown.
    CHECK(application.initialize(config).is_ok());
    application.shutdown();
    CHECK_FALSE(application.initialized());
    application.shutdown();

    std::error_code ec;
    core::FileSystem::remove_all(root, ec);
}

TEST_CASE("Application runs the deterministic command pipeline and publishes job events") {
    const std::string root = fresh_temp_dir("app_pipeline");
    app::Application application;
    REQUIRE(application.initialize(test_config(root)).is_ok());

    std::mutex mutex;
    std::vector<core::Json> finished;
    std::atomic<int> progress_events{0};
    application.events().subscribe(core::topics::kJobFinished, [&](const core::Event& event) {
        std::lock_guard<std::mutex> lock(mutex);
        finished.push_back(event.payload);
    });
    application.events().subscribe(core::topics::kJobProgress,
                                   [&](const core::Event&) { progress_events.fetch_add(1); });

    const std::string project_id = create_project(application, "Drone");
    REQUIRE_FALSE(project_id.empty());

    const std::string job_id = run_engineering_command(application, "calculate 2+2*3", project_id);
    REQUIRE_FALSE(job_id.empty());

    application.jobs().wait_for_idle();
    auto record = application.jobs().get(job_id);
    REQUIRE(record.is_ok());
    CHECK(record.value().status == jobs::JobState::Completed);
    CHECK(record.value().engine == "math");
    CHECK(record.value().project_id == project_id);

    const core::Json* value = record.value().output.find("result");
    REQUIRE(value != nullptr);
    CHECK(value->as_double() == doctest::Approx(8.0));

    // Progress and terminal events reached the bus (no UI thread involved).
    CHECK(progress_events.load() > 0);
    REQUIRE(wait_until([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return !finished.empty();
    }));
    {
        std::lock_guard<std::mutex> lock(mutex);
        CHECK(finished.at(0).find("job_id")->as_string() == job_id);
        CHECK(finished.at(0).find("status")->as_string() == "COMPLETED");
        CHECK(finished.at(0).find("engine")->as_string() == "math");
    }

    // The IJob handle reports the same live state as the manager.
    jobs::JobHandle handle = application.jobs().handle(job_id);
    CHECK(handle.state() == jobs::JobState::Completed);
    CHECK(handle.terminal());
    CHECK(handle.engine() == "math");

    application.shutdown();
    std::error_code ec;
    core::FileSystem::remove_all(root, ec);
}

TEST_CASE("Application produces, stores and verifies a CAD artifact") {
    const std::string root = fresh_temp_dir("app_cad");
    app::Application application;
    REQUIRE(application.initialize(test_config(root)).is_ok());

    const std::string project_id = create_project(application, "Frame");
    REQUIRE_FALSE(project_id.empty());

    const std::string job_id =
        run_engineering_command(application, "create a 50 mm quadcopter frame", project_id);
    REQUIRE_FALSE(job_id.empty());
    application.jobs().wait_for_idle();

    auto record = application.jobs().get(job_id);
    REQUIRE(record.is_ok());
    REQUIRE(record.value().status == jobs::JobState::Completed);

    // The engine produced scratch files; the shell moves them into the store
    // through the single writer.
    const core::Json* pending = record.value().output.find("pending_artifacts");
    REQUIRE(pending != nullptr);
    REQUIRE(pending->is_array());
    std::string stl_path;
    for (const core::Json& entry : pending->as_array()) {
        const core::Json* type = entry.find("type");
        const core::Json* path = entry.find("path");
        if (type != nullptr && path != nullptr && type->as_string() == "stl") {
            stl_path = path->as_string();
        }
    }
    REQUIRE_FALSE(stl_path.empty());

    auto stored =
        application.artifacts().store_file(stl_path, "stl", project_id, job_id, "cad", "1.0");
    REQUIRE(stored.is_ok());
    CHECK(stored.value().validation_state == artifacts::ValidationState::Generated);
    CHECK_FALSE(stored.value().hash_sha256.empty());
    CHECK(stored.value().size_bytes > 0);

    auto integrity = application.artifacts().verify_integrity(stored.value().artifact_id);
    REQUIRE(integrity.is_ok());
    CHECK(integrity.value());

    // GENERATED -> VALIDATED -> VERIFIED, each step recorded as evidence.
    core::Json checks = core::Json::object();
    checks["triangle_count"] = true;
    auto validated = application.validation().apply_checks(stored.value().artifact_id, "cad", checks,
                                                           true, false);
    REQUIRE(validated.is_ok());
    CHECK(validated.value() == artifacts::ValidationState::Validated);

    auto verified = application.validation().apply_checks(stored.value().artifact_id, "cad", checks,
                                                          true, true);
    REQUIRE(verified.is_ok());
    CHECK(verified.value() == artifacts::ValidationState::Verified);

    // Verification never silently downgrades: re-validating is refused.
    auto illegal = application.validation().apply_checks(stored.value().artifact_id, "cad", checks,
                                                         true, false);
    CHECK(illegal.is_error());

    auto history = application.validation().history_for_artifact(stored.value().artifact_id);
    REQUIRE(history.is_ok());
    CHECK(history.value().size() == 2);

    auto artifacts = application.artifacts().list_for_project(project_id);
    REQUIRE(artifacts.is_ok());
    CHECK(artifacts.value().size() >= 1);

    application.shutdown();
    std::error_code ec;
    core::FileSystem::remove_all(root, ec);
}

TEST_CASE("Application discovers plugin manifests from its plugin directory") {
    const std::string root = fresh_temp_dir("app_plugins");
    std::error_code ec;
    REQUIRE(core::FileSystem::ensure_directory(root + "/plugins", ec));

    std::error_code write_ec;
    REQUIRE(core::FileSystem::write_file_atomic(
        root + "/plugins/sample.plugin.json",
        R"({"plugin_id":"trinity.sample","name":"Sample Plugin","version":"1.0.0",
            "capabilities":["cad.preview"]})",
        write_ec));

    app::ApplicationConfig config = test_config(root);
    config.enable_plugins = true;

    app::Application application;
    REQUIRE(application.initialize(config).is_ok());
    CHECK(application.plugins().count() == 1);
    CHECK(application.plugins().loaded_count() == 1);

    auto record = application.plugins().get("trinity.sample");
    REQUIRE(record.is_ok());
    CHECK(record.value().state == plugins::PluginState::Loaded);
    CHECK(record.value().manifest.capabilities.size() == 1);

    application.shutdown();
    core::FileSystem::remove_all(root, ec);
}

TEST_CASE("Command argument validation fails loudly instead of guessing") {
    const std::string root = fresh_temp_dir("app_args");
    app::Application application;
    REQUIRE(application.initialize(test_config(root)).is_ok());

    auto missing_argument =
        application.commands().execute("workspace.new_project", core::Json::object());
    REQUIRE(missing_argument.is_error());
    CHECK(missing_argument.error().code() == core::ErrorCode::RequestValidationError);
    CHECK(missing_argument.error().message().find("name") != std::string::npos);

    auto unknown_command = application.commands().execute("does.not.exist");
    REQUIRE(unknown_command.is_error());
    CHECK(unknown_command.error().code() == core::ErrorCode::RequestValidationError);

    // An unparseable engineering command is refused by the planner, not guessed.
    core::Json arguments = core::Json::object();
    arguments["text"] = "optimize the aerodynamic profile";
    auto unplanned = application.commands().execute("engine.run_command", arguments);
    REQUIRE(unplanned.is_error());
    CHECK(unplanned.error().code() == core::ErrorCode::RequestValidationError);

    application.shutdown();
    std::error_code ec;
    core::FileSystem::remove_all(root, ec);
}
