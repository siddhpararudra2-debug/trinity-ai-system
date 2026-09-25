#include "trinity/robotics/Types.hpp"

#include <cmath>
#include <stdexcept>

namespace trinity::robotics {

namespace {

double round6(double value) { return std::round(value * 1000000.0) / 1000000.0; }

Vec3 vecFromJson(const core::Json& json) { return Vec3::fromJson(json); }

core::Json vecArray(const std::vector<double>& values) {
    core::Json arr = core::Json::array();
    for (const double value : values) {
        arr.push_back(value);
    }
    return arr;
}

}  // namespace

Transform Transform::identity() {
    Transform t;
    t.m = {{{1.0, 0.0, 0.0, 0.0},
            {0.0, 1.0, 0.0, 0.0},
            {0.0, 0.0, 1.0, 0.0},
            {0.0, 0.0, 0.0, 1.0}}};
    return t;
}

Transform Transform::fromTranslation(const Vec3& translation) {
    Transform t = identity();
    t.m[0][3] = translation.x;
    t.m[1][3] = translation.y;
    t.m[2][3] = translation.z;
    return t;
}

Transform Transform::fromAxisAngle(const Vec3& axis, double angleRad) {
    const double norm = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    Transform t = identity();
    if (norm <= 0.0 || !std::isfinite(norm)) {
        return t;
    }
    const double ux = axis.x / norm;
    const double uy = axis.y / norm;
    const double uz = axis.z / norm;
    const double c = std::cos(angleRad);
    const double s = std::sin(angleRad);
    const double ic = 1.0 - c;
    t.m[0][0] = c + ux * ux * ic;
    t.m[0][1] = ux * uy * ic - uz * s;
    t.m[0][2] = ux * uz * ic + uy * s;
    t.m[1][0] = uy * ux * ic + uz * s;
    t.m[1][1] = c + uy * uy * ic;
    t.m[1][2] = uy * uz * ic - ux * s;
    t.m[2][0] = uz * ux * ic - uy * s;
    t.m[2][1] = uz * uy * ic + ux * s;
    t.m[2][2] = c + uz * uz * ic;
    return t;
}

Transform Transform::fromRotationTranslation(const std::array<std::array<double, 3>, 3>& rotation,
                                             const Vec3& translation) {
    Transform t = identity();
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            t.m[r][c] = rotation[static_cast<size_t>(r)][static_cast<size_t>(c)];
        }
    }
    t.m[0][3] = translation.x;
    t.m[1][3] = translation.y;
    t.m[2][3] = translation.z;
    return t;
}

Transform Transform::compose(const Transform& next) const {
    Transform out;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) {
                sum += m[static_cast<size_t>(r)][static_cast<size_t>(k)] *
                       next.m[static_cast<size_t>(k)][static_cast<size_t>(c)];
            }
            out.m[r][c] = sum;
        }
    }
    return out;
}

Transform Transform::inverse() const {
    // Rigid transform: inverse = [R^T, -R^T t; 0 1].
    Transform out;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out.m[static_cast<size_t>(r)][static_cast<size_t>(c)] =
                m[static_cast<size_t>(c)][static_cast<size_t>(r)];
        }
    }
    const double tx = m[0][3];
    const double ty = m[1][3];
    const double tz = m[2][3];
    out.m[0][3] = -(out.m[0][0] * tx + out.m[0][1] * ty + out.m[0][2] * tz);
    out.m[1][3] = -(out.m[1][0] * tx + out.m[1][1] * ty + out.m[1][2] * tz);
    out.m[2][3] = -(out.m[2][0] * tx + out.m[2][1] * ty + out.m[2][2] * tz);
    out.m[3][0] = 0.0;
    out.m[3][1] = 0.0;
    out.m[3][2] = 0.0;
    out.m[3][3] = 1.0;
    return out;
}

Vec3 Transform::transformPoint(const Vec3& point) const {
    return Vec3{m[0][0] * point.x + m[0][1] * point.y + m[0][2] * point.z + m[0][3],
                m[1][0] * point.x + m[1][1] * point.y + m[1][2] * point.z + m[1][3],
                m[2][0] * point.x + m[2][1] * point.y + m[2][2] * point.z + m[2][3]};
}

