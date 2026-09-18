#include "PluginManager.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "../core/FileSystem.hpp"
#include "../core/Logging.hpp"

namespace trinity::plugins {
namespace {

core::ComponentLog log_("plugins");

constexpr const char* kManifestSuffix = ".plugin.json";

bool valid_plugin_id(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (const char c : id) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
                             c == '-' || c == '_';
        if (!allowed) return false;
    }
    return true;
}

bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string json_string(const core::Json& json, const char* key) {
    const core::Json* value = json.find(key);
    return value != nullptr ? value->as_string() : std::string();
}

}  // namespace

// ------------------------------------------------------------- PluginManifest

core::Json PluginManifest::to_json() const {
    core::Json out = core::Json::object();
    out["plugin_id"] = plugin_id;
    out["name"] = name;
    out["version"] = version;
    out["description"] = description;
    out["entry"] = entry;
    out["minimum_trinity"] = minimum_trinity;
    core::Json caps = core::Json::array();
    for (const std::string& capability : capabilities) caps.push_back(core::Json(capability));
    out["capabilities"] = caps;
    out["enabled"] = enabled;
    return out;
}

core::Result<PluginManifest> PluginManifest::from_json(const core::Json& json) {
    if (!json.is_object()) {
        return core::Result<PluginManifest>::fail(core::Error(
            core::ErrorCode::RequestValidationError, "plugin manifest must be a JSON object"));
    }
    PluginManifest manifest;
    manifest.plugin_id = json_string(json, "plugin_id");
    manifest.name = json_string(json, "name");
    manifest.version = json_string(json, "version");
    manifest.description = json_string(json, "description");
    manifest.entry = json_string(json, "entry");
    manifest.minimum_trinity = json_string(json, "minimum_trinity");
    if (const core::Json* enabled = json.find("enabled"); enabled != nullptr && enabled->is_bool()) {
        manifest.enabled = enabled->as_bool();
    }
    if (const core::Json* capabilities = json.find("capabilities");
        capabilities != nullptr && capabilities->is_array()) {
        for (const core::Json& entry : capabilities->as_array()) {
            if (entry.is_string()) manifest.capabilities.push_back(entry.as_string());
        }
    }

    if (!valid_plugin_id(manifest.plugin_id)) {
        return core::Result<PluginManifest>::fail(core::Error(
            core::ErrorCode::RequestValidationError,
            "plugin manifest has a missing or malformed plugin_id (lowercase letters, digits, "
            "'-', '_' and '.' only)"));
    }
    if (manifest.name.empty()) {
        return core::Result<PluginManifest>::fail(core::Error(
            core::ErrorCode::RequestValidationError,
            "plugin manifest for '" + manifest.plugin_id + "' is missing 'name'"));
    }
    if (manifest.version.empty()) {
        return core::Result<PluginManifest>::fail(core::Error(
            core::ErrorCode::RequestValidationError,
            "plugin manifest for '" + manifest.plugin_id + "' is missing 'version'"));
    }
    return core::Result<PluginManifest>::ok(std::move(manifest));
}

// ---------------------------------------------------------------- plugin state

const char* plugin_state_string(PluginState state) {
    switch (state) {
        case PluginState::Discovered: return "discovered";
        case PluginState::Loaded: return "loaded";
        case PluginState::Failed: return "failed";
        case PluginState::Unloaded: return "unloaded";
    }
    return "discovered";
}

core::Json PluginRecord::to_json() const {
    core::Json out = manifest.to_json();
    out["state"] = plugin_state_string(state);
    out["detail"] = detail;
    return out;
}

// -------------------------------------------------------------- ManifestPlugin

ManifestPlugin::ManifestPlugin(PluginManifest manifest) : manifest_(std::move(manifest)) {}

PluginManifest ManifestPlugin::manifest() const { return manifest_; }

core::Status ManifestPlugin::on_load() {
    // Nothing executable to do: declared capabilities are advertised only.
    state_ = PluginState::Loaded;
    return core::Status::ok();
}

