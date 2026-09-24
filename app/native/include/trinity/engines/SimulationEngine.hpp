#pragma once

// Simulation engine: deterministic kinematics + basic_dynamics (F=m·a)
// with semi-implicit Euler / closed-form integration, validation, and
// CSV/JSON exports. No CFD/FEA/collision/GPU claims.

#include <string>
#include <vector>

#include "Engine.hpp"
#include "../simulation/Types.hpp"

namespace trinity::engines {

class SimulationEngine : public EngineBase {
public:
    SimulationEngine();

    EngineResult execute(const EngineRequest& request) override;
    validation::ValidationResult validate(const EngineResult& result) const override;

private:
    EngineResult executeCreate(const EngineRequest& request);
    EngineResult executeRun(const EngineRequest& request);
    EngineResult executeValidate(const EngineRequest& request);
    EngineResult executeExport(const EngineRequest& request);
    EngineResult executeOneShot(const EngineRequest& request, const std::string& model);

    simulation::SimulationProject projectFromParams(const core::Json& params,
                                                    const std::string& model) const;
    EngineResult runProject(const EngineRequest& request, const simulation::SimulationProject& project,
                            bool writeArtifacts);
};

}  // namespace trinity::engines
