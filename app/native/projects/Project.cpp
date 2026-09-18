#include "Project.hpp"

#include <algorithm>

#include "../core/FileSystem.hpp"
#include "../core/Logging.hpp"
#include "../core/Time.hpp"
#include "../core/Uuid.hpp"

namespace trinity::projects {
namespace {
core::ComponentLog log_("projects");
}

const char* const kStandardFolders[] = {"cad",    "pcb",       "calculations", "simulation",
                                        "firmware", "research", "artifacts",    "logs"};
const int kStandardFolderCount =
    static_cast<int>(sizeof(kStandardFolders) / sizeof(kStandardFolders[0]));

core::Json Project::to_json() const {
    core::Json out = core::Json::object();
    out["project_id"] = project_id;
    out["name"] = name;
    out["description"] = description;
    out["workspace"] = workspace;
    out["archived"] = archived;
    out["created_at"] = created_at;
    out["updated_at"] = updated_at;
    return out;
}

Project Project::from_json(const core::Json& json) {
    Project project;
    if (const core::Json* v = json.find("project_id")) project.project_id = v->as_string();
    if (const core::Json* v = json.find("name")) project.name = v->as_string();
    if (const core::Json* v = json.find("description")) project.description = v->as_string();
    if (const core::Json* v = json.find("workspace")) project.workspace = v->as_string();
    if (const core::Json* v = json.find("archived")) project.archived = v->as_bool();
    if (const core::Json* v = json.find("created_at")) project.created_at = v->as_string();
    if (const core::Json* v = json.find("updated_at")) project.updated_at = v->as_string();
    return project;
}

ProjectStore::ProjectStore(db::Database& db, std::string projects_root)
    : db_(&db), projects_root_(std::move(projects_root)) {}

std::string ProjectStore::workspace_for(const std::string& name) const {
    // Sanitise the folder name: keep alphanumerics, dash, underscore, space.
    std::string safe;
    safe.reserve(name.size());
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ') {
            safe.push_back(c);
        }
    }
    if (safe.empty()) safe = "project";
    // Ensure uniqueness inside the projects root.
    std::string candidate = core::FileSystem::normalise(projects_root_ + "/" + safe).generic_string();
    const std::string base = candidate;
    int suffix = 2;
    while (core::FileSystem::file_size(candidate) != 0 ||
           !core::FileSystem::list_files(candidate).empty()) {
        // file_size returns 0 for directories/missing; probe with ensure op instead
        std::error_code probe_ec;
        if (!std::filesystem::exists(base + "-" + std::to_string(suffix), probe_ec)) break;
        candidate = base + "-" + std::to_string(suffix);
        ++suffix;
    }
    return candidate;
}

core::Result<Project> ProjectStore::create(const std::string& name,
                                           const std::string& description,
                                           const std::string& explicit_workspace) {
    if (name.empty()) {
        return core::Result<Project>::fail(
            core::Error(core::ErrorCode::RequestValidationError, "project name is required"));
    }

    Project project;
    project.project_id = core::new_uuid();
    project.name = name;
    project.description = description;
    project.workspace =
        explicit_workspace.empty() ? workspace_for(name)
                                   : core::FileSystem::normalise(explicit_workspace).generic_string();
    project.created_at = core::iso_utc_now();
    project.updated_at = project.created_at;

    std::error_code ec;
    if (!core::FileSystem::ensure_directory(project.workspace, ec)) {
        return core::Result<Project>::fail(core::Error(
            core::ErrorCode::PathValidationError,
            "cannot create project workspace: " + project.workspace + " (" + ec.message() + ")"));
    }
    for (int i = 0; i < kStandardFolderCount; ++i) {
        const std::string sub = project.workspace + "/" + kStandardFolders[i];
        if (!core::FileSystem::ensure_directory(sub, ec)) {
            return core::Result<Project>::fail(core::Error(
                core::ErrorCode::PathValidationError,
                std::string("cannot create project folder '") + kStandardFolders[i] + "'"));
        }
    }

    if (auto run = db_->run(
            "INSERT INTO projects (project_id, name, description, workspace, archived, "
            "created_at, updated_at) VALUES (?, ?, ?, ?, 0, ?, ?);",
            {core::Json(project.project_id), core::Json(project.name),
             core::Json(project.description), core::Json(project.workspace),
             core::Json(project.created_at), core::Json(project.updated_at)});
        run.is_error()) {
        return core::Result<Project>::fail(run.take_error());
    }

    log_.info("project created", [&] {
        core::Json ctx = core::Json::object();
        ctx["project_id"] = project.project_id;
        ctx["workspace"] = project.workspace;
        return ctx;
    }());
    return core::Result<Project>::ok(std::move(project));
}

core::Result<Project> ProjectStore::row_to_project(const core::JsonObject& row) const {
    Project project;
    const auto get = [&row](const char* key) -> core::Json {
        const auto it = row.find(key);
        return it != row.end() ? it->second : core::Json(nullptr);
    };
    project.project_id = get("project_id").as_string();
    project.name = get("name").as_string();
    project.description = get("description").as_string();
    project.workspace = get("workspace").as_string();
    project.archived = get("archived").as_int() != 0;
    project.created_at = get("created_at").as_string();
    project.updated_at = get("updated_at").as_string();
    if (project.project_id.empty()) {
        return core::Result<Project>::fail(
            core::Error(core::ErrorCode::RequestValidationError, "project row malformed"));
    }
    return core::Result<Project>::ok(std::move(project));
}

