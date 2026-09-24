#pragma once

// Domain stub engine: Robotics. Registers capability metadata and
// refuses execution with structured CAPABILITY_UNAVAILABLE — never
// fake results. (CAD, PCB, Firmware, Vision, Math, Research and
// Simulation graduated to real engines.)

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

std::shared_ptr<StubEngine> makeRoboticsEngine();

class EngineRegistry;

/// Register all engines: math + cad + pcb + firmware + vision +
/// research + simulation + robotics stub. Idempotent per process:
/// skips engines
/// already registered (e.g. when ApplicationContext::initialize runs
/// twice in tests).
void registerAllEngines(EngineRegistry& registry);

}  // namespace trinity::engines
