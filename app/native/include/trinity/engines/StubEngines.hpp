#pragma once

// Generic scaffold engine used only inside tests (e.g. registry guard
// coverage). All eight shipped domains — math, cad, pcb, firmware,
// vision, research, simulation, robotics — are real engines.

#include <string>
#include <vector>

#include "Engine.hpp"

namespace trinity::engines {

/// Generic scaffold engine used for unimplemented domains.
class StubEngine : public EngineBase {
public:
    StubEngine(std::string engineName, std::string engineVersion,
               std::vector<std::string> capabilityNames);

    EngineResult execute(const EngineRequest& request) override;
    validation::ValidationResult validate(const EngineResult& result) const override;
};

class EngineRegistry;

/// Register all engines: math + cad + pcb + firmware + vision +
/// research + simulation + robotics. Idempotent per process:
/// skips engines
/// already registered (e.g. when ApplicationContext::initialize runs
/// twice in tests).
void registerAllEngines(EngineRegistry& registry);

}  // namespace trinity::engines
