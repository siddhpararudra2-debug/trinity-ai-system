#pragma once

// Robotics engine: deterministic forward kinematics (standard DH
// convention), joint-space trajectories integrated through
// simulation::Integrator (closed form), and URDF export with SHA-256
// checksum. No dynamics, collision, or ML claims.

#include <string>
#include <vector>

#include "Engine.hpp"
#include "../core/Json.hpp"

namespace trinity::engines {

class RoboticsEngine : public EngineBase {
public:
    RoboticsEngine();

    EngineResult execute(const EngineRequest& request) override;
    validation::ValidationResult validate(const EngineResult& result) const override;

private:
    EngineResult executeForwardKinematics(const EngineRequest& request);
    EngineResult executePlanTrajectory(const EngineRequest& request);
    EngineResult executeExportUrdf(const EngineRequest& request);

    // Last resolved DH chain so export_urdf can reuse the chain that a
    // previous forward_kinematics call used (same chaining pattern as
    // the PCB design / firmware project JSON).
    core::Json lastChain_ = core::Json::array();
};

}  // namespace trinity::engines
