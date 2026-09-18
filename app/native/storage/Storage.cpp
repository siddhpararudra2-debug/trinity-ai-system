#include "Storage.hpp"

#include <algorithm>
#include <system_error>

#include "../core/FileSystem.hpp"
#include "../core/Logging.hpp"

namespace trinity::storage {
namespace {

core::ComponentLog log_("storage");

std::string join(const std::string& base, const std::string& child) {
    if (base.empty()) return child;
    const char last = base.back();
    if (last == '/' || last == '\\') return base + child;
    return base + "/" + child;
}

std::string env_or_empty(const platform::IEnvironment& environment, const char* name) {
    auto value = environment.get(name);
    return value ? *value : std::string();
}

}  // namespace

// ------------------------------------------------------------- LocalFileStorage

LocalFileStorage::LocalFileStorage(std::string root) : root_(core::FileSystem::normalise(root).string()) {
    std::error_code ec;
    core::FileSystem::ensure_directory(root_, ec);
    if (ec) {
        log_.warning("storage root could not be created", [&] {
            core::Json ctx = core::Json::object();
            ctx["root"] = root_;
            ctx["error"] = ec.message();
            return ctx;
        }());
    }
}

std::optional<std::filesystem::path> LocalFileStorage::resolve(const std::string& relative) const {
    if (relative.empty()) return std::nullopt;
    auto validated = core::FileSystem::validate_under(root_, join(root_, relative));
    if (!validated.has_value()) return std::nullopt;
    return validated;
}

std::string LocalFileStorage::absolute(const std::string& relative) const {
    auto resolved = resolve(relative);
    return resolved.has_value() ? resolved->string() : std::string();
}

bool LocalFileStorage::exists(const std::string& relative) const {
    auto resolved = resolve(relative);
    if (!resolved.has_value()) return false;
    std::error_code ec;
    return std::filesystem::exists(*resolved, ec) && !ec;
}

bool LocalFileStorage::ensure_directory(const std::string& relative, std::error_code& ec) {
    auto resolved = resolve(relative);
    if (!resolved.has_value()) {
        ec = std::make_error_code(std::errc::permission_denied);
        return false;
    }
    return core::FileSystem::ensure_directory(resolved->string(), ec);
}

std::optional<std::string> LocalFileStorage::read_text(const std::string& relative) const {
    auto resolved = resolve(relative);
    if (!resolved.has_value()) return std::nullopt;
    return core::FileSystem::read_file(resolved->string());
}

bool LocalFileStorage::write_text_atomic(const std::string& relative, std::string_view content,
                                         std::error_code& ec) {
    auto resolved = resolve(relative);
    if (!resolved.has_value()) {
        ec = std::make_error_code(std::errc::permission_denied);
        return false;
    }
    return core::FileSystem::write_file_atomic(resolved->string(), content, ec);
}

bool LocalFileStorage::copy_in(const std::string& source_file, const std::string& relative_destination,
                               std::error_code& ec) {
    auto resolved = resolve(relative_destination);
    if (!resolved.has_value()) {
        ec = std::make_error_code(std::errc::permission_denied);
        return false;
    }
    if (!core::FileSystem::ensure_parent_directories(resolved->string(), ec) || ec) return false;
    std::filesystem::copy_file(core::FileSystem::normalise(source_file), *resolved,
                               std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

bool LocalFileStorage::remove_all(const std::string& relative, std::error_code& ec) {
    auto resolved = resolve(relative);
    if (!resolved.has_value()) {
        ec = std::make_error_code(std::errc::permission_denied);
        return false;
    }
    return core::FileSystem::remove_all(resolved->string(), ec);
}

std::vector<std::string> LocalFileStorage::list_files(const std::string& relative_directory) const {
    auto resolved = resolve(relative_directory);
    if (!resolved.has_value()) return {};
    return core::FileSystem::list_files(resolved->string());
}

std::vector<std::string> LocalFileStorage::list_directories(
    const std::string& relative_directory) const {
    std::vector<std::string> out;
    auto resolved = resolve(relative_directory);
    if (!resolved.has_value()) return out;
    std::error_code ec;
    std::filesystem::directory_iterator iterator(*resolved, ec);
    if (ec) return out;
    for (const std::filesystem::directory_entry& entry : iterator) {
        std::error_code entry_ec;
        if (entry.is_directory(entry_ec) && !entry_ec) {
            out.push_back(entry.path().filename().string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::uintmax_t LocalFileStorage::size(const std::string& relative) const {
    auto resolved = resolve(relative);
    if (!resolved.has_value()) return 0;
    return core::FileSystem::file_size(resolved->string());
}

// --------------------------------------------------------------- StorageLayout

StorageLayout StorageLayout::resolve(const platform::IEnvironment& environment,
                                    const std::string& executable_dir) {
    return resolve_with_overrides(environment, std::string(), std::string(), executable_dir);
}

StorageLayout StorageLayout::resolve_with_overrides(const platform::IEnvironment& environment,
                                                    const std::string& data_dir_override,
                                                    const std::string& projects_dir_override,
                                                    const std::string& executable_dir) {
    StorageLayout layout;

    // 1. Data directory: explicit override, then TRINITY_DATA_DIR, then the
    //    platform convention (never a hardcoded user path).
    if (!data_dir_override.empty()) {
        layout.data_dir = data_dir_override;
    } else if (const std::string configured = env_or_empty(environment, "TRINITY_DATA_DIR");
               !configured.empty()) {
        layout.data_dir = configured;
    } else if (const std::string local = env_or_empty(environment, "LOCALAPPDATA"); !local.empty()) {
        layout.data_dir = join(local, "Trinity");
    } else if (const std::string xdg = env_or_empty(environment, "XDG_DATA_HOME"); !xdg.empty()) {
        layout.data_dir = join(xdg, "Trinity");
    } else if (const std::string home = environment.home_directory(); !home.empty()) {
        layout.data_dir = join(home, ".local/share/Trinity");
    } else {
        layout.data_dir = "TrinityData";
    }

    // 2. Projects directory: explicit override, TRINITY_PROJECTS_DIR, else
    //    <documents>/Trinity Projects.
    if (!projects_dir_override.empty()) {
        layout.projects_dir = projects_dir_override;
    } else if (const std::string configured = env_or_empty(environment, "TRINITY_PROJECTS_DIR");
               !configured.empty()) {
        layout.projects_dir = configured;
    } else {
        std::string documents = env_or_empty(environment, "TRINITY_DOCUMENTS");
        if (documents.empty()) {
            const std::string home = environment.home_directory();
            if (!home.empty()) documents = join(home, "Documents");
        }
        layout.projects_dir =
            documents.empty() ? "Trinity Projects" : join(documents, "Trinity Projects");
    }

    layout.projects_dir = core::FileSystem::normalise(layout.projects_dir).string();

    // 3. Fixed sub-directories of the data directory.
    layout.artifacts_dir = join(layout.data_dir, "artifacts");
    layout.logs_dir = join(layout.data_dir, "logs");
    layout.temp_dir = join(layout.data_dir, "tmp");
    layout.plugins_dir = join(layout.data_dir, "plugins");
    layout.database_file = join(layout.data_dir, "trinity.db");
    layout.log_file = join(layout.logs_dir, "trinity.log");

    // 4. Bundled Python engine host (packaged next to the executable).
    if (!executable_dir.empty()) {
        const std::string candidate = join(join(executable_dir, "python"), "services/engine_host.py");
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && !ec) {
            layout.engine_host_script = core::FileSystem::normalise(candidate).string();
        }
    }
    return layout;
}

bool StorageLayout::ensure_directories(std::error_code& ec) const {
    for (const std::string& directory :
         {data_dir, artifacts_dir, logs_dir, temp_dir, plugins_dir, projects_dir}) {
        if (directory.empty()) continue;
        if (!core::FileSystem::ensure_directory(directory, ec) || ec) return false;
    }
    return true;
}

core::Json StorageLayout::to_json() const {
    core::Json out = core::Json::object();
    out["data_dir"] = data_dir;
    out["projects_dir"] = projects_dir;
    out["artifacts_dir"] = artifacts_dir;
    out["logs_dir"] = logs_dir;
    out["temp_dir"] = temp_dir;
    out["plugins_dir"] = plugins_dir;
    out["database_file"] = database_file;
    out["log_file"] = log_file;
    out["engine_host_script"] = engine_host_script;
    return out;
}

}  // namespace trinity::storage
