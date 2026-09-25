#pragma once

// Robotics IR (V1).
//
// Coordinate-frame convention (documented once, enforced everywhere):
//  * Right-handed frames. Base frame is `world` by default and every
//    other frame is expressed as a homogeneous transform relative to
//    its parent (URDF-style: joint origin = translation + fixed-axis
//    rpy rotation Rz(yaw)*Ry(pitch)*Rx(roll), then joint motion).
//  * SI units internally: meters, radians, seconds. Lengths given in
//    mm and angles in degrees are normalized at the intent/UI/engine
//    boundary; original units are preserved on the request metadata.
//  * Revolute joints rotate about `axis`, prismatic joints translate
//    along `axis`, fixed joints have no degrees of freedom and only
//    carry the origin transform. The axis is expressed in the joint
//    (child) frame and must be finite and non-zero.
//  * Joint positions: radians for revolute, meters for prismatic.
//    Joint velocities: rad/s and m/s respectively.
//
// V1 supports serial chains of revolute/prismatic/fixed joints only.
// No dynamics, collision, planning, or hardware control (future
// phases: ROS2, SLAM, path planning, rigid-body simulation).

#include <array>
#include <map>
#include <string>
#include <vector>

#include "../core/Json.hpp"
#include "../simulation/Types.hpp"  // Vec3 + CancelProbe reuse

namespace trinity::robotics {

/// Reuse the simulation IR vector type — no duplicate 3D vector
/// implementation (shared with Math/Simulation systems).
using simulation::Vec3;
using simulation::CancelProbe;

/// Row-major homogeneous transform (4x4). Rigid-body only: rotation
/// block must stay orthonormal (validated on import).
struct Transform {
    std::array<std::array<double, 4>, 4> m{};

    static Transform identity();
    static Transform fromTranslation(const Vec3& translation);
    /// Rotation of `angleRad` about unit `axis` (axis expressed in the
    /// frame the transform is defined in), zero translation.
    static Transform fromAxisAngle(const Vec3& axis, double angleRad);
    static Transform fromRotationTranslation(const std::array<std::array<double, 3>, 3>& rotation,
                                             const Vec3& translation);

    /// this ∘ next (apply `next` first in parent coordinates):
    /// composed = m * next.m.
    Transform compose(const Transform& next) const;
    /// Rigid inverse (rotation transposed, translation back-rotated).
    Transform inverse() const;

    Vec3 transformPoint(const Vec3& point) const;
    Vec3 rotationColumn(int column) const;  // 0..2 → world x/y/z basis vector
    Vec3 translation() const;

    core::Json toJson() const;
    static Transform fromJson(const core::Json& json);
};

/// End-effector (or frame) pose: position + 3x3 row-major rotation.
struct Pose {
    Vec3 positionM;
    std::array<std::array<double, 3>, 3> rotation{
        {{{1.0, 0.0, 0.0}}, {{0.0, 1.0, 0.0}}, {{0.0, 0.0, 1.0}}}};

    core::Json toJson() const;
    static Pose fromJson(const core::Json& json);
    static Pose fromTransform(const Transform& transform);
};

enum class JointType { Revolute, Prismatic, Fixed };

std::string toString(JointType type);
JointType jointTypeFromString(const std::string& value);  // throws on unknown

struct JointLimit {
    bool specified = false;  // false → unlimited (V1 default for fixed)
    double lower = 0.0;
    double upper = 0.0;
    double velocityMax = 0.0;  // 0 → unspecified; rad/s (revolute) or m/s (prismatic)

    core::Json toJson() const;
    static JointLimit fromJson(const core::Json& json);
};

struct Link {
    std::string name;
    double lengthM = 0.0;  // visual/structural length along the link frame x

    core::Json toJson() const;
    static Link fromJson(const core::Json& json);
};

struct Joint {
    std::string name;
    JointType type = JointType::Revolute;
    std::string parentLink;
    std::string childLink;
    Vec3 axis{0.0, 0.0, 1.0};  // joint frame axis (unit after validation)
    Vec3 originXYZ;            // joint origin translation in parent frame (m)
    Vec3 originRPYRad;         // fixed joint origin rotation (rpy, rad)
    JointLimit limit;

