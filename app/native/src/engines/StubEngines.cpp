#include "trinity/engines/StubEngines.hpp"

#include "trinity/core/Logger.hpp"
#include "trinity/engines/CadEngine.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/FirmwareEngine.hpp"
#include "trinity/engines/MathEngine.hpp"
#include "trinity/engines/PcbEngine.hpp"

namespace trinity::engines {

StubEngine::StubEngine(std::string engineName, std::string engineVersion,
                       std::vector<std::string> capabilityNames) {
    name_ = std::move(engineName);
    version_ = std::move(engineVersion);
    capabilities_ = std::move(capabilityNames);
}

EngineResult StubEngine::execute(const EngineRequest& request) {
    // Stubs expose metadata but perform no work: always refuse truthfully.
    EngineResult out = capabilityUnavailable(
        request, "Engine '" + name_ + "' is scaffolded; no operations implemented yet");
    core::Logger::instance().warning(
        "engines", "stub engine refused operation",
        core::Json{{"engine", name_}, {"operation", request.operation}});
    return out;
}

validation::ValidationResult StubEngine::validate(const EngineResult& result) const {
    validation::ValidationResult validation;
    validation.operation = result.operation;
    validation.jobId = result.jobId;
    validation.checks = {{"engine", name_}, {"operation", result.operation}};
    validation.status = validation::ValidationStatus::Invalid;
    validation.message = "Stub engine produced no validated output";
    validation::ValidationMessage msg;
    msg.rule = "stub.unimplemented";
    msg.severity = validation::Severity::Warning;
    msg.passed = false;
    msg.message = "Engine '" + name_ + "' is scaffolded only";
    validation.addMessage(std::move(msg));
    if (!result.errors.empty()) {
        validation.error = result.errors.front();
    }
    return validation;
}

std::shared_ptr<StubEngine> makeVisionEngine() {
    return std::make_shared<StubEngine>("vision", "0.1.0",
                                        std::vector<std::string>{"describe"});
}

std::shared_ptr<StubEngine> makeResearchEngine() {
    return std::make_shared<StubEngine>("research", "0.1.0",
                                        std::vector<std::string>{"describe"});
}

std::shared_ptr<StubEngine> makeSimulationEngine() {
    return std::make_shared<StubEngine>("simulation", "0.1.0",
                                        std::vector<std::string>{"describe"});
}

std::shared_ptr<StubEngine> makeRoboticsEngine() {
    return std::make_shared<StubEngine>("robotics", "0.1.0",
                                        std::vector<std::string>{"describe"});
}

void registerAllEngines(EngineRegistry& registry) {
    if (!registry.has("math")) {
        registry.registerEngine(std::make_shared<MathEngine>());
    }
    if (!registry.has("cad")) {
        registry.registerEngine(std::make_shared<CadEngine>());
    }
    if (!registry.has("pcb")) {
        registry.registerEngine(std::make_shared<PcbEngine>());
    }
    if (!registry.has("firmware")) {
        registry.registerEngine(std::make_shared<FirmwareEngine>());
    }
    if (!registry.has("vision")) {
        registry.registerEngine(makeVisionEngine());
    }
    if (!registry.has("research")) {
        registry.registerEngine(makeResearchEngine());
    }
    if (!registry.has("simulation")) {
        registry.registerEngine(makeSimulationEngine());
    }
    if (!registry.has("robotics")) {
        registry.registerEngine(makeRoboticsEngine());
    }
}

}  // namespace trinity::engines
