#include "trinity/robotics/Kinematics.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace trinity::robotics {
namespace {

double round6(double value) { return std::round(value * 1000000.0) / 1000000.0; }

/// Fixed-axis rpy: R = Rz(yaw) * Ry(pitch) * Rx(roll) (URDF convention).
Transform fromRpy(const Vec3& rpy) {
    const Transform rx = Transform::fromAxisAngle({1.0, 0.0, 0.0}, rpy.x);
    const Transform ry = Transform::fromAxisAngle({0.0, 1.0, 0.0}, rpy.y);
    const Transform rz = Transform::fromAxisAngle({0.0, 0.0, 1.0}, rpy.z);
    return rz.compose(ry).compose(rx);
}

double vecNorm(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

struct ResolvedFrames {
    bool success = false;
    std::string error;
    std::map<std::string, Transform> frames;         // link/frame name → base transform
    std::map<std::string, Transform> jointOrigins;   // joint name → parent-side origin frame
    std::map<std::string, Vec3> jointAxesBase;       // joint name → unit axis in base frame
    std::vector<std::string> frameOrder;
};

/// Resolve every joint frame in dependency order. Returns an error for
/// cycles or dangling parent references (validators reject these first,
/// but FK must never silently succeed on a broken chain).
ResolvedFrames resolveFrames(const RobotProject& project, const JointState& state) {
    ResolvedFrames out;
    const RobotModel& model = project.model;
    out.frames[project.baseFrame] = Transform::identity();
    out.frameOrder.push_back(project.baseFrame);

    std::set<std::string> childLinks;
    for (const Joint& joint : model.joints) {
        childLinks.insert(joint.childLink);
    }
    for (const Link& link : model.links) {
        if (childLinks.find(link.name) == childLinks.end() &&
            out.frames.find(link.name) == out.frames.end()) {
            out.frames[link.name] = Transform::identity();
            out.frameOrder.push_back(link.name);
        }
    }

    std::vector<bool> done(model.joints.size(), false);
    size_t resolved = 0;
    bool progress = true;
    while (resolved < model.joints.size() && progress) {
        progress = false;
        for (size_t i = 0; i < model.joints.size(); ++i) {
            if (done[i]) {
                continue;
            }
            const Joint& joint = model.joints[i];
            const auto parentIt = out.frames.find(joint.parentLink);
            if (parentIt == out.frames.end()) {
                continue;
            }
            const Transform& parent = parentIt->second;
            const Transform origin =
                Transform::fromTranslation(joint.originXYZ).compose(fromRpy(joint.originRPYRad));

            bool found = false;
            const double q = state.positionOf(joint.name, found);

            Transform motion = Transform::identity();
            if (joint.type == JointType::Revolute) {
                motion = Transform::fromAxisAngle(joint.axis, q);
            } else if (joint.type == JointType::Prismatic) {
                const double norm = vecNorm(joint.axis);
                if (norm <= 0.0) {
                    out.error = "Joint '" + joint.name + "' has a zero axis";
                    return out;
                }
                motion = Transform::fromTranslation(
                    {joint.axis.x / norm * q, joint.axis.y / norm * q, joint.axis.z / norm * q});
            }

            const Transform beforeMotion = parent.compose(origin);
            const Transform child = beforeMotion.compose(motion);
            out.frames[joint.childLink] = child;
            out.jointOrigins[joint.name] = beforeMotion;
            // Axis direction in the base frame: the joint frame
            // orientation is parent ∘ origin; a revolute motion about
            // its own axis leaves that axis invariant.
            const Transform rotationPart =
                Transform::fromRotationTranslation(
                    [&]() {
                        std::array<std::array<double, 3>, 3> r{};
                        for (int a = 0; a < 3; ++a) {
                            for (int b = 0; b < 3; ++b) {
                                r[static_cast<size_t>(a)][static_cast<size_t>(b)] =
                                    beforeMotion.m[static_cast<size_t>(a)][static_cast<size_t>(b)];
                            }
                        }
                        return r;
                    }(),
                    {0.0, 0.0, 0.0});
            Vec3 axisBase{rotationPart.m[0][0] * joint.axis.x + rotationPart.m[0][1] * joint.axis.y +
                              rotationPart.m[0][2] * joint.axis.z,
                          rotationPart.m[1][0] * joint.axis.x + rotationPart.m[1][1] * joint.axis.y +
                              rotationPart.m[1][2] * joint.axis.z,
                          rotationPart.m[2][0] * joint.axis.x + rotationPart.m[2][1] * joint.axis.y +
                              rotationPart.m[2][2] * joint.axis.z};
            out.jointAxesBase[joint.name] = axisBase;
            out.frameOrder.push_back(joint.childLink);
            done[i] = true;
            ++resolved;
            progress = true;
        }
    }
    if (resolved < model.joints.size()) {
        std::string unresolved;
        for (size_t i = 0; i < model.joints.size(); ++i) {
            if (!done[i]) {
                if (!unresolved.empty()) {
                    unresolved += ", ";
                }
                unresolved += model.joints[i].name;
            }
        }
        out.error =
            "Kinematic hierarchy has a cycle or references an unknown parent link (unresolved: " +
            unresolved + ")";
        return out;
    }
    out.success = true;
    return out;
}

}  // namespace

FkResult computeForwardKinematics(const RobotProject& project, const JointState& state) {
    FkResult out;
    out.linkCount = static_cast<long long>(project.model.links.size());
    out.jointCount = static_cast<long long>(project.model.joints.size());
    if (project.model.joints.empty()) {
        out.error = "Robot model has no joints";
        return out;
    }

    const ResolvedFrames resolved = resolveFrames(project, state);
    if (!resolved.success) {
        out.error = resolved.error;
        return out;
    }

    // End-effector frame: explicit parent, else the last joint's child.
    std::string eeParent = project.endEffector.parentLink;
    if (eeParent.empty()) {
        if (project.model.joints.empty()) {
            out.error = "Robot model has no joints to attach the end effector to";
            return out;
        }
        eeParent = project.model.joints.back().childLink;
    }
    const auto parentIt = resolved.frames.find(eeParent);
    if (parentIt == resolved.frames.end()) {
        out.error = "End-effector parent link '" + eeParent + "' does not exist";
        return out;
    }
    const Transform ee =
        parentIt->second.compose(Transform::fromTranslation(project.endEffector.originXYZ));

    out.frameTransforms = resolved.frames;
    out.frameTransforms[project.endEffector.name] = ee;
    out.frameOrder = resolved.frameOrder;
    out.frameOrder.push_back(project.endEffector.name);
    out.endEffector = ee;
    out.endEffectorPose = Pose::fromTransform(ee);
    for (const Joint& joint : project.model.joints) {
        if (joint.type != JointType::Fixed) {
            ++out.actuatedCount;
        }
    }
    out.success = true;
    return out;
}

bool actuatedJointOrder(const RobotProject& project, std::vector<std::string>& orderOut,
                        std::string& errorOut) {
    orderOut.clear();
    for (const Joint& joint : project.model.joints) {
        if (joint.type != JointType::Fixed) {
            orderOut.push_back(joint.name);
        }
    }
    if (orderOut.empty()) {
        errorOut = "Robot model has no actuated (revolute/prismatic) joints";
        return false;
    }
    if (orderOut.size() > static_cast<size_t>(kMaxJoints)) {
        errorOut = "Robot model exceeds the maximum of " + std::to_string(kMaxJoints) +
                   " actuated joints";
        return false;
    }
    return true;
}

IkResult solveInverseKinematics(const RobotProject& project, const Vec3& targetM,
                                const JointState& seed, const IkOptions& options) {
    IkResult out;
    out.positionOnly = true;  // V1 solves position only

    std::vector<std::string> order;
    std::string orderError;
    if (!actuatedJointOrder(project, order, orderError)) {
        out.error = orderError;
        return out;
    }
    if (!std::isfinite(targetM.x) || !std::isfinite(targetM.y) || !std::isfinite(targetM.z)) {
        out.error = "IK target must be finite";
        return out;
    }
    const long long maxIterations =
        options.maxIterations > 0 ? options.maxIterations : 1;
    const double tolerance = options.toleranceM > 0.0 ? options.toleranceM : 1e-9;
    const double step = options.stepSize > 0.0 ? options.stepSize : 1.0;
    // Keep the damped matrix non-singular even if λ is set to 0.
    const double lambda2 = std::max(options.damping * options.damping, 1e-12);

    std::vector<double> q(order.size(), 0.0);
    for (size_t k = 0; k < order.size(); ++k) {
        bool found = false;
        const double value = seed.positionOf(order[k], found);
        q[k] = found ? value : 0.0;
    }

    // Fixed joint-limit lookup per actuated joint.
    std::vector<const Joint*> joints;
    joints.reserve(order.size());
    for (const std::string& name : order) {
        joints.push_back(project.model.findJoint(name));
    }

    auto clampToLimits = [&](size_t k, double value) {
        value = std::clamp(value, -kMaxJointValue, kMaxJointValue);
        const Joint* joint = joints[k];
        if (joint != nullptr && joint->limit.specified) {
            value = std::clamp(value, joint->limit.lower, joint->limit.upper);
        }
        return value;
    };
    auto withinLimits = [&](size_t k, double value) {
        const Joint* joint = joints[k];
        if (joint == nullptr || !joint->limit.specified) {
            return true;
        }
        return value >= joint->limit.lower - 1e-9 && value <= joint->limit.upper + 1e-9;
    };

    const FkResult fk0 = computeForwardKinematics(project, [&]() {
        JointState st;
        for (size_t k = 0; k < order.size(); ++k) {
            st.positions[order[k]] = q[k];
        }
        return st;
    }());
    if (!fk0.success) {
        out.error = "IK seed forward kinematics failed: " + fk0.error;
        return out;
    }

    double finalError =
        vecNorm(Vec3{targetM.x - fk0.endEffectorPose.positionM.x,
                     targetM.y - fk0.endEffectorPose.positionM.y,
                     targetM.z - fk0.endEffectorPose.positionM.z});
    long long iteration = 0;

    for (; iteration < maxIterations; ++iteration) {
        if (options.cancelCheck && options.cancelCheck()) {
            out.cancelled = true;
            out.iterations = iteration;
            out.error = "IK cancelled by request";
            return out;
        }

        JointState current;
        for (size_t k = 0; k < order.size(); ++k) {
            current.positions[order[k]] = q[k];
        }
        const FkResult fk = computeForwardKinematics(project, current);
        if (!fk.success) {
            out.error = "IK forward kinematics failed: " + fk.error;
            out.iterations = iteration;
            return out;
        }
        const Vec3 p = fk.endEffectorPose.positionM;
        const Vec3 e{targetM.x - p.x, targetM.y - p.y, targetM.z - p.z};
        finalError = vecNorm(e);
        if (finalError <= tolerance) {
            out.converged = true;
            out.iterations = iteration + 1;
            break;
        }

        // Analytic position Jacobian (3 × n):
        // revolute: axis × (p_ee - p_joint); prismatic: axis.
        const size_t n = order.size();
        std::array<std::vector<double>, 3> J;
        for (auto& column : J) {
            column.assign(n, 0.0);
        }
        const ResolvedFrames resolved = resolveFrames(project, current);
        if (!resolved.success) {
            out.error = "IK frame resolution failed: " + resolved.error;
            out.iterations = iteration;
            return out;
        }
        for (size_t k = 0; k < n; ++k) {
            const Joint* joint = joints[k];
            const Vec3 axis = resolved.jointAxesBase.at(order[k]);
            if (joint->type == JointType::Prismatic) {
                const double norm = vecNorm(axis);
                if (norm <= 0.0) {
                    out.error = "Joint '" + order[k] + "' has a zero axis";
                    out.iterations = iteration;
                    return out;
                }
                J[0][k] = axis.x / norm;
                J[1][k] = axis.y / norm;
                J[2][k] = axis.z / norm;
            } else {
                const Vec3 origin = resolved.jointOrigins.at(order[k]).translation();
                const Vec3 rx{p.x - origin.x, p.y - origin.y, p.z - origin.z};
                J[0][k] = axis.y * rx.z - axis.z * rx.y;
                J[1][k] = axis.z * rx.x - axis.x * rx.z;
                J[2][k] = axis.x * rx.y - axis.y * rx.x;
            }
        }

        // Damped least squares: dq = Jᵀ (J Jᵀ + λ²I)⁻¹ e.
        double M[3][3] = {};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                double sum = (r == c) ? lambda2 : 0.0;
                for (size_t k = 0; k < n; ++k) {
                    sum += J[static_cast<size_t>(r)][k] * J[static_cast<size_t>(c)][k];
                }
                M[r][c] = sum;
            }
        }
        // Closed-form 3x3 inverse (M is SPD for λ > 0).
        const double det = M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) -
                           M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                           M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
        if (std::fabs(det) < 1e-15) {
            out.error = "IK damped matrix is singular";
            out.iterations = iteration;
            return out;
        }
        const double inv[3][3] = {
            {(M[1][1] * M[2][2] - M[1][2] * M[2][1]) / det,
             (M[0][2] * M[2][1] - M[0][1] * M[2][2]) / det,
             (M[0][1] * M[1][2] - M[0][2] * M[1][1]) / det},
            {(M[1][2] * M[2][0] - M[1][0] * M[2][2]) / det,
             (M[0][0] * M[2][2] - M[0][2] * M[2][0]) / det,
             (M[0][2] * M[1][0] - M[0][0] * M[1][2]) / det},
            {(M[1][0] * M[2][1] - M[1][1] * M[2][0]) / det,
             (M[0][1] * M[2][0] - M[0][0] * M[2][1]) / det,
             (M[0][0] * M[1][1] - M[0][1] * M[1][0]) / det}};
        double y[3] = {0.0, 0.0, 0.0};
        for (int r = 0; r < 3; ++r) {
            y[r] = inv[r][0] * e.x + inv[r][1] * e.y + inv[r][2] * e.z;
        }
        for (size_t k = 0; k < n; ++k) {
            const double dq = (J[0][k] * y[0] + J[1][k] * y[1] + J[2][k] * y[2]) * step;
            q[k] = clampToLimits(k, q[k] + dq);
        }
    }

    if (!out.converged && !out.cancelled) {
        out.iterations = iteration;
        // Final state for truthful reporting.
        JointState finalState;
        for (size_t k = 0; k < order.size(); ++k) {
            finalState.positions[order[k]] = q[k];
        }
        const FkResult fkFinal = computeForwardKinematics(project, finalState);
        if (fkFinal.success) {
            const Vec3 p = fkFinal.endEffectorPose.positionM;
            finalError = vecNorm(
                Vec3{targetM.x - p.x, targetM.y - p.y, targetM.z - p.z});
            out.finalPose = fkFinal.endEffectorPose;
        }
        out.solution = finalState;
        out.finalErrorM = finalError;
        for (size_t k = 0; k < order.size(); ++k) {
            if (!withinLimits(k, q[k])) {
                out.withinLimits = false;
            }
        }
        out.error = "IK did not converge within " + std::to_string(out.iterations) +
                    " iterations (final position error " + std::to_string(round6(finalError)) +
                    " m)";
        return out;
    }

    JointState finalState;
    for (size_t k = 0; k < order.size(); ++k) {
        finalState.positions[order[k]] = q[k];
        if (!withinLimits(k, q[k])) {
            out.withinLimits = false;
        }
    }
    const FkResult fkFinal = computeForwardKinematics(project, finalState);
    if (!fkFinal.success) {
        out.error = "IK final forward kinematics failed: " + fkFinal.error;
        return out;
    }
    const Vec3 p = fkFinal.endEffectorPose.positionM;
    out.finalErrorM = vecNorm(Vec3{targetM.x - p.x, targetM.y - p.y, targetM.z - p.z});
    out.finalPose = fkFinal.endEffectorPose;
    out.solution = finalState;
    if (out.converged && out.iterations == 0) {
        out.iterations = 1;
    }
    return out;
}