Vec3 Transform::rotationColumn(int column) const {
    return Vec3{m[0][static_cast<size_t>(column)], m[1][static_cast<size_t>(column)],
                m[2][static_cast<size_t>(column)]};
}

Vec3 Transform::translation() const { return Vec3{m[0][3], m[1][3], m[2][3]}; }

core::Json Transform::toJson() const {
    core::Json rows = core::Json::array();
    for (int r = 0; r < 4; ++r) {
        core::Json row = core::Json::array();
        for (int c = 0; c < 4; ++c) {
            row.push_back(round6(m[static_cast<size_t>(r)][static_cast<size_t>(c)]));
        }
        rows.push_back(row);
    }
    return rows;
}

Transform Transform::fromJson(const core::Json& json) {
    Transform out = identity();
    if (!json.is_array() || json.size() != 4) {
        return out;
    }
    for (size_t r = 0; r < 4; ++r) {
        const core::Json& row = json[r];
        if (!row.is_array() || row.size() != 4) {
            return identity();
        }
        for (size_t c = 0; c < 4; ++c) {
            out.m[r][c] = row[c].is_number() ? row[c].get<double>() : 0.0;
        }
    }
    return out;
}

core::Json Pose::toJson() const {
    core::Json rows = core::Json::array();
    for (int r = 0; r < 3; ++r) {
        core::Json row = core::Json::array();
        for (int c = 0; c < 3; ++c) {
            row.push_back(round6(rotation[static_cast<size_t>(r)][static_cast<size_t>(c)]));
        }
        rows.push_back(row);
    }
    return core::Json{{"position",
                       core::Json{{"x", round6(positionM.x)},
                                  {"y", round6(positionM.y)},
                                  {"z", round6(positionM.z)}}},
                      {"rotation", rows}};
}

Pose Pose::fromJson(const core::Json& json) {
    Pose out;
    if (json.is_object()) {
        out.positionM = vecFromJson(json.value("position", core::Json::object()));
        if (json.contains("rotation") && json["rotation"].is_array() &&
            json["rotation"].size() == 3) {
            for (size_t r = 0; r < 3; ++r) {
                const core::Json& row = json["rotation"][r];
                if (row.is_array() && row.size() == 3) {
                    for (size_t c = 0; c < 3; ++c) {
                        out.rotation[r][c] = row[c].is_number() ? row[c].get<double>() : 0.0;
                    }
                }
            }
        }
    }
    return out;
}

Pose Pose::fromTransform(const Transform& transform) {
    Pose out;
    out.positionM = transform.translation();
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out.rotation[static_cast<size_t>(r)][static_cast<size_t>(c)] =
                transform.m[static_cast<size_t>(r)][static_cast<size_t>(c)];
        }
    }
    return out;
}

std::string toString(JointType type) {
    switch (type) {
        case JointType::Revolute:
            return "revolute";
        case JointType::Prismatic:
            return "prismatic";
        case JointType::Fixed:
            return "fixed";
    }
    return "revolute";
}

JointType jointTypeFromString(const std::string& value) {
    if (value == "revolute") {
        return JointType::Revolute;
    }
    if (value == "prismatic") {
        return JointType::Prismatic;
    }
    if (value == "fixed") {
        return JointType::Fixed;
    }
    throw std::invalid_argument("Unknown joint type '" + value + "'");
}

core::Json JointLimit::toJson() const {
    core::Json out = core::Json::object();
    out["specified"] = specified;
    if (specified) {
        out["lower"] = round6(lower);
        out["upper"] = round6(upper);
        out["velocity_max"] = round6(velocityMax);
    }
    return out;
}

JointLimit JointLimit::fromJson(const core::Json& json) {
    JointLimit out;
    if (json.is_object()) {
        out.specified = json.value("specified", false);
        out.lower = json.value("lower", 0.0);
        out.upper = json.value("upper", 0.0);
        out.velocityMax = json.value("velocity_max", 0.0);
    }
    return out;
}

core::Json Link::toJson() const {
    return core::Json{{"name", name}, {"length_m", round6(lengthM)}};
}

