#include "Paths.hpp"

#include <cstdlib>

#include "FileSystem.hpp"

namespace trinity::core {
namespace {

std::string join_path(const std::string& base, const std::string& child) {
    if (base.empty()) return child;
    const char last = base.back();
    if (last == '/' || last == '\\') return base + child;
    return base + "/" + child;
}

std::optional<std::string> env_value(const std::string& name) {
#if defined(_WIN32)
    // getenv_s writes the required buffer size through this pointer, so it must
    // not be const (MSVC rejects a const size_t*).
    std::size_t required = 0;
    if (::getenv_s(&required, nullptr, 0, name.c_str()) != 0 || required == 0) {
        return std::nullopt;
    }
    std::string value(required, '\0');
    if (::getenv_s(&required, value.data(), required, name.c_str()) != 0) {
        return std::nullopt;
    }
    value.resize(required - 1 < value.size() ? required - 1 : 0);
    return value;
#else
    const char* value = std::getenv(name.c_str());
    if (value == nullptr) return std::nullopt;
    return std::string(value);
#endif
}

}  // namespace

std::optional<std::string> Paths::env(const std::string& name) { return env_value(name); }

std::string Paths::data_dir() {
    if (auto override_dir = env_value("TRINITY_DATA_DIR"); override_dir && !override_dir->empty()) {
        return *override_dir;
    }
    if (auto local = env_value("LOCALAPPDATA"); local && !local->empty()) {
        return join_path(*local, "Trinity");
    }
    // POSIX fallback (development/CI): respect XDG, then HOME.
    if (auto xdg = env_value("XDG_DATA_HOME"); xdg && !xdg->empty()) {
        return join_path(*xdg, "Trinity");
    }
    if (auto home = env_value("HOME"); home && !home->empty()) {
        return join_path(*home, ".local/share/Trinity");
    }
    return std::string("TrinityData");
}

std::string Paths::projects_root() {
    if (auto override_dir = env_value("TRINITY_PROJECTS_DIR");
        override_dir && !override_dir->empty()) {
        return *override_dir;
    }
    if (auto documents = env_value("TRINITY_DOCUMENTS"); documents && !documents->empty()) {
        return join_path(*documents, "Trinity Projects");
    }
    if (auto user_profile = env_value("USERPROFILE"); user_profile && !user_profile->empty()) {
        return join_path(*user_profile, "Documents/Trinity Projects");
    }
    if (auto home = env_value("HOME"); home && !home->empty()) {
        return join_path(*home, "Documents/Trinity Projects");
    }
    return std::string("Trinity Projects");
}

std::string Paths::database_file() { return join_path(data_dir(), "trinity.db"); }
std::string Paths::artifacts_dir() { return join_path(data_dir(), "artifacts"); }
std::string Paths::logs_dir() { return join_path(data_dir(), "logs"); }
std::string Paths::main_log_file() { return join_path(logs_dir(), "trinity.log"); }
std::string Paths::temp_dir() { return join_path(data_dir(), "tmp"); }
std::string Paths::settings_file() { return join_path(data_dir(), "settings.json"); }

bool Paths::ensure_layout() {
    bool ok = true;
    for (const std::string dir : {data_dir(), artifacts_dir(), logs_dir(), temp_dir(),
                                  projects_root()}) {
        std::error_code ec;
        if (!FileSystem::ensure_directory(dir, ec)) ok = false;
    }
    return ok;
}

}  // namespace trinity::core
