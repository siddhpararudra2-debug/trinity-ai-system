#pragma once

// Robotics engine: deterministic forward kinematics (standard DH
// convention and joint/link IR), joint-space trajectories, URDF export
// with SHA-256 checksum, V1 position-only damped-least-squares inverse
// kinematics, and structural model validation. No dynamics, collision,
// or ML claims.

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "Engine.hpp"
#include "../core/Json.hpp"
#include "../robotics/Types.hpp"

namespace trinity::engines {

class RoboticsEngine : public EngineBase {
public:
    RoboticsEngine();

    EngineResult execute(const EngineRequest& request) override;
    validation::ValidationResult validate(const EngineResult& result) const override;

private:
    // Legacy DH operations (kept byte-compatible).
    EngineResult executeForwardKinematics(const EngineRequest& request);
    EngineResult executePlanTrajectory(const EngineRequest& request);
    EngineResult executeExportUrdf(const EngineRequest& request);

    // Joint/link IR operations.
    EngineResult executeCreateRobot(const EngineRequest& request);
    EngineResult executeAddLink(const EngineRequest& request);
    EngineResult executeAddJoint(const EngineRequest& request);
    EngineResult executeSetJointState(const EngineRequest& request);
    EngineResult executeRobotFk(const EngineRequest& request);
    EngineResult executeInverseKinematics(const EngineRequest& request);
    EngineResult executeGenerateTrajectory(const EngineRequest& request);
    EngineResult executeValidateRobot(const EngineRequest& request);

    /// Resolve the target robot from `robot` (inline IR) or
    /// `project_id` (engine-stored project). Stores inline robots so
    /// validate() can re-check them. Throws
    /// core::RequestValidationError when neither is usable.
    robotics::RobotProject resolveProject(const core::Json& params,
                                           const std::string& operation);
    /// Persist a project under its project_id (generating one when
    /// absent) and return the stored copy.
    robotics::RobotProject storeProject(robotics::RobotProject project);
    /// Const lookup used by validate() for independent recomputation.
    std::optional<robotics::RobotProject> findProject(const std::string& projectId) const;

    // Last resolved DH chain so export_urdf can reuse the chain that a
    // previous forward_kinematics call used (same chaining pattern as
    // the PCB design / firmware project JSON).
    core::Json lastChain_ = core::Json::array();

    // Stored RobotProjects, keyed by project_id. Guarded by mutex even
    // though the worker thread is currently the sole caller.
    mutable std::mutex projectsMutex_;
    std::map<std::string, robotics::RobotProject> projects_;
};

}  // namespace trinity::engines
