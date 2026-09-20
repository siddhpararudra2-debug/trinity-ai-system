#pragma once

// Domain stub engines: PCB, Firmware, Vision, Research, Simulation,
// Robotics. Each registers capability metadata and refuses execution
// with structured CAPABILITY_UNAVAILABLE — never fake results.

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

std::shared_ptr<StubEngine> makePcbEngine();
std::shared_ptr<StubEngine> makeFirmwareEngine();
std::shared_ptr<StubEngine> makeVisionEngine();
std::shared_ptr<StubEngine> makeResearchEngine();
std::shared_ptr<StubEngine> makeSimulationEngine();
std::shared_ptr<StubEngine> makeRoboticsEngine();

class EngineRegistry;

/// Register all Phase-2 engines: math + cad + six domain stubs.
/// Idempotent per process: skips engines already registered (e.g. when
/// ApplicationContext::initialize runs twice in tests).
void registerAllEngines(EngineRegistry& registry);

}  // namespace trinity::engines
