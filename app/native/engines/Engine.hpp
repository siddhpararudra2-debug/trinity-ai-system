// Trinity — unified Engine interface (brief §Engine System).
// Every engine exposes: engine ID, name, version, capabilities, input schema,
// output schema, execution, validation and health status. Engines never depend
// on an LLM; the model layer is a separate replaceable provider.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../core/Error.hpp"
#include "../core/Json.hpp"

namespace trinity::engines {

// Structured JSON-schema-ish descriptor (subset sufficient for UI + planning).
struct IOSchema {
    std::vector<std::pair<std::string, std::string>> fields;  // name, type/doc
    core::Json to_json() const {
        core::Json out = core::Json::array();
        for (const auto& [name, doc] : fields) {
            core::Json entry = core::Json::object();
            entry["name"] = name;
            entry["type"] = doc;
            out.push_back(entry);
        }
        return out;
    }
};

enum class EngineHealth { Healthy, Degraded, Scaffolded, Unavailable };

const char* engine_health_string(EngineHealth health);

struct EngineDescriptor {
    std::string id;
    std::string name;
    std::string version;
    std::vector<std::string> capabilities;
    IOSchema input_schema;
    IOSchema output_schema;
    EngineHealth health = EngineHealth::Healthy;
    std::string health_detail;

    core::Json to_json() const {
        core::Json out = core::Json::object();
        out["id"] = id;
        out["name"] = name;
        out["version"] = version;
        core::Json caps = core::Json::array();
        for (const std::string& capability : capabilities) caps.push_back(core::Json(capability));
        out["capabilities"] = caps;
        out["input_schema"] = input_schema.to_json();
        out["output_schema"] = output_schema.to_json();
        out["health"] = engine_health_string(health);
        out["health_detail"] = health_detail;
        return out;
    }
};

struct ExecutionOutput {
    bool success = false;
    core::Json result = core::Json::object();
    // Files an engine produced in scratch space; the caller moves them into
    // the ArtifactStore (single-writer rule) then cleans up scratch.
    std::vector<std::pair<std::string, std::string>> pending_artifacts;  // path, type
    core::Json validation_checks = core::Json::object();
    std::string validation_status;  // GENERATED | VALIDATED | VERIFIED | FAILED
};

class IEngine {
public:
    virtual ~IEngine() = default;

    virtual EngineDescriptor describe() const = 0;

    // Executes a capability. Throws core::TrinityException on classified
    // failure (callers in the job system catch and convert).
    virtual ExecutionOutput execute(const std::string& capability,
                                    const core::Json& parameters) = 0;

    // Engine-specific validation of a produced payload (brief §Validation).
    virtual core::Json validate(const core::Json& payload) = 0;

    virtual EngineHealth health() const = 0;
};

// ---------------------------------------------------------------- registry

class EngineRegistry {
public:
    // Singleton by necessity (process-wide discovery point) but access is
    // encapsulated; registration happens at startup only.
    static EngineRegistry& instance();

    // Takes ownership. Later registrations with the same id replace earlier
    // ones (allows adapter overrides by the user).
    core::Status register_engine(std::shared_ptr<IEngine> engine);

    core::Result<std::shared_ptr<IEngine>> get(const std::string& engine_id) const;
    bool has(const std::string& engine_id) const;
    std::vector<EngineDescriptor> list() const;
    std::size_t count() const;

private:
    EngineRegistry() = default;
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<IEngine>> engines_;
};

// Registers the built-in engines. Returns the list of registered ids.
std::vector<std::string> bootstrap_builtin_engines();

}  // namespace trinity::engines