Link Link::fromJson(const core::Json& json) {
    Link out;
    if (json.is_object()) {
        out.name = json.value("name", "");
        out.lengthM = json.value("length_m", 0.0);
    }
    return out;
}

core::Json Joint::toJson() const {
    return core::Json{{"name", name},
                      {"type", toString(type)},
                      {"parent_link", parentLink},
                      {"child_link", childLink},
                      {"axis", axis.toJson()},
                      {"origin_xyz", originXYZ.toJson()},
                      {"origin_rpy", originRPYRad.toJson()},
                      {"limit", limit.toJson()}};
}

Joint Joint::fromJson(const core::Json& json) {
    Joint out;
    if (json.is_object()) {
        out.name = json.value("name", "");
        const std::string type = json.value("type", "revolute");
        try {
            out.type = jointTypeFromString(type);
        } catch (const std::exception&) {
            out.type = JointType::Revolute;  // reported by validators
        }
        out.parentLink = json.value("parent_link", "");
        out.childLink = json.value("child_link", "");
        out.axis = vecFromJson(json.value("axis", core::Json{{"x", 0.0}, {"y", 0.0}, {"z", 1.0}}));
        out.originXYZ = vecFromJson(json.value("origin_xyz", core::Json::object()));
        out.originRPYRad = vecFromJson(json.value("origin_rpy", core::Json::object()));
        out.limit = JointLimit::fromJson(json.value("limit", core::Json::object()));
    }
    return out;
}

core::Json JointState::toJson() const {
    core::Json pos = core::Json::object();
    for (const auto& [name, value] : positions) {
        pos[name] = round6(value);
    }
    core::Json vel = core::Json::object();
    for (const auto& [name, value] : velocities) {
        vel[name] = round6(value);
    }
    return core::Json{{"positions", pos}, {"velocities", vel}};
}

JointState JointState::fromJson(const core::Json& json) {
    JointState out;
    if (json.is_object()) {
        if (json.contains("positions") && json["positions"].is_object()) {
            for (auto it = json["positions"].begin(); it != json["positions"].end(); ++it) {
                if (it.value().is_number()) {
                    out.positions[it.key()] = it.value().get<double>();
                }
            }
        }
        if (json.contains("velocities") && json["velocities"].is_object()) {
            for (auto it = json["velocities"].begin(); it != json["velocities"].end(); ++it) {
                if (it.value().is_number()) {
                    out.velocities[it.key()] = it.value().get<double>();
                }
            }
        }
    }
    return out;
}

double JointState::positionOf(const std::string& jointName, bool& found) const {
    const auto it = positions.find(jointName);
    found = it != positions.end();
    return found ? it->second : 0.0;
}

core::Json EndEffector::toJson() const {
    return core::Json{{"name", name}, {"parent_link", parentLink}, {"origin_xyz", originXYZ.toJson()}};
}

EndEffector EndEffector::fromJson(const core::Json& json) {
    EndEffector out;
    if (json.is_object()) {
        out.name = json.value("name", "tool0");
        out.parentLink = json.value("parent_link", "");
        out.originXYZ = vecFromJson(json.value("origin_xyz", core::Json::object()));
    }
    return out;
}

core::Json ControlInput::toJson() const {
    core::Json pos = core::Json::object();
    for (const auto& [name, value] : jointPositions) {
        pos[name] = round6(value);
    }
    core::Json vel = core::Json::object();
    for (const auto& [name, value] : jointVelocities) {
        vel[name] = round6(value);
    }
    core::Json out = core::Json::object();
    out["joint_positions"] = pos;
    out["joint_velocities"] = vel;
    if (hasTargetPosition) {
        out["target_position_m"] = targetPositionM.toJson();
    }
    return out;
}

ControlInput ControlInput::fromJson(const core::Json& json) {
    ControlInput out;
    if (json.is_object()) {
        if (json.contains("joint_positions")) {
            out.jointPositions = JointState::fromJson(core::Json{{"positions", json["joint_positions"]}})
                                     .positions;
        }
        if (json.contains("joint_velocities")) {
            out.jointVelocities =
                JointState::fromJson(core::Json{{"velocities", json["joint_velocities"]}}).velocities;
        }
        if (json.contains("target_position_m")) {
            out.targetPositionM = vecFromJson(json["target_position_m"]);
            out.hasTargetPosition = true;
        }
    }
    return out;
}