void ManifestPlugin::on_unload() { state_ = PluginState::Unloaded; }

core::Json ManifestPlugin::status() const {
    core::Json out = manifest_.to_json();
    out["state"] = plugin_state_string(state_);
    out["executable"] = false;
    return out;
}

// --------------------------------------------------------------- PluginManager

PluginManager::PluginManager(core::EventBus* events) : events_(events) {}

void PluginManager::publish_change(const std::string& plugin_id, PluginState state,
                                   const std::string& detail) {
    if (events_ == nullptr) return;
    core::Json payload = core::Json::object();
    payload["plugin_id"] = plugin_id;
    payload["state"] = plugin_state_string(state);
    payload["detail"] = detail;
    events_->publish(core::topics::kPluginChanged, payload);
}

core::Status PluginManager::register_plugin(std::shared_ptr<IPlugin> plugin) {
    if (!plugin) {
        return core::Status::fail(core::Error(core::ErrorCode::RequestValidationError,
                                             "plugin instance is required"));
    }
    const PluginManifest manifest = plugin->manifest();
    if (manifest.plugin_id.empty()) {
        return core::Status::fail(core::Error(core::ErrorCode::RequestValidationError,
                                             "plugin manifest is missing plugin_id"));
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (plugins_.count(manifest.plugin_id) > 0) {
        return core::Status::fail(core::Error(
            core::ErrorCode::RequestValidationError,
            "plugin '" + manifest.plugin_id + "' is already registered"));
    }
    PluginRecord record;
    record.manifest = manifest;
    record.state = PluginState::Discovered;
    record.detail = "registered";
    records_[manifest.plugin_id] = record;
    plugins_[manifest.plugin_id] = std::move(plugin);
    return core::Status::ok();
}

core::Result<std::size_t> PluginManager::discover(const std::string& directory) {
    if (directory.empty()) return core::Result<std::size_t>::ok(0);

    std::error_code ec;
    if (!std::filesystem::exists(core::FileSystem::normalise(directory), ec) || ec) {
        return core::Result<std::size_t>::ok(0);  // nothing to discover
    }

    std::size_t registered = 0;
    for (const std::string& filename : core::FileSystem::list_files(directory)) {
        if (!ends_with(filename, kManifestSuffix)) continue;
        const std::string path = directory + "/" + filename;
        auto contents = core::FileSystem::read_file(path);
        if (!contents.has_value()) {
            log_.warning("plugin manifest could not be read", [&] {
                core::Json ctx = core::Json::object();
                ctx["path"] = path;
                return ctx;
            }());
            continue;
        }
        core::Json document;
        try {
            document = core::Json::parse(*contents);
        } catch (const std::exception& exception) {
            log_.warning("plugin manifest is not valid JSON", [&] {
                core::Json ctx = core::Json::object();
                ctx["path"] = path;
                ctx["what"] = std::string(exception.what());
                return ctx;
            }());
            continue;
        }
        auto manifest = PluginManifest::from_json(document);
        if (manifest.is_error()) {
            log_.warning("plugin manifest rejected", [&] {
                core::Json ctx = core::Json::object();
                ctx["path"] = path;
                ctx["error"] = manifest.error().to_json();
                return ctx;
            }());
            continue;
        }
        auto plugin = std::make_shared<ManifestPlugin>(manifest.value());
        const core::Status status = register_plugin(plugin);
        if (status.is_ok()) {
            ++registered;
        } else {
            log_.warning("plugin not registered", [&] {
                core::Json ctx = core::Json::object();
                ctx["path"] = path;
                ctx["error"] = status.error().to_json();
                return ctx;
            }());
        }
    }
    return core::Result<std::size_t>::ok(registered);
}

core::Status PluginManager::load(const std::string& plugin_id) {
    std::shared_ptr<IPlugin> plugin;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = plugins_.find(plugin_id);
        if (it == plugins_.end()) {
            return core::Status::fail(core::Error(
                core::ErrorCode::RequestValidationError, "no plugin registered as '" + plugin_id + "'"));
        }
        plugin = it->second;
    }

    const core::Status status = plugin->on_load();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto record_it = records_.find(plugin_id);
        if (record_it != records_.end()) {
            if (status.is_ok()) {
                record_it->second.state = PluginState::Loaded;
                record_it->second.detail = "loaded";
            } else {
                record_it->second.state = PluginState::Failed;
                record_it->second.detail = status.error().message();
            }
        }
    }
    publish_change(plugin_id, status.is_ok() ? PluginState::Loaded : PluginState::Failed,
                   status.is_ok() ? std::string("loaded") : status.error().message());
    if (!status.is_ok()) return status;
    log_.info("plugin loaded", [&] {
        core::Json ctx = core::Json::object();
        ctx["plugin_id"] = plugin_id;
        return ctx;
    }());
    return core::Status::ok();
}