    core::Json toJson() const;
    static Joint fromJson(const core::Json& json);
};

/// Full joint positions/velocities keyed by joint name (std::map →
/// deterministic iteration for serialization and tests).
struct JointState {
    std::map<std::string, double> positions;   // rad (revolute) / m (prismatic)
    std::map<std::string, double> velocities;  // rad/s or m/s (optional)

    core::Json toJson() const;
    static JointState fromJson(const core::Json& json);
    double positionOf(const std::string& jointName, bool& found) const;
};

struct EndEffector {
    std::string name = "tool0";
    std::string parentLink;  // attached to this link's frame
    Vec3 originXYZ;          // offset from parent link frame (m)

    core::Json toJson() const;
    static EndEffector fromJson(const core::Json& json);
};

/// Structured command payload: joint commands and/or an IK target.
struct ControlInput {
    std::map<std::string, double> jointPositions;
    std::map<std::string, double> jointVelocities;
    Vec3 targetPositionM;
    bool hasTargetPosition = false;

    core::Json toJson() const;
    static ControlInput fromJson(const core::Json& json);
};

struct Trajectory {
    std::vector<std::string> jointOrder;  // column order (model joint order)
    std::vector<double> timestampsS;
    /// [jointIndex][sampleIndex] parallel arrays (deterministic order).
    std::vector<std::vector<double>> positions;
    std::vector<std::vector<double>> velocities;
    double durationS = 0.0;
    double dt = 0.0;

    long long sampleCount() const;
    core::Json toJson() const;
    static Trajectory fromJson(const core::Json& json);
};

struct RobotModel {
    std::string name;
    std::string baseFrame = "world";
    std::vector<Link> links;
    std::vector<Joint> joints;

    const Link* findLink(const std::string& name) const;
    const Joint* findJoint(const std::string& name) const;

    core::Json toJson() const;
    static RobotModel fromJson(const core::Json& json);
};

struct RobotProject {
    std::string projectId;
    std::string robotName;
    RobotModel model;
    /// Project-level joint limits keyed by joint name. Applied to the
    /// joints on import (joint entries without an explicit limit win
    /// nothing: the map fills gaps); exported back out for the UI.
    std::map<std::string, JointLimit> jointLimits;
    std::string baseFrame = "world";
    EndEffector endEffector;
    JointState initialState;   // also serves as the current state
    Trajectory trajectory;     // empty until a trajectory is generated
    core::Json metadata = core::Json::object();

    /// Fill joint limits from the jointLimits map where joints leave
    /// them unspecified (single source of truth before validation).
    void syncJointLimits();

    core::Json toJson() const;
    static RobotProject fromJson(const core::Json& json);
};

struct RobotResult {
    std::string projectId;
    std::string operation;  // forward_kinematics | inverse_kinematics | ...
    long long linkCount = 0;
    long long jointCount = 0;
    /// Frame name → pose of that frame expressed in the base frame
    /// (base frame itself, one frame per link, plus end effector).
    std::map<std::string, Pose> framePoses;
    Pose endEffectorPose;
    // IK reporting (V1 position-only solver).
    bool converged = false;
    long long iterations = 0;
    double finalErrorM = 0.0;
    bool positionOnly = true;  // V1 solves position only — always true
    JointState solution;
    Trajectory trajectory;  // populated for generate_trajectory
    core::Json checks = core::Json::object();
    core::Json summary = core::Json::object();

    core::Json toJson() const;
    static RobotResult fromJson(const core::Json& json);
};

constexpr int kMaxJoints = 12;
constexpr int kMaxLinks = 16;
constexpr double kMaxLinkLengthM = 100.0;
constexpr double kMaxJointValue = 1.0e6;
constexpr double kMaxDurationS = 3600.0;
constexpr long long kMaxTrajectorySteps = 1000000;
constexpr double kDefaultLinkLengthM = 0.1;
constexpr int kCancelCheckInterval = 256;

}  // namespace trinity::robotics
