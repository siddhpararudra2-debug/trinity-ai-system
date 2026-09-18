// Trinity — console shell (trinity-shell).
//
// A thin front-end over the native core that proves the whole stack end to end
// with no browser and no server: configuration, storage layout, SQLite
// migrations, projects, engines, async jobs, artifacts, the command pipeline,
// the validation lifecycle and the event bus.
//
// The Qt Quick desktop application (guarded by TRINITY_WITH_QT) is a sibling
// front-end: it consumes exactly the same Application and the same services, so
// nothing here is shell-specific logic.
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "../app/Application.hpp"
#include "../artifacts/Artifact.hpp"
#include "../core/EventBus.hpp"
#include "../core/Logging.hpp"
#include "../engines/Engine.hpp"
#include "../jobs/JobSystem.hpp"
#include "../storage/Storage.hpp"

using namespace trinity;  // shell-local convenience

namespace {

void print_banner() {
    std::printf(
        "TRINITY — Engineering Operating System (native console shell)\n"
        "  MODEL -> REASON -> TOOLS -> EXECUTE -> VALIDATE -> VERIFY\n"
        "  type 'help' for commands, 'quit' to exit\n\n");
}

void print_help() {
    std::printf(
        "  help                          this text\n"
        "  status                        platform, storage, jobs, engines\n"
        "  palette                       list registered commands (command palette)\n"
        "  new project <name>            create a project\n"
        "  list projects                 list projects\n"
        "  use project <id>              select the active project\n"
        "  create a <n> mm quadcopter frame\n"
        "                                deterministic CAD generation as an async job\n"
        "  calculate <expression>        evaluate a math expression\n"
        "  jobs                          recent jobs\n"
        "  artifacts                     artifacts of the active project\n"
        "  validate <artifact_id>        run checks: GENERATED -> VALIDATED\n"
        "  verify <artifact_id>          promote VALIDATED -> VERIFIED with evidence\n"
        "  quit\n");
}

std::string read_line() {
    std::string line;
    if (!std::getline(std::cin, line)) return "quit";
    return line;
}

std::vector<std::string> split_args(const std::string& line) {
    std::vector<std::string> out;
    std::string current;
    bool in_quotes = false;
    for (const char c : line) {
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ' ' && !in_quotes) {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

// Resolves a possibly-abbreviated artifact id against the active project.
std::string resolve_artifact_id(artifacts::ArtifactStore& store, const std::string& project_id,
                               const std::string& prefix) {
    if (prefix.empty()) return std::string();
    auto listed = store.list_for_project(project_id);
    if (listed.is_error()) return prefix;
    for (const artifacts::Artifact& artifact : listed.value()) {
        if (artifact.artifact_id.rfind(prefix, 0) == 0) return artifact.artifact_id;
    }
    return prefix;
}

}  // namespace

int main() {
    core::ComponentLog log("shell");

    app::Application application;
    if (const core::Status initialized = application.initialize(app::ApplicationConfig{});
        !initialized.is_ok()) {
        std::fprintf(stderr, "FATAL: %s\n", initialized.error().message().c_str());
        return 1;
    }

    // Job progress and log lines stream to the console over the event bus, so
    // the input loop never polls the job system.
    application.events().subscribe(core::topics::kJobProgress, [](const core::Event& event) {
        const core::Json* job_id = event.payload.find("job_id");
        const core::Json* progress = event.payload.find("progress");
        if (job_id != nullptr && progress != nullptr) {
            std::printf("  [job %.8s] %.0f%%\n", job_id->as_string().c_str(),
                        progress->as_double() * 100.0);
        }
    });
    application.events().subscribe(core::topics::kJobLog, [](const core::Event& event) {
        const core::Json* line = event.payload.find("line");
        if (line != nullptr) std::printf("  [log] %s\n", line->as_string().c_str());
    });

    projects::ProjectStore& project_store = application.projects();
    artifacts::ArtifactStore& artifact_store = application.artifacts();
    jobs::JobSystem& job_system = application.jobs();
    validation::ValidationEngine& validation_engine = application.validation();
    commands::ToolExecutor& command_executor = application.executor();

    std::string active_project;
    print_banner();

    while (true) {
        const std::string prompt =
            active_project.empty() ? std::string("trinity> ")
                                   : "trinity [" + active_project.substr(0, 8) + "]> ";
        std::printf("%s", prompt.c_str());
        std::fflush(stdout);

        const std::string line = read_line();
        if (line == "quit" || line == "exit") break;
        if (line.empty()) continue;

        if (line == "help") {
            print_help();
            continue;
        }

        if (line == "status") {
            const core::Json status = application.status();
            std::printf("  data dir:      %s\n", application.layout().data_dir.c_str());
            std::printf("  database:      %s\n", application.layout().database_file.c_str());
            std::printf("  projects root: %s\n", application.layout().projects_dir.c_str());
            std::printf("  commands:      %zu\n", application.commands().count());
            std::printf("  jobs:          %zu queued, %zu running\n", job_system.queued_count(),
                        job_system.active_count());
            std::printf("  engines:\n");
            for (const engines::EngineDescriptor& descriptor :
                 engines::EngineRegistry::instance().list()) {
                std::printf("    %-10s v%-6s [%s] %s\n", descriptor.id.c_str(),
                            descriptor.version.c_str(),
                            engines::engine_health_string(descriptor.health),
                            descriptor.health_detail.c_str());
            }
            (void)status;
            continue;
        }

        if (line == "palette") {
            for (const core::Json& entry : application.commands().catalogue()) {
                const core::Json* id = entry.find("command_id");
                const core::Json* title = entry.find("title");
                const core::Json* category = entry.find("category");
                std::printf("  %-28s %-26s [%s]\n",
                            id != nullptr ? id->as_string().c_str() : "",
                            title != nullptr ? title->as_string().c_str() : "",
                            category != nullptr ? category->as_string().c_str() : "");
            }
            continue;
        }

        if (line.rfind("new project", 0) == 0 || line.rfind("create project", 0) == 0) {
            const std::vector<std::string> args = split_args(line);
            if (args.size() < 3) {
                std::printf("  usage: new project <name>\n");
                continue;
            }
            core::Json arguments = core::Json::object();
            arguments["name"] = args.back();
            auto created = application.commands().execute("workspace.new_project", arguments);
            if (created.is_error()) {
                std::printf("  error: %s\n", created.error().message().c_str());
                continue;
            }
            const core::Json* project_id = created.value().find("project_id");
            active_project = project_id != nullptr ? project_id->as_string() : std::string();
            const core::Json* workspace = created.value().find("workspace");
            std::printf("  created project %.8s @ %s\n", active_project.c_str(),
                        workspace != nullptr ? workspace->as_string().c_str() : "");
            continue;
        }

        if (line == "list projects") {
            core::Json arguments = core::Json::object();
            arguments["include_archived"] = true;
            auto listed = application.commands().execute("workspace.list_projects", arguments);
            if (listed.is_error()) {
                std::printf("  error: %s\n", listed.error().message().c_str());
                continue;
            }
            for (const core::Json& project : listed.value().find("projects")->as_array()) {
                const core::Json* id = project.find("project_id");
                const core::Json* name = project.find("name");
                const core::Json* archived = project.find("archived");
                std::printf("  %s  %-30s %s\n", id != nullptr ? id->as_string().c_str() : "",
                            name != nullptr ? name->as_string().c_str() : "",
                            (archived != nullptr && archived->as_bool()) ? "(archived)" : "");
            }
            continue;
        }

        if (line.rfind("use project", 0) == 0) {
            const std::vector<std::string> args = split_args(line);
            active_project = args.empty() ? std::string() : args.back();
            auto fetched = project_store.get(active_project);
            if (fetched.is_error()) {
                std::printf("  unknown project id\n");
                active_project.clear();
            } else {
                std::printf("  active project: %s\n", fetched.value().name.c_str());
            }
            continue;
        }

        if (line.rfind("create a", 0) == 0 || line.rfind("generate", 0) == 0 ||
            line.rfind("calculate", 0) == 0) {
            core::Json arguments = core::Json::object();
            arguments["text"] = line;
            arguments["project_id"] = active_project;
            auto outcome = application.commands().execute("engine.run_command", arguments);
            if (outcome.is_error()) {
                std::printf("  no deterministic plan: %s\n", outcome.error().message().c_str());
                continue;
            }
            const core::Json* job_id = outcome.value().find("job_id");
            const std::string submitted = job_id != nullptr ? job_id->as_string() : std::string();
            std::printf("  job %.8s submitted\n", submitted.c_str());

            job_system.wait_for_idle();
            auto record = job_system.get(submitted);
            if (record.is_error()) {
                std::printf("  error: %s\n", record.error().message().c_str());
                continue;
            }
            std::printf("  status: %s\n", jobs::job_state_string(record.value().status));

            const core::Json* pending = record.value().output.find("pending_artifacts");
            if (pending == nullptr || !pending->is_array()) continue;
            for (const core::Json& entry : pending->as_array()) {
                const core::Json* path = entry.find("path");
                const core::Json* type = entry.find("type");
                if (path == nullptr || type == nullptr) continue;
                auto stored = artifact_store.store_file(path->as_string(), type->as_string(),
                                                        active_project, record.value().job_id,
                                                        record.value().engine, "1.0");
                if (stored.is_ok()) {
                    std::printf("  artifact %.8s (%s, sha256 %s...)\n",
                                stored.value().artifact_id.c_str(), stored.value().type.c_str(),
                                stored.value().hash_sha256.substr(0, 12).c_str());
                } else {
                    std::printf("  artifact store error: %s\n",
                                stored.error().message().c_str());
                }
            }
            continue;
        }

        if (line == "jobs") {
            auto recent = job_system.list_recent(20);
            for (const jobs::JobRecord& job : recent.value_or({})) {
                std::printf("  %.8s  %-10s %-6s.%-10s %s\n", job.job_id.c_str(),
                            jobs::job_state_string(job.status), job.engine.c_str(),
                            job.operation.c_str(), job.created_at.c_str());
            }
            continue;
        }

        if (line == "artifacts") {
            if (active_project.empty()) {
                std::printf("  no active project\n");
                continue;
            }
            auto listed = artifact_store.list_for_project(active_project);
            for (const artifacts::Artifact& artifact : listed.value_or({})) {
                std::printf("  %.8s  %-5s %-26s %s  %s\n", artifact.artifact_id.c_str(),
                            artifact.type.c_str(), artifact.filename.c_str(),
                            artifacts::validation_state_string(artifact.validation_state),
                            artifact.created_at.c_str());
            }
            continue;
        }

        if (line.rfind("validate ", 0) == 0 || line.rfind("verify ", 0) == 0) {
            const std::vector<std::string> args = split_args(line);
            if (args.size() < 2) {
                std::printf("  usage: validate <artifact_id> | verify <artifact_id>\n");
                continue;
            }
            const bool promote = line.rfind("verify", 0) == 0;
            const std::string artifact_id =
                resolve_artifact_id(artifact_store, active_project, args.back());

            auto integrity = artifact_store.verify_integrity(artifact_id);
            if (integrity.is_error() || !integrity.value()) {
                std::printf("  integrity check failed\n");
                continue;
            }
            core::Json arguments = core::Json::object();
            arguments["artifact_id"] = artifact_id;
            arguments["engine"] = "cad";
            arguments["verified"] = promote;
            auto state = application.commands().execute("artifact.validate", arguments);
            if (state.is_error()) {
                std::printf("  %s\n", state.error().message().c_str());
                continue;
            }
            const core::Json* reported = state.value().find("state");
            std::printf("  validation: %s\n",
                        reported != nullptr ? reported->as_string().c_str() : "");
            continue;
        }

        std::printf("  unknown command (try 'help')\n");
    }

    log.info("shell exiting");
    application.shutdown();
    return 0;
}