core::Result<Project> ProjectStore::get(const std::string& project_id) const {
    auto row = db_->query_one("SELECT * FROM projects WHERE project_id = ?;",
                              {core::Json(project_id)});
    if (row.is_error()) return core::Result<Project>::fail(row.take_error());
    if (!row.value().has_value()) {
        return core::Result<Project>::fail(core::Error(
            core::ErrorCode::RequestValidationError, "no project with id '" + project_id + "'"));
    }
    return row_to_project(*row.value());
}

core::Result<std::vector<Project>> ProjectStore::list(bool include_archived) const {
    auto rows = db_->query(include_archived
                               ? "SELECT * FROM projects ORDER BY created_at DESC;"
                               : "SELECT * FROM projects WHERE archived = 0 ORDER BY created_at DESC;");
    if (rows.is_error()) return core::Result<std::vector<Project>>::fail(rows.take_error());

    std::vector<Project> out;
    out.reserve(rows.value().size());
    // Column order from schema: project_id, name, description, workspace,
    // archived, created_at, updated_at.
    for (const auto& r : rows.value()) {
        core::JsonObject obj;
        const char* columns[] = {"project_id", "name",      "description", "workspace",
                                 "archived",   "created_at", "updated_at"};
        for (int i = 0; i < 7; ++i) obj[columns[i]] = r[static_cast<std::size_t>(i)];
        auto project = row_to_project(obj);
        if (project.is_ok()) out.push_back(project.take_value());
    }
    return core::Result<std::vector<Project>>::ok(std::move(out));
}

core::Result<Project> ProjectStore::rename(const std::string& project_id,
                                           const std::string& new_name) {
    if (new_name.empty()) {
        return core::Result<Project>::fail(
            core::Error(core::ErrorCode::RequestValidationError, "new name is required"));
    }
    if (auto run = db_->run("UPDATE projects SET name = ?, updated_at = ? WHERE project_id = ?;",
                            {core::Json(new_name), core::Json(core::iso_utc_now()),
                             core::Json(project_id)});
        run.is_error()) {
        return core::Result<Project>::fail(run.take_error());
    }
    return get(project_id);
}

core::Result<Project> ProjectStore::set_description(const std::string& project_id,
                                                    const std::string& description) {
    if (auto run = db_->run(
            "UPDATE projects SET description = ?, updated_at = ? WHERE project_id = ?;",
            {core::Json(description), core::Json(core::iso_utc_now()), core::Json(project_id)});
        run.is_error()) {
        return core::Result<Project>::fail(run.take_error());
    }
    return get(project_id);
}

core::Result<Project> ProjectStore::archive(const std::string& project_id, bool archived) {
    if (auto run = db_->run(
            "UPDATE projects SET archived = ?, updated_at = ? WHERE project_id = ?;",
            {core::Json(archived ? 1 : 0), core::Json(core::iso_utc_now()),
             core::Json(project_id)});
        run.is_error()) {
        return core::Result<Project>::fail(run.take_error());
    }
    return get(project_id);
}

core::Result<Project> ProjectStore::duplicate(const std::string& project_id,
                                              const std::string& new_name) {
    auto source = get(project_id);
    if (source.is_error()) return source;
    auto created = create(new_name.empty() ? source.value().name + " (copy)" : new_name,
                          source.value().description);
    if (created.is_error()) return created;
    // Copy artifact and log material that lives inside the workspace.
    std::error_code ec;
    std::filesystem::copy(source.value().workspace, created.value().workspace,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::skip_existing,
                          ec);
    if (ec) {
        log_.warning("project duplicate: partial folder copy",
                     [&] {
                         core::Json ctx = core::Json::object();
                         ctx["error"] = ec.message();
                         return ctx;
                     }());
    }
    return created;
}

core::Result<Project> ProjectStore::import_from_folder(const std::string& source_folder,
                                                       const std::string& new_name) {
    std::error_code ec;
    const std::filesystem::path source = core::FileSystem::normalise(source_folder);
    if (!std::filesystem::is_directory(source, ec) || ec) {
        return core::Result<Project>::fail(core::Error(
            core::ErrorCode::PathValidationError, "source folder does not exist: " + source_folder));
    }
    const std::string name =
        new_name.empty() ? source.filename().generic_string() : new_name;
    return create(name, "imported from " + source.generic_string());
}

core::Status ProjectStore::export_to_folder(const std::string& project_id,
                                            const std::string& destination_folder) {
    auto project = get(project_id);
    if (project.is_error()) return core::Status::fail(project.take_error());
    std::error_code ec;
    const std::filesystem::path destination = core::FileSystem::normalise(destination_folder);
    if (destination.filename().empty()) {
        return core::Status::fail(core::Error(core::ErrorCode::PathValidationError,
                                              "destination must be a folder path"));
    }
    std::filesystem::create_directories(destination, ec);
    if (ec) {
        return core::Status::fail(
            core::Error(core::ErrorCode::PathValidationError, "cannot create destination"));
    }
    const std::filesystem::path target = destination / project.value().name;
    std::filesystem::copy(project.value().workspace, target,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);
    if (ec) {
        return core::Status::fail(core::Error(core::ErrorCode::PathValidationError,
                                              "copy failed: " + ec.message()));
    }
    return core::Status::ok();
}

}  // namespace trinity::projects
