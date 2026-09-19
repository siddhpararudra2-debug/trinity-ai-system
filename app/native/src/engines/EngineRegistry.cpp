#include "trinity/engines/EngineRegistry.hpp"

#include <algorithm>

#include "trinity/core/Error.hpp"

namespace trinity::engines {

void EngineRegistry::registerEngine(std::shared_ptr<IEngine> engine) {
    engines_[engine->name()] = std::move(engine);
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
                                        core::Json{{"available", available}});
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

const std::vector<std::string>& EngineRegistry::plannedEngineNames() {
    static const std::vector<std::string> names = {
        "math", "cad", "pcb", "firmware", "vision", "research", "simulation", "robotics",
    };
    return names;
}

}  // namespace trinity::engines