long long Trajectory::sampleCount() const {
    return timestampsS.empty() ? 0 : static_cast<long long>(timestampsS.size());
}

core::Json Trajectory::toJson() const {
    core::Json samples = core::Json::array();
    const size_t n = timestampsS.size();
    for (size_t i = 0; i < n; ++i) {
        core::Json pos = core::Json::array();
        core::Json vel = core::Json::array();
        for (size_t k = 0; k < positions.size(); ++k) {
            pos.push_back(round6(positions[k][i]));
            vel.push_back(round6(velocities[k][i]));
        }
        samples.push_back(core::Json{{"t", round6(timestampsS[i])},
                                     {"positions", pos},
                                     {"velocities", vel}});
    }
    core::Json order = core::Json::array();
    for (const std::string& name : jointOrder) {
        order.push_back(name);
    }
    return core::Json{{"joint_order", order},
                      {"duration_s", round6(durationS)},
                      {"dt", round6(dt)},
                      {"sample_count", sampleCount()},
                      {"samples", samples}};
}

Trajectory Trajectory::fromJson(const core::Json& json) {
    Trajectory out;
    if (!json.is_object()) {
        return out;
    }
    if (json.contains("joint_order") && json["joint_order"].is_array()) {
        for (const auto& name : json["joint_order"]) {
            if (name.is_string()) {
                out.jointOrder.push_back(name.get<std::string>());
            }
        }
    }
    out.durationS = json.value("duration_s", 0.0);
    out.dt = json.value("dt", 0.0);
    if (json.contains("samples") && json["samples"].is_array()) {
        const size_t joints = out.jointOrder.size();
        out.positions.assign(joints, std::vector<double>{});
        out.velocities.assign(joints, std::vector<double>{});
        for (const auto& sample : json["samples"]) {
            if (!sample.is_object()) {
                continue;
            }
            out.timestampsS.push_back(sample.value("t", 0.0));
            const core::Json& pos = sample.value("positions", core::Json::array());
            const core::Json& vel = sample.value("velocities", core::Json::array());
            for (size_t k = 0; k < joints; ++k) {
                out.positions[k].push_back(k < pos.size() && pos[k].is_number()
                                               ? pos[k].get<double>()
                                               : 0.0);
                out.velocities[k].push_back(k < vel.size() && vel[k].is_number()
                                                ? vel[k].get<double>()
                                                : 0.0);
            }
        }
    }
    return out;
}

const Link* RobotModel::findLink(const std::string& linkName) const {
    for (const Link& link : links) {
        if (link.name == linkName) {
            return &link;
        }
    }
    return nullptr;
}

const Joint* RobotModel::findJoint(const std::string& jointName) const {
    for (const Joint& joint : joints) {
        if (joint.name == jointName) {
            return &joint;
        }
    }
    return nullptr;
}

core::Json RobotModel::toJson() const {
    core::Json linksJson = core::Json::array();
    for (const Link& link : links) {
        linksJson.push_back(link.toJson());
    }
    core::Json jointsJson = core::Json::array();
    for (const Joint& joint : joints) {
        jointsJson.push_back(joint.toJson());
    }
    return core::Json{{"name", name}, {"base_frame", baseFrame},
                      {"links", linksJson}, {"joints", jointsJson}};
}

RobotModel RobotModel::fromJson(const core::Json& json) {
    RobotModel out;
    if (!json.is_object()) {
        return out;
    }
    out.name = json.value("name", "");
    out.baseFrame = json.value("base_frame", "world");
    if (json.contains("links") && json["links"].is_array()) {
        for (const auto& link : json["links"]) {
            out.links.push_back(Link::fromJson(link));
        }
    }
    if (json.contains("joints") && json["joints"].is_array()) {
        for (const auto& joint : json["joints"]) {
            out.joints.push_back(Joint::fromJson(joint));
        }
    }
    return out;
}

void RobotProject::syncJointLimits() {
    for (Joint& joint : model.joints) {
        const auto it = jointLimits.find(joint.name);
        if (it != jointLimits.end() && it->second.specified && !joint.limit.specified) {
            joint.limit = it->second;
        }
    }
}