core::Status PluginManager::unload(const std::string& plugin_id) {
    std::shared_ptr<IPlugin> plugin;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = plugins_.find(plugin_id);
        if (it != plugins_.end()) {
            plugin = it->second;
            found = true;
        }
    }
    if (!found) {
        return core::Status::fail(core::Error(
            core::ErrorCode::RequestValidationError, "no plugin registered as '" + plugin_id + "'"));
    }
    if (plugin) plugin->on_unload();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto record_it = records_.find(plugin_id);
        if (record_it != records_.end()) {
            record_it->second.state = PluginState::Unloaded;
            record_it->second.detail = "unloaded";
        }
    }
    publish_change(plugin_id, PluginState::Unloaded, "unloaded");
    return core::Status::ok();
}

core::Result<std::size_t> PluginManager::load_all() {
    std::vector<std::string> pending;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : records_) {
            if (entry.second.manifest.enabled && entry.second.state != PluginState::Loaded) {
                pending.push_back(entry.first);
            }
        }
    }

    std::size_t loaded = 0;
    for (const std::string& plugin_id : pending) {
        if (load(plugin_id).is_ok()) ++loaded;
    }
    return core::Result<std::size_t>::ok(loaded);
}

void PluginManager::unload_all() {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : records_) ids.push_back(entry.first);
    }
    for (const std::string& plugin_id : ids) {
        std::shared_ptr<IPlugin> plugin;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = plugins_.find(plugin_id);
            if (it != plugins_.end()) plugin = it->second;
            auto record_it = records_.find(plugin_id);
            if (record_it != records_.end()) {
                record_it->second.state = PluginState::Unloaded;
                record_it->second.detail = "unloaded";
            }
        }
        if (plugin) plugin->on_unload();
    }
}

core::Result<PluginRecord> PluginManager::get(const std::string& plugin_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = records_.find(plugin_id);
    if (it == records_.end()) {
        return core::Result<PluginRecord>::fail(core::Error(
            core::ErrorCode::RequestValidationError, "no plugin registered as '" + plugin_id + "'"));
    }
    return core::Result<PluginRecord>::ok(it->second);
}

std::vector<PluginRecord> PluginManager::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PluginRecord> out;
    out.reserve(records_.size());
    for (const auto& entry : records_) out.push_back(entry.second);
    return out;
}

std::size_t PluginManager::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_.size();
}

std::size_t PluginManager::loaded_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t loaded = 0;
    for (const auto& entry : records_) {
        if (entry.second.state == PluginState::Loaded) ++loaded;
    }
    return loaded;
}

core::Json PluginManager::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    core::Json out = core::Json::object();
    core::Json items = core::Json::array();
    std::size_t loaded = 0;
    for (const auto& entry : records_) {
        items.push_back(entry.second.to_json());
        if (entry.second.state == PluginState::Loaded) ++loaded;
    }
    out["count"] = static_cast<double>(records_.size());
    out["loaded"] = static_cast<double>(loaded);
    out["plugins"] = items;
    return out;
}

}  // namespace trinity::plugins
