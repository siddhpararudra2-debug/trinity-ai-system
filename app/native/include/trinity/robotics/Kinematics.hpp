#pragma once

// Robotics kinematics: deterministic forward kinematics, V1
// position-only damped-least-squares inverse kinematics for serial
// manipulators, and deterministic joint-space linear trajectory
// generation. SI units (m, rad, s). No dynamics or collision.

#include <map>
#include <string>
#include <vector>

#include "Types.hpp"

namespace trinity::robotics {

struct FkResult {
    bool success = false;
    std::string error;
    /// Frame name → transform of that frame relative to the base frame.
    /// Includes the base frame, one frame per link reached by joints,
    /// and the end-effector frame.
    std::map<std::string, Transform> frameTransforms;
    std::vector<std::string> frameOrder;  // base frame first, then chain order
    Transform endEffector = Transform::identity();
    Pose endEffectorPose;
    long long linkCount = 0;
    long long jointCount = 0;
    /// Number of actuated (revolute + prismatic) joints.
    long long actuatedCount = 0;
};

/// Compute per-link transforms and the end-effector pose for `state`.
/// Joint positions missing from `state` are treated as 0.0 (documented
/// default, reported through the returned state usage by the engine).
/// Deterministic: frames resolved in joint declaration order with
/// dependency checks; same input → same output.
FkResult computeForwardKinematics(const RobotProject& project, const JointState& state);

struct IkOptions {
    long long maxIterations = 200;
    double toleranceM = 1e-4;
    double stepSize = 1.0;
    double damping = 0.05;  // λ for damped least squares (λ² on diagonal)
    CancelProbe cancelCheck;  // optional cooperative probe, polled each iteration
};

struct IkResult {
    bool converged = false;
    long long iterations = 0;
    double finalErrorM = 0.0;
    bool cancelled = false;
    bool withinLimits = true;
    bool positionOnly = true;  // V1 solves position only — always true
    std::string error;         // non-empty when the solver failed
    JointState solution;
    /// EE pose reached at the final iterate (for verification/reporting).
    Pose finalPose;
};

/// V1 position-only IK for a serial manipulator (damped least squares
/// on the analytic position Jacobian). Clamps every iterate to the
/// configured joint limits. Never claims convergence it did not
/// achieve: `converged` is true only when the final position error is
/// within `toleranceM`. Orientation is not controlled in V1
/// (`positionOnly` is always true).
IkResult solveInverseKinematics(const RobotProject& project, const Vec3& targetM,
                                const JointState& seed, const IkOptions& options);

struct TrajectoryResult {
    bool success = false;
    std::string error;
    Trajectory trajectory;
    /// True when a model with joint limits was provided and every
    /// sample was checked against those limits.
    bool limitsEnforced = false;
    bool withinLimits = true;
    bool cancelled = false;
};

/// Deterministic joint-space linear trajectory:
/// q(t) = start + (goal - start) * t / duration (closed form).
/// Start/goal must satisfy the configured joint limits when a model
/// with limits is provided (violations are rejected, never silently
/// clamped). `project` may be null for model-less generation (no
/// limits to enforce — reported via limitsEnforced=false).
TrajectoryResult generateLinearJointTrajectory(const RobotProject* project,
                                               const std::vector<std::string>& jointOrder,
                                               const std::vector<double>& start,
                                               const std::vector<double>& goal,
                                               double durationS, double dt,
                                               const CancelProbe& cancelCheck = nullptr);

/// Resolve the actuated-joint order (revolute/prismatic, model order).
/// Returns false with `error` when the model is not a usable serial
/// chain (also used by validators for early rejection).
bool actuatedJointOrder(const RobotProject& project, std::vector<std::string>& orderOut,
                        std::string& errorOut);

}  // namespace trinity::robotics
