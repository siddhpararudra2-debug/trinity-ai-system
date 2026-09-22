#pragma once

// First real firmware engine: deterministic project/MCU/pin/peripheral
// modeling over the tool-agnostic IR, with Arduino-style source
// generation, controlled build verification, and ArtifactManager
// integration. State flows as project JSON between operations so jobs
// and workflow nodes chain (create -> mcu -> pins -> generate ->
// validate -> build). Anything beyond the listed capabilities
// (RTOS, flashing, OTA, PlatformIO, simulation) refuses truthfully.

#include <string>
#include <vector>

#include "Engine.hpp"

namespace trinity::engines {

class FirmwareEngine : public EngineBase {
public:
    FirmwareEngine();

    EngineResult execute(const EngineRequest& request) override;
    validation::ValidationResult validate(const EngineResult& result) const override;

    /// Supported MCU models (single source of truth for the UI combo).
    static std::vector<std::string> mcuNames();

private:
    EngineResult executeCreateProject(const EngineRequest& request);
    EngineResult executeSelectMcu(const EngineRequest& request);
    EngineResult executeConfigurePin(const EngineRequest& request);
    EngineResult executeConfigurePeripheral(const EngineRequest& request);
    EngineResult executeGenerateFirmware(const EngineRequest& request);
    EngineResult executeValidateProject(const EngineRequest& request);
    EngineResult executeBuild(const EngineRequest& request);
};

}  // namespace trinity::engines
