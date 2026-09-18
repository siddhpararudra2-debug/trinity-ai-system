#include "Engine.hpp"

#include <algorithm>

namespace trinity::engines {

const char* engine_health_string(EngineHealth health) {
    switch (health) {
        case EngineHealth::Healthy: return "healthy";
        case EngineHealth::Degraded: return "degraded";
        case EngineHealth::Scaffolded: return "scaffolded";
        case EngineHealth::Unavailable: return "unavailable";
    }
    return "unavailable";
}

EngineRegistry& EngineRegistry::instance() {
    static EngineRegistry registry;
    return registry;
}

core::Status EngineRegistry::register_engine(std::shared_ptr<IEngine> engine) {
    if (!engine) {
        return core::Status::fail(
            core::Error(core::ErrorCode::RequestValidationError, "engine instance required"));
    }
    const EngineDescriptor descriptor = engine->describe();
    if (descriptor.id.empty()) {
        return core::Status::fail(
            core::Error(core::ErrorCode::RequestValidationError, "engine id required"));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    engines_[descriptor.id] = std::move(engine);
    return core::Status::ok();
}

core::Result<std::shared_ptr<IEngine>> EngineRegistry::get(const std::string& engine_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = engines_.find(engine_id);
    if (it == engines_.end()) {
        return core::Result<std::shared_ptr<IEngine>>::fail(
            core::Error(core::ErrorCode::EngineNotFoundError,
                        "no engine registered under id '" + engine_id + "'"));
    }
    return core::Result<std::shared_ptr<IEngine>>::ok(it->second);
}

bool EngineRegistry::has(const std::string& engine_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engines_.count(engine_id) > 0;
}

std::vector<EngineDescriptor> EngineRegistry::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EngineDescriptor> out;
    out.reserve(engines_.size());
    for (const auto& [id, engine] : engines_) {
        (void)id;
        out.push_back(engine->describe());
    }
    std::sort(out.begin(), out.end(),
              [](const EngineDescriptor& a, const EngineDescriptor& b) { return a.id < b.id; });
    return out;
}

std::size_t EngineRegistry::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engines_.size();
}

std::vector<std::string> bootstrap_builtin_engines() {
    // Defined in CadEngine.cpp / MathEngine.cpp / ScaffoldEngines.cpp.
    std::vector<std::string> registered;
    void register_cad_engine(std::vector<std::string>& out);
    void register_math_engine(std::vector<std::string>& out);
    void register_scaffold_engines(std::vector<std::string>& out);
    register_cad_engine(registered);
    register_math_engine(registered);
    register_scaffold_engines(registered);
    return registered;
}

}  // namespace trinity::engines
