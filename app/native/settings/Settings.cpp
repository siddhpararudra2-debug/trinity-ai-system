#include "Settings.hpp"

#include "../core/Time.hpp"

namespace trinity::settings {

const SettingDescriptor kDescriptors[] = {
    // General
    {"general.auto_save", "General", "Auto-save projects", "true"},
    {"general.startup_view", "General", "Startup view", "workspace"},
    // Appearance
    {"appearance.theme", "Appearance", "Theme", "engineering-dark"},
    {"appearance.font_size", "Appearance", "UI font size", "13"},
    // Workspace
    {"workspace.root", "Workspace", "Projects root directory", ""},
    {"workspace.open_last_project", "Workspace", "Reopen last project on start", "true"},
    // Engines
    {"engines.parallel_workers", "Engines", "Job worker threads", "0"},
    {"engines.cad_adapter", "Engines", "Preferred CAD adapter", "mesh_kernel"},
    // CAD
    {"cad.default_units", "CAD", "Default units", "mm"},
    {"cad.min_printable_feature_mm", "CAD", "FDM minimum feature (mm)", "1.0"},
    // Rendering
    {"rendering.antialias", "Rendering", "Viewport antialiasing", "true"},
    {"rendering.grid_visible", "Rendering", "Show grid", "true"},
    // Performance
    {"performance.cache_math", "Performance", "Cache deterministic math results", "true"},
    {"performance.log_ring_size", "Performance", "In-memory log ring size", "2048"},
    // Python
    {"python.enabled", "Python", "Enable Python engine host", "true"},
    {"python.interpreter", "Python", "Python interpreter path", ""},
    // Plugins
    {"plugins.enabled", "Plugins", "Enable plugin loading", "false"},
    {"plugins.directories", "Plugins", "Plugin search paths", ""},
    // Model Provider (configuration surface only — no network code ships)
    {"model.provider", "Model Provider", "Provider implementation", "null"},
    {"model.endpoint", "Model Provider", "Endpoint URL (unused until a provider is connected)", ""},
    {"model.name", "Model Provider", "Model name", ""},
    {"model.api_key_ref", "Model Provider", "Key reference (stored in OS credential store; never in DB)", ""},
    // Logging
    {"logging.level", "Logging", "Log level", "INFO"},
    {"logging.file_enabled", "Logging", "Write log file", "true"},
    // Updates
    {"updates.check_enabled", "Updates", "Check for updates", "false"},
};
const int kDescriptorCount =
    static_cast<int>(sizeof(kDescriptors) / sizeof(kDescriptors[0]));

SettingsStore::SettingsStore(db::Database& db) : db_(&db) {}

core::Status SettingsStore::seed_defaults() {
    const std::string now = core::iso_utc_now();
    for (int i = 0; i < kDescriptorCount; ++i) {
        const SettingDescriptor& descriptor = kDescriptors[i];
        auto existing = db_->query_one("SELECT value FROM settings WHERE key = ?;",
                                       {core::Json(descriptor.key)});
        if (existing.is_error()) return core::Status::fail(existing.take_error());
        if (existing.value().has_value()) continue;
        if (auto insert = db_->run(
                "INSERT INTO settings (key, value, updated_at) VALUES (?, ?, ?);",
                {core::Json(descriptor.key), core::Json(descriptor.default_value),
                 core::Json(now)});
            insert.is_error()) {
            return insert;
        }
    }
    return core::Status::ok();
}

core::Result<const SettingDescriptor*> SettingsStore::descriptor_for(
    const std::string& key) const {
    for (int i = 0; i < kDescriptorCount; ++i) {
        if (key == kDescriptors[i].key) return core::Result<const SettingDescriptor*>::ok(&kDescriptors[i]);
    }
    return core::Result<const SettingDescriptor*>::fail(core::Error(
        core::ErrorCode::RequestValidationError, "unknown setting key '" + key + "'"));
}

core::Result<std::string> SettingsStore::get(const std::string& key) const {
    auto descriptor = descriptor_for(key);
    if (descriptor.is_error()) return core::Result<std::string>::fail(descriptor.take_error());
    auto row = db_->query_one("SELECT value FROM settings WHERE key = ?;", {core::Json(key)});
    if (row.is_error()) return core::Result<std::string>::fail(row.take_error());
    if (!row.value().has_value()) {
        return core::Result<std::string>::ok(std::string(descriptor.value()->default_value));
    }
    return core::Result<std::string>::ok(row.value()->at("value").as_string());
}

core::Status SettingsStore::set(const std::string& key, const std::string& value) {
    auto descriptor = descriptor_for(key);
    if (descriptor.is_error()) return core::Status::fail(descriptor.take_error());
    auto existing = db_->query_one("SELECT value FROM settings WHERE key = ?;", {core::Json(key)});
    if (existing.is_error()) return core::Status::fail(existing.take_error());
    if (existing.value().has_value()) {
        return db_->run("UPDATE settings SET value = ?, updated_at = ? WHERE key = ?;",
                        {core::Json(value), core::Json(core::iso_utc_now()), core::Json(key)});
    }
    return db_->run("INSERT INTO settings (key, value, updated_at) VALUES (?, ?, ?);",
                    {core::Json(key), core::Json(value), core::Json(core::iso_utc_now())});
}

core::Result<std::string> SettingsStore::workspace_root() const { return get("workspace.root"); }

core::Status SettingsStore::set_workspace_root(const std::string& path) {
    return set("workspace.root", path);
}

core::Status SettingsStore::set_log_level(const std::string& level) {
    return set("logging.level", level);
}

core::Result<std::string> SettingsStore::log_level() const { return get("logging.level"); }

core::Json SettingsStore::to_json() const {
    core::Json out = core::Json::object();
    for (int i = 0; i < kDescriptorCount; ++i) {
        const SettingDescriptor& descriptor = kDescriptors[i];
        if (!out.contains(descriptor.category)) {
            out[descriptor.category] = core::Json::array();
        }
        core::Json entry = core::Json::object();
        entry["key"] = descriptor.key;
        entry["label"] = descriptor.label;
        auto value = get(descriptor.key);
        entry["value"] = value.is_ok() ? value.value() : std::string(descriptor.default_value);
        entry["default"] = descriptor.default_value;
        out[descriptor.category].push_back(entry);
    }
    return out;
}

}  // namespace trinity::settings
