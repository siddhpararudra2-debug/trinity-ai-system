// Trinity — plugin system (brief §Plugin manager, §IPlugin).
//
// Scope of this phase, stated plainly:
//
//   * Plugins are described by a validated JSON manifest
//     ("<name>.plugin.json") discovered in the configured plugin directories.
//   * In-process (native) plugins implement IPlugin and are registered by the
//     host or by built-in modules.
//   * This build does NOT load third-party binaries or scripts. Loading
//     untrusted code is a trust decision that belongs behind code signing and
//     the security roadmap; pretending otherwise would be a security hole, so
//     manifest plugins are registered as descriptors and never executed.
//
// Every state change is published on the event bus ("plugin.changed") and the
// plugin table is queryable by the Settings → Plugins panel.
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../core/Error.hpp"
#include "../core/EventBus.hpp"
#include "../core/Json.hpp"

namespace trinity::plugins {

struct PluginManifest {
    std::string plugin_id;             // required, e.g. "trinity.onshape-adapter"
    std::string name;                  // required, human readable
    std::string version;               // required, e.g. "1.0.0"
    std::string description;
    std::string entry;                 // informational: relative entry artifact
    std::string minimum_trinity;       // informational: host version requirement
    std::vector<std::string> capabilities;
    bool enabled = true;

    core::Json to_json() const;

    // Parses and validates a manifest document. Fails with
    // request_validation_error when a required field is missing or malformed.
    static core::Result<PluginManifest> from_json(const core::Json& json);
};

enum class PluginState { Discovered, Loaded, Failed, Unloaded };

const char* plugin_state_string(PluginState state);

// Canonical plugin interface (brief §Core interfaces: IPlugin).
class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual PluginManifest manifest() const = 0;

    // Called once when the plugin is loaded. Return a failure to be recorded
    // as a failed plugin (the application keeps running).
    virtual core::Status on_load() = 0;

    // Called on unload/shutdown. Must not throw.
    virtual void on_unload() = 0;

    virtual core::Json status() const = 0;
};

// A plugin whose behavior is entirely described by its manifest (capability
// advertisement, no executable contribution). This is what directory discovery
// produces today.
class ManifestPlugin : public IPlugin {
public:
    explicit ManifestPlugin(PluginManifest manifest);

    PluginManifest manifest() const override;
    core::Status on_load() override;
    void on_unload() override;
    core::Json status() const override;

private:
    PluginManifest manifest_;
    PluginState state_ = PluginState::Discovered;
};

struct PluginRecord {
    PluginManifest manifest;
    PluginState state = PluginState::Discovered;
    std::string detail;

    core::Json to_json() const;
};

class PluginManager {
public:
    explicit PluginManager(core::EventBus* events = nullptr);

    // Registers a native plugin implementation (id must be unique).
    core::Status register_plugin(std::shared_ptr<IPlugin> plugin);

    // Scans `directory` for "*.plugin.json" manifests and registers them.
    // A missing directory is not an error (returns 0).
    core::Result<std::size_t> discover(const std::string& directory);

    core::Status load(const std::string& plugin_id);
    core::Status unload(const std::string& plugin_id);

    // Loads every enabled, not-yet-loaded plugin. Failures are recorded per
    // plugin and never abort the remaining set.
    core::Result<std::size_t> load_all();

    // Unloads everything (called during application shutdown).
    void unload_all();

    core::Result<PluginRecord> get(const std::string& plugin_id) const;
    std::vector<PluginRecord> list() const;

    std::size_t count() const;
    std::size_t loaded_count() const;

    core::Json status() const;

private:
    void publish_change(const std::string& plugin_id, PluginState state, const std::string& detail);

    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<IPlugin>> plugins_;
    std::map<std::string, PluginRecord> records_;
    core::EventBus* events_;
};

}  // namespace trinity::plugins
