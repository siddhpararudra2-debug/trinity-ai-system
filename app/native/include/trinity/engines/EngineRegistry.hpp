#pragma once

// Engine registry: the single process-wide catalogue of deterministic
// engines. Mirrors src/engines/registry.py — a future LLM tool-caller
// asks "what can Trinity do?" and gets a structured answer instead of
// a hardcoded prompt. Engine implementations themselves (math, cad,
// pcb, firmware, vision, research, simulation, robotics) are later
// phases; only the registry ships now.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Engine.hpp"

namespace trinity::engines {

class EngineRegistry {
public:
    void registerEngine(std::shared_ptr<IEngine> engine);
    std::shared_ptr<IEngine> get(const std::string& name) const;
    bool has(const std::string& name) const noexcept;
    std::vector<EngineCapability> list() const;

    // Names of engines planned for later phases. No implementations
    // ship yet; kept here so UI and tests share one catalogue.
    static const std::vector<std::string>& plannedEngineNames();

private:
    std::map<std::string, std::shared_ptr<IEngine>> engines_;
};

}  // namespace trinity::engines