core::Json RobotProject::toJson() const {
    // joint_limits is exported from the joints (derived view) so a
    // single source of truth is serialized.
    core::Json limits = core::Json::object();
    for (const Joint& joint : model.joints) {
        if (joint.limit.specified) {
            limits[joint.name] = joint.limit.toJson();
        }
    }
    return core::Json{{"project_id", projectId},
                      {"robot_name", robotName},
                      {"model", model.toJson()},
                      {"joint_limits", limits},
                      {"base_frame", baseFrame},
                      {"end_effector", endEffector.toJson()},
                      {"initial_state", initialState.toJson()},
                      {"trajectory", trajectory.toJson()},
                      {"metadata", metadata}};
}

RobotProject RobotProject::fromJson(const core::Json& json) {
    RobotProject out;
    if (!json.is_object()) {
        return out;
    }
    out.projectId = json.value("project_id", "");
    out.robotName = json.value("robot_name", "");
    out.model = RobotModel::fromJson(json.value("model", core::Json::object()));
    out.baseFrame = json.value("base_frame", out.model.baseFrame.empty() ? "world"
                                                                         : out.model.baseFrame);
    out.model.baseFrame = out.baseFrame;
    if (json.contains("joint_limits") && json["joint_limits"].is_object()) {
        for (auto it = json["joint_limits"].begin(); it != json["joint_limits"].end(); ++it) {
            out.jointLimits[it.key()] = JointLimit::fromJson(it.value());
        }
    }
    out.endEffector = EndEffector::fromJson(json.value("end_effector", core::Json::object()));
    out.initialState = JointState::fromJson(json.value("initial_state", core::Json::object()));
    out.trajectory = Trajectory::fromJson(json.value("trajectory", core::Json::object()));
    out.metadata = json.value("metadata", core::Json::object());
    out.syncJointLimits();
    return out;
}

core::Json RobotResult::toJson() const {
    core::Json frames = core::Json::object();
    for (const auto& [name, pose] : framePoses) {
        frames[name] = pose.toJson();
    }
    core::Json order = core::Json::array();
    for (const std::string& name : trajectory.jointOrder) {
        order.push_back(name);
    }
    return core::Json{{"project_id", projectId},
                      {"operation", operation},
                      {"link_count", linkCount},
                      {"joint_count", jointCount},
                      {"frames", frames},
                      {"end_effector", endEffectorPose.toJson()},
                      {"ik",
                       core::Json{{"converged", converged},
                                  {"iterations", iterations},
                                  {"final_error_m", round6(finalErrorM)},
                                  {"position_only", positionOnly}}},
                      {"solution", solution.toJson()},
                      {"trajectory", trajectory.toJson()},
                      {"checks", checks},
                      {"summary", summary}};
}

RobotResult RobotResult::fromJson(const core::Json& json) {
    RobotResult out;
    if (!json.is_object()) {
        return out;
    }
    out.projectId = json.value("project_id", "");
    out.operation = json.value("operation", "");
    out.linkCount = json.value("link_count", 0LL);
    out.jointCount = json.value("joint_count", 0LL);
    if (json.contains("frames") && json["frames"].is_object()) {
        for (auto it = json["frames"].begin(); it != json["frames"].end(); ++it) {
            out.framePoses[it.key()] = Pose::fromJson(it.value());
        }
    }
    out.endEffectorPose = Pose::fromJson(json.value("end_effector", core::Json::object()));
    if (json.contains("ik") && json["ik"].is_object()) {
        const core::Json& ik = json["ik"];
        out.converged = ik.value("converged", false);
        out.iterations = ik.value("iterations", 0LL);
        out.finalErrorM = ik.value("final_error_m", 0.0);
        out.positionOnly = ik.value("position_only", true);
    }
    out.solution = JointState::fromJson(json.value("solution", core::Json::object()));
    out.trajectory = Trajectory::fromJson(json.value("trajectory", core::Json::object()));
    out.checks = json.value("checks", core::Json::object());
    out.summary = json.value("summary", core::Json::object());
    return out;
}

}  // namespace trinity::robotics