TrajectoryResult generateLinearJointTrajectory(const RobotProject* project,
                                               const std::vector<std::string>& jointOrder,
                                               const std::vector<double>& start,
                                               const std::vector<double>& goal, double durationS,
                                               double dt, const CancelProbe& cancelCheck) {
    TrajectoryResult out;
    if (jointOrder.empty()) {
        out.error = "Trajectory requires a non-empty joint order";
        return out;
    }
    if (start.size() != jointOrder.size() || goal.size() != jointOrder.size()) {
        out.error = "Trajectory start/goal must match the joint count (" +
                    std::to_string(jointOrder.size()) + ")";
        return out;
    }
    if (!std::isfinite(durationS) || !(durationS > 0.0) || durationS > kMaxDurationS) {
        out.error = "Trajectory duration_s must be in (0, " + std::to_string(kMaxDurationS) + "]";
        return out;
    }
    if (!std::isfinite(dt) || !(dt > 0.0) || dt > 10.0) {
        out.error = "Trajectory dt must be in (0, 10]";
        return out;
    }
    for (size_t k = 0; k < start.size(); ++k) {
        if (!std::isfinite(start[k]) || !std::isfinite(goal[k])) {
            out.error = "Trajectory start/goal values must be finite";
            return out;
        }
    }

    // Joint limit enforcement (never silently clamp user inputs).
    bool anyLimit = false;
    if (project != nullptr) {
        for (size_t k = 0; k < jointOrder.size(); ++k) {
            const Joint* joint = project->model.findJoint(jointOrder[k]);
            if (joint == nullptr || !joint->limit.specified) {
                continue;
            }
            anyLimit = true;
            const auto violates = [&](double value) {
                return value < joint->limit.lower - 1e-9 || value > joint->limit.upper + 1e-9;
            };
            if (violates(start[k])) {
                out.error = "Trajectory start violates limits for joint '" + jointOrder[k] +
                            "' (value " + std::to_string(start[k]) + ", limits [" +
                            std::to_string(joint->limit.lower) + ", " +
                            std::to_string(joint->limit.upper) + "])";
                return out;
            }
            if (violates(goal[k])) {
                out.error = "Trajectory goal violates limits for joint '" + jointOrder[k] +
                            "' (value " + std::to_string(goal[k]) + ", limits [" +
                            std::to_string(joint->limit.lower) + ", " +
                            std::to_string(joint->limit.upper) + "])";
                return out;
            }
            if (joint->limit.velocityMax > 0.0) {
                const double speed = std::fabs(goal[k] - start[k]) / durationS;
                if (speed > joint->limit.velocityMax + 1e-9) {
                    out.error = "Trajectory exceeds the velocity limit of joint '" +
                                jointOrder[k] + "' (" + std::to_string(round6(speed)) +
                                " > " + std::to_string(round6(joint->limit.velocityMax)) + ")";
                    return out;
                }
            }
        }
    }
    out.limitsEnforced = anyLimit;

    const double ratio = durationS / dt;
    long long steps = static_cast<long long>(std::floor(ratio + 1e-9));
    if (steps < 1) {
        steps = 1;
    }
    if (steps > kMaxTrajectorySteps) {
        out.error = "Trajectory duration/dt exceeds " + std::to_string(kMaxTrajectorySteps) +
                    " steps";
        return out;
    }
    std::vector<double> times;
    times.reserve(static_cast<size_t>(steps) + 2);
    for (long long i = 0; i <= steps; ++i) {
        times.push_back(static_cast<double>(i) * dt);
    }
    if (times.back() < durationS - 1e-9) {
        times.push_back(durationS);
    } else {
        times.back() = durationS;
    }

    Trajectory& traj = out.trajectory;
    traj.jointOrder = jointOrder;
    traj.durationS = durationS;
    traj.dt = dt;
    traj.timestampsS = times;
    traj.positions.assign(jointOrder.size(), std::vector<double>{});
    traj.velocities.assign(jointOrder.size(), std::vector<double>{});
    for (auto& column : traj.positions) {
        column.reserve(times.size());
    }
    for (auto& column : traj.velocities) {
        column.reserve(times.size());
    }
    std::vector<double> slope(jointOrder.size(), 0.0);
    for (size_t k = 0; k < jointOrder.size(); ++k) {
        slope[k] = (goal[k] - start[k]) / durationS;
    }

    out.withinLimits = true;
    size_t processed = 0;
    for (size_t i = 0; i < times.size(); ++i) {
        if (cancelCheck && (processed % kCancelCheckInterval) == 0 && cancelCheck()) {
            out.cancelled = true;
            out.error = "Trajectory generation cancelled";
            return out;
        }
        ++processed;
        const double t = times[i];
        for (size_t k = 0; k < jointOrder.size(); ++k) {
            const double position =
                (i == times.size() - 1) ? goal[k] : start[k] + slope[k] * t;
            traj.positions[k].push_back(position);
            traj.velocities[k].push_back(slope[k]);
            if (project != nullptr) {
                const Joint* joint = project->model.findJoint(jointOrder[k]);
                if (joint != nullptr && joint->limit.specified &&
                    (position < joint->limit.lower - 1e-9 ||
                     position > joint->limit.upper + 1e-9)) {
                    out.withinLimits = false;
                }
            }
        }
    }

    // Endpoint verification (truthful reporting for validation).
    for (size_t k = 0; k < jointOrder.size(); ++k) {
        if (std::fabs(traj.positions[k].front() - start[k]) > 1e-6 ||
            std::fabs(traj.positions[k].back() - goal[k]) > 1e-6) {
            out.error = "Trajectory endpoints do not match start/goal for joint '" +
                        jointOrder[k] + "'";
            return out;
        }
    }
    out.success = true;
    return out;
}

}  // namespace trinity::robotics
