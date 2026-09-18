// Trinity — Settings (brief §Settings).
// Categories: General, Appearance, Workspace, Engines, CAD, Rendering,
// Performance, Python, Plugins, Model Provider, Logging, Updates.
//
// The Model Provider category stores configuration surface only (endpoint,
// model name, keys placeholder) — no network code exists in this build.
// Settings persist to SQLite (settings table) with a JSON value per key.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "../core/Json.hpp"
#include "../db/Database.hpp"

namespace trinity::settings {

struct SettingDescriptor {
    const char* key;
    const char* category;
    const char* label;
    const char* default_value;
};

extern const SettingDescriptor kDescriptors[];
extern const int kDescriptorCount;

class SettingsStore {
public:
    explicit SettingsStore(db::Database& db);

    // Applies defaults for any missing keys at startup.
    core::Status seed_defaults();

    core::Result<std::string> get(const std::string& key) const;
    core::Status set(const std::string& key, const std::string& value);

    // Typed helpers for common settings.
    core::Result<std::string> workspace_root() const;
    core::Status set_workspace_root(const std::string& path);

    core::Status set_log_level(const std::string& level);
    core::Result<std::string> log_level() const;

    // All settings grouped by category for the settings UI.
    core::Json to_json() const;

private:
    core::Result<const SettingDescriptor*> descriptor_for(const std::string& key) const;

    db::Database* db_;
};

}  // namespace trinity::settings
