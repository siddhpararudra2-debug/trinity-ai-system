#include "trinity/engines/EngineRegistry.hpp"

#include <algorithm>
#include <chrono>

#include "trinity/core/Error.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/core/Time.hpp"

namespace trinity::engines {

void EngineRegistry::registerEngine(std::shared_ptr<IEngine> engine) {
    if (!engine) {
        throw core::RequestValidationError("Cannot register null engine", {}, "engines");
    }
    const std::string engineName = engine->name();
    if (engineName.empty()) {
        throw core::RequestValidationError("Engine name must not be empty", {}, "engines");
    }
    if (engines_.find(engineName) != engines_.end()) {
        throw core::RequestValidationError(
            "Engine '" + engineName + "' is already registered", {{"engine", engineName}},
            "engines");
    }
    engines_[engineName] = std::move(engine);
    core::Logger::instance().info("engines", "engine registered",
                                  core::Json{{"engine", engineName}});
}

bool EngineRegistry::unregisterEngine(const std::string& name) {
    const auto it = engines_.find(name);
    if (it == engines_.end()) {
        return false;
    }
    engines_.erase(it);
    core::Logger::instance().info("engines", "engine unregistered",
                                  core::Json{{"engine", name}});
    return true;
}

std::shared_ptr<IEngine> EngineRegistry::get(const std::string& name) const {
    const auto it = engines_.find(name);
    if (it == engines_.end()) {
        core::Json available = core::Json::array();
        for (const auto& [key, _] : engines_) {
            available.push_back(key);
        }
        std::sort(available.begin(), available.end());
        throw core::EngineNotFoundError("No engine registered under name '" + name + "'",
                                        core::Json{{"available", available}}, "engines");
    }
    return it->second;
}

bool EngineRegistry::has(const std::string& name) const noexcept {
    return engines_.find(name) != engines_.end();
}

std::vector<EngineCapability> EngineRegistry::list() const {
    std::vector<EngineCapability> capabilities;
    capabilities.reserve(engines_.size());
    for (const auto& [_, engine] : engines_) {
        capabilities.push_back(engine->describe());
    }
    return capabilities;
}

std::vector<std::string> EngineRegistry::listCapabilities(
    const std::string& name) const {
    return get(name)->capabilities();
}

EngineResult EngineRegistry::execute(const EngineRequest& request) const {
    auto& log = core::Logger::instance();
    if (request.engine.empty()) {
        throw core::RequestValidationError("EngineRequest.engine must not be empty",
                                           request.toJson(), "engines");
    }
    if (request.operation.empty()) {
        throw core::RequestValidationError("EngineRequest.operation must not be empty",
                                           request.toJson(), "engines");
    }
    std::shared_ptr<IEngine> engine = get(request.engine);
    const auto started = std::chrono::steady_clock::now();
    log.info("engines", "engine execute started",
             core::Json{{"engine", request.engine},
                        {"operation", request.operation},
                        {"request_id", request.requestId}});
    EngineResult result;
    try {
        result = engine->execute(request);
    } catch (const core::TrinityError&) {
        throw;
    } catch (const std::exception& exc) {
        throw core::EngineExecutionError(
            std::string("Engine '") + request.engine + "' failed: " + exc.what(),
            {{"engine", request.engine}, {"operation", request.operation}}, "engines");
    }
    result.engine = request.engine;
    if (result.operation.empty()) {
        result.operation = request.operation;
    }
    if (result.requestId.empty()) {
        result.requestId = request.requestId;
    }
    try {
        result.validation = engine->validate(result);
    } catch (const std::exception& exc) {
        log.warning("engines", "engine validate threw",
                    core::Json{{"engine", request.engine}, {"error", exc.what()}});
    }
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - started)
                               .count();
    if (result.metadata.is_null()) {
        result.metadata = core::Json::object();
    }
    result.metadata["duration_ms"] = elapsedMs;
    result.metadata["executed_at"] = core::utcNowIso();
    log.info("engines", "engine execute finished",
             core::Json{{"engine", request.engine},
                        {"operation", request.operation},
                        {"success", result.success},
                        {"duration_ms", elapsedMs}});
    return result;
}

const std::vector<std::string>& EngineRegistry::plannedEngineNames() {
    static const std::vector<std::string> names = {
        "math", "cad", "pcb", "firmware", "vision", "research", "simulation", "robotics",
    };
    return names;
}

}  // namespace trinity::engines
