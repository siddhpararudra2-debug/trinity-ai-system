#include "trinity/engines/RoboticsEngine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include "trinity/artifacts/Checksum.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/robotics/Exports.hpp"
#include "trinity/robotics/Kinematics.hpp"
#include "trinity/robotics/Validators.hpp"
#include "trinity/simulation/Integrator.hpp"

namespace trinity::engines {
namespace fs = std::filesystem;

namespace {

constexpr int kMaxJoints = 12;
constexpr double kMaxLinkMeters = 100.0;
constexpr double kMaxAngleRad = 12.6;  // ~4*pi
constexpr double kMaxJointValueRad = 1.0e6;
constexpr double kDefaultRadiusM = 0.02;
constexpr double kMaxDurationS = 3600.0;
constexpr long long kMaxTrajSteps = 1000000;

double round6(double value) { return std::round(value * 1000000.0) / 1000000.0; }

struct DhLink {
    double a = 0.0;
    double alpha = 0.0;
    double d = 0.0;
    double thetaOffset = 0.0;
    std::string name;
};

core::Json defaultChainJson() {
    return core::Json::array(
        {core::Json{{"a", 1.0},
                    {"alpha", 0.0},
                    {"d", 0.0},
                    {"theta_offset", 0.0},
                    {"name", "link1"}},
         core::Json{{"a", 1.0},
                    {"alpha", 0.0},
                    {"d", 0.0},
                    {"theta_offset", 0.0},
                    {"name", "link2"}}});
}

double requireFinite(const core::Json& value, const std::string& key,
                     const std::string& operation) {
    if (!value.is_number()) {
        throw core::RequestValidationError("Parameter '" + key + "' must be numeric",
                                           {{"operation", operation}, {"param", key}},
                                           "engines");
    }
    const double raw = value.get<double>();
    if (!std::isfinite(raw)) {
        throw core::RequestValidationError("Parameter '" + key + "' must be finite",
                                           {{"param", key}}, "engines");
    }
    return raw;
}

std::vector<DhLink> parseChain(const core::Json& params, const std::string& operation,
                               std::string& sourceOut) {
    core::Json chain = defaultChainJson();
    sourceOut = "engine_default_2link";
    if (params.contains("dh_params") && !params["dh_params"].is_null()) {
        if (!params["dh_params"].is_array() || params["dh_params"].empty()) {
            throw core::RequestValidationError("dh_params must be a non-empty array",
                                               {{"operation", operation},
                                                {"param", "dh_params"}},
                                               "engines");
        }
        chain = params["dh_params"];
        sourceOut = "parameters";
    }
    if (!chain.is_array() || chain.empty() || chain.size() > kMaxJoints) {
        throw core::RequestValidationError(
            "dh_params must contain between 1 and " + std::to_string(kMaxJoints) + " rows",
            {{"param", "dh_params"}}, "engines");
    }
    std::vector<DhLink> links;
    links.reserve(chain.size());
    for (size_t i = 0; i < chain.size(); ++i) {
        const core::Json& row = chain[i];
        if (!row.is_object()) {
            throw core::RequestValidationError("dh_params[" + std::to_string(i) +
                                                   "] must be an object",
                                               {{"param", "dh_params"}}, "engines");
        }
        for (const std::string& key : {"a", "alpha", "d"}) {
            if (!row.contains(key)) {
                throw core::RequestValidationError("dh_params[" + std::to_string(i) +
                                                       "] is missing '" + key + "'",
                                                   {{"param", "dh_params"}}, "engines");
            }
        }
        DhLink link;
        link.a = requireFinite(row["a"], "dh_params.a", operation);
        link.alpha = requireFinite(row["alpha"], "dh_params.alpha", operation);
        link.d = requireFinite(row["d"], "dh_params.d", operation);
        if (row.contains("theta_offset") && !row["theta_offset"].is_null()) {
            link.thetaOffset =
                requireFinite(row["theta_offset"], "dh_params.theta_offset", operation);
        }
        if (std::fabs(link.a) > kMaxLinkMeters || std::fabs(link.d) > kMaxLinkMeters) {
            throw core::RequestValidationError(
                "dh_params link lengths must be within ±100 m",
                {{"param", "dh_params"}}, "engines");
        }
        if (std::fabs(link.alpha) > kMaxAngleRad ||
            std::fabs(link.thetaOffset) > kMaxAngleRad) {
            throw core::RequestValidationError("dh_params angles must be within ±12.6 rad",
                                               {{"param", "dh_params"}}, "engines");
        }
        link.name = row.value("name", "link_" + std::to_string(i + 1));
        links.push_back(link);
    }
    return links;
}

std::vector<double> parseJointArray(const core::Json& params, const std::string& key,
                                    const std::string& operation, bool required,
                                    size_t expectedLength) {
    if (!params.contains(key) || params[key].is_null()) {
        if (required) {
            throw core::RequestValidationError(
                "Operation '" + operation + "' requires array '" + key + "'",
                {{"operation", operation}, {"param", key}}, "engines");
        }
        return std::vector<double>(expectedLength, 0.0);
    }
    const core::Json& arr = params[key];
    if (!arr.is_array() || arr.empty() || arr.size() > kMaxJoints) {
        throw core::RequestValidationError(
            key + " must be an array of 1 to " + std::to_string(kMaxJoints) + " numbers",
            {{"operation", operation}, {"param", key}}, "engines");
    }
    if (expectedLength > 0 && arr.size() != expectedLength) {
        throw core::RequestValidationError(
            key + " must have exactly " + std::to_string(expectedLength) + " entries",
            {{"param", key}, {"expected", static_cast<long long>(expectedLength)}},
            "engines");
    }
    std::vector<double> values;
    values.reserve(arr.size());
    for (size_t i = 0; i < arr.size(); ++i) {
        const double value =
            requireFinite(arr[i], key + "[" + std::to_string(i) + "]", operation);
        if (std::fabs(value) > kMaxJointValueRad) {
            throw core::RequestValidationError(key + " values must be within ±1e6",
                                               {{"param", key}}, "engines");
        }
        values.push_back(value);
    }
    return values;
}

core::Json chainToJson(const std::vector<DhLink>& links) {
    core::Json arr = core::Json::array();
    for (const DhLink& link : links) {
        arr.push_back(core::Json{{"a", link.a},
                                 {"alpha", link.alpha},
                                 {"d", link.d},
                                 {"theta_offset", link.thetaOffset},
                                 {"name", link.name}});
    }
    return arr;
}

using Mat4 = std::array<std::array<double, 4>, 4>;

Mat4 identity4() {
    Mat4 m{};
    for (int r = 0; r < 4; ++r) {
        m[r][r] = 1.0;
    }
    return m;
}

Mat4 mul4(const Mat4& left, const Mat4& right) {
    Mat4 out{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) {
                sum += left[r][k] * right[k][c];
            }
            out[r][c] = sum;
        }
    }
    return out;
}

Mat4 dhMatrix(const DhLink& link, double theta) {
    const double ct = std::cos(theta);
    const double st = std::sin(theta);
    const double ca = std::cos(link.alpha);
    const double sa = std::sin(link.alpha);
    Mat4 m = identity4();
    m[0][0] = ct;
    m[0][1] = -st * ca;
    m[0][2] = st * sa;
    m[0][3] = link.a * ct;
    m[1][0] = st;
    m[1][1] = ct * ca;
    m[1][2] = -ct * sa;
    m[1][3] = link.a * st;
    m[2][1] = sa;
    m[2][2] = ca;
    m[2][3] = link.d;
    return m;
}

// Fixed joint-origin matrices for the URDF export. The DH chain factors
// exactly as O_1·Rz(q_0)·O_2·Rz(q_1)…·O_n·Rz(q_{n-1})·B_{n-1} with
// H_i = Trans(0,0,d_i)·Rot(z,thetaoff_i), B_i = Trans(a_i,0,0)·Rot(x,alpha_i),
// O_1 = H_0, O_{i+1} = B_{i-1}·H_i.
Mat4 trans(double x, double y, double z) {
    Mat4 m = identity4();
    m[0][3] = x;
    m[1][3] = y;
    m[2][3] = z;
    return m;
}

Mat4 rotZ(double angle) {
    Mat4 m = identity4();
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    m[0][0] = c;
    m[0][1] = -s;
    m[1][0] = s;
    m[1][1] = c;
    return m;
}

Mat4 rotX(double angle) {
    Mat4 m = identity4();
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    m[1][1] = c;
    m[1][2] = -s;
    m[2][1] = s;
    m[2][2] = c;
    return m;
}

Mat4 hMatrix(const DhLink& link) {
    return mul4(trans(0.0, 0.0, link.d), rotZ(link.thetaOffset));
}

Mat4 bMatrix(const DhLink& link) {
    return mul4(trans(link.a, 0.0, 0.0), rotX(link.alpha));
}

// URDF origin rpy uses fixed-axis Rz(yaw)*Ry(pitch)*Rx(roll).
void matToRpy(const Mat4& m, double& roll, double& pitch, double& yaw) {
    const double r20 = std::clamp(m[2][0], -1.0, 1.0);
    pitch = std::asin(-r20);
    const double cp = std::cos(pitch);
    if (std::fabs(cp) < 1e-9) {
        roll = 0.0;
        yaw = std::atan2(-m[0][1], m[1][1]);
    } else {
        roll = std::atan2(m[2][1], m[2][2]);
        yaw = std::atan2(m[1][0], m[0][0]);
    }
}

std::string fmt(double value) {
    std::ostringstream stream;
    stream << std::setprecision(9) << round6(value);
    return stream.str();
}

std::string sanitizeName(const std::string& raw, const std::string& fallback) {
    std::string out;
    out.reserve(raw.size());
    for (const char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            out.push_back(c);
        }
    }
    if (out.empty()) {
        return fallback;
    }
    if (std::isdigit(static_cast<unsigned char>(out.front()))) {
        out.insert(out.begin(), '_');
    }
    return out;
}

std::string originXml(const Mat4& origin) {
    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
    matToRpy(origin, roll, pitch, yaw);
    return "<origin xyz=\"" + fmt(origin[0][3]) + " " + fmt(origin[1][3]) + " " +
           fmt(origin[2][3]) + "\" rpy=\"" + fmt(roll) + " " + fmt(pitch) + " " +
           fmt(yaw) + "\"/>";
}

// ---- Joint/link IR parameter helpers (SI inside; mm/deg accepted at
// the boundary and normalized here). ----

bool hasKey(const core::Json& params, const std::string& key) {
    return params.contains(key) && !params[key].is_null();
}

double optionalNumber(const core::Json& params, const std::string& key, double fallback) {
    if (hasKey(params, key) && params[key].is_number()) {
        return params[key].get<double>();
    }
    return fallback;
}

/// Length in meters from `<mKey>` (meters) or `<mmKey>` (millimeters).
double lengthMeters(const core::Json& params, const std::string& mKey, const std::string& mmKey,
                    const std::string& operation, bool required, bool& found) {
    found = false;
    if (hasKey(params, mKey)) {
        found = true;
        const double value = requireFinite(params[mKey], mKey, operation);
        if (std::fabs(value) > robotics::kMaxLinkLengthM) {
            throw core::RequestValidationError(mKey + " must be within ±" +
                                                   std::to_string(robotics::kMaxLinkLengthM) + " m",
                                               {{"operation", operation}, {"param", mKey}},
                                               "engines");
        }
        return value;
    }
    if (hasKey(params, mmKey)) {
        found = true;
        const double value = requireFinite(params[mmKey], mmKey, operation) * 0.001;
        if (std::fabs(value) > robotics::kMaxLinkLengthM) {
            throw core::RequestValidationError(
                mmKey + " must be within ±" +
                    std::to_string(robotics::kMaxLinkLengthM * 1000.0) + " mm",
                {{"operation", operation}, {"param", mmKey}}, "engines");
        }
        return value;
    }
    if (required) {
        throw core::RequestValidationError(
            "Operation '" + operation + "' requires '" + mKey + "' (or '" + mmKey + "')",
            {{"operation", operation}, {"param", mKey}}, "engines");
    }
    return 0.0;
}

robotics::Vec3 vec3Param(const core::Json& params, const std::string& mKey, const std::string& mmKey,
               const std::string& operation, bool required, bool& found) {
    found = false;
    auto read = [&](const std::string& key, double scale) -> robotics::Vec3 {
        const core::Json& value = params[key];
        if (value.is_array()) {
            if (value.size() != 3) {
                throw core::RequestValidationError(key + " must be an array of 3 numbers",
                                                   {{"operation", operation}, {"param", key}},
                                                   "engines");
            }
            robotics::Vec3 out;
            out.x = requireFinite(value[0], key + "[0]", operation) * scale;
            out.y = requireFinite(value[1], key + "[1]", operation) * scale;
            out.z = requireFinite(value[2], key + "[2]", operation) * scale;
            return out;
        }
        if (value.is_object()) {
            for (const std::string& component : {"x", "y", "z"}) {
                if (!value.contains(component)) {
                    throw core::RequestValidationError(key + " object requires x, y, and z",
                                                       {{"operation", operation},
                                                        {"param", key}},
                                                       "engines");
                }
            }
            robotics::Vec3 out;
            out.x = requireFinite(value["x"], key + ".x", operation) * scale;
            out.y = requireFinite(value["y"], key + ".y", operation) * scale;
            out.z = requireFinite(value["z"], key + ".z", operation) * scale;
            return out;
        }
        if (value.is_string()) {
            const std::string text = value.get<std::string>();
            robotics::Vec3 out;
            if (text == "x") {
                out = {1.0, 0.0, 0.0};
            } else if (text == "y") {
                out = {0.0, 1.0, 0.0};
            } else if (text == "z") {
                out = {0.0, 0.0, 1.0};
            } else {
                throw core::RequestValidationError(key + " must be [x,y,z] or 'x'|'y'|'z'",
                                                   {{"operation", operation}, {"param", key}},
                                                   "engines");
            }
            return out;
        }
        throw core::RequestValidationError(
            key + " must be [x,y,z], {x,y,z}, or 'x'|'y'|'z'",
            {{"operation", operation}, {"param", key}}, "engines");
    };
    if (hasKey(params, mKey)) {
        found = true;
        return read(mKey, 1.0);
    }
    if (hasKey(params, mmKey)) {
        found = true;
        return read(mmKey, 0.001);
    }
    if (required) {
        throw core::RequestValidationError(
            "Operation '" + operation + "' requires '" + mKey + "' (or '" + mmKey + "')",
            {{"operation", operation}, {"param", mKey}}, "engines");
    }
    return robotics::Vec3{};
}

/// IK target: `target_xyz_m` / `target_xyz_mm` arrays, or scalars
/// `target_x_m|target_y_m|target_z_m` (mm variants accepted).
robotics::Vec3 targetParam(const core::Json& params, const std::string& operation) {
    bool found = false;
    const robotics::Vec3 arrayTarget =
        vec3Param(params, "target_xyz_m", "target_xyz_mm", operation, false, found);
    if (found) {
        return arrayTarget;
    }
    const bool anyScalar = hasKey(params, "target_x_m") || hasKey(params, "target_x_mm") ||
                           hasKey(params, "target_y_m") || hasKey(params, "target_y_mm") ||
                           hasKey(params, "target_z_m") || hasKey(params, "target_z_mm");
    if (!anyScalar) {
        throw core::RequestValidationError(
            "Operation '" + operation +
                "' requires a target position: target_xyz_m [x,y,z] (or target_x_m/y_m/z_m, "
                "mm variants accepted)",
            {{"operation", operation}, {"param", "target_xyz_m"}}, "engines");
    }
    robotics::Vec3 target;
    target.x = hasKey(params, "target_x_m")
                   ? requireFinite(params["target_x_m"], "target_x_m", operation)
                   : (hasKey(params, "target_x_mm")
                          ? requireFinite(params["target_x_mm"], "target_x_mm", operation) * 0.001
                          : 0.0);
    target.y = hasKey(params, "target_y_m")
                   ? requireFinite(params["target_y_m"], "target_y_m", operation)
                   : (hasKey(params, "target_y_mm")
                          ? requireFinite(params["target_y_mm"], "target_y_mm", operation) * 0.001
                          : 0.0);
    target.z = hasKey(params, "target_z_m")
                   ? requireFinite(params["target_z_m"], "target_z_m", operation)
                   : (hasKey(params, "target_z_mm")
                          ? requireFinite(params["target_z_mm"], "target_z_mm", operation) * 0.001
                          : 0.0);
    return target;
}

/// Optional joint limit from `limit_lower`/`limit_upper` (canonical:
/// rad for revolute, m for prismatic) with `limit_lower_deg` /
/// `limit_upper_deg` (revolute) and `_mm` (prismatic) aliases.
robotics::JointLimit limitParam(const core::Json& params, robotics::JointType type,
                                const std::string& operation) {
    robotics::JointLimit limit;
    const std::string degreeSuffix = type == robotics::JointType::Prismatic ? "_mm" : "_deg";
    const bool hasLower = hasKey(params, "limit_lower") || hasKey(params, "limit_lower" + degreeSuffix);
    const bool hasUpper = hasKey(params, "limit_upper") || hasKey(params, "limit_upper" + degreeSuffix);
    if (!hasLower && !hasUpper) {
        return limit;  // unspecified → unlimited
    }
    if (!hasLower || !hasUpper) {
        throw core::RequestValidationError(
            "Joint limits require both limit_lower and limit_upper",
            {{"operation", operation}, {"param", "limit_lower"}}, "engines");
    }
    limit.specified = true;
    const auto read = [&](const char* base) -> double {
        const std::string canonical = std::string(base);
        const std::string aliased = std::string(base) + degreeSuffix;
        if (hasKey(params, canonical)) {
            return requireFinite(params[canonical], canonical, operation);
        }
        const double raw = requireFinite(params[aliased], aliased, operation);
        return type == robotics::JointType::Prismatic ? raw * 0.001 : raw * (3.14159265358979323846 / 180.0);
    };
    limit.lower = read("limit_lower");
    limit.upper = read("limit_upper");
    limit.velocityMax = optionalNumber(params, "limit_velocity_max", 0.0);
    return limit;
}

}  // namespace

RoboticsEngine::RoboticsEngine() {
    name_ = "robotics";
    version_ = "0.1.0";
    capabilities_ = {"describe",         "forward_kinematics", "plan_trajectory",
                     "export_urdf",      "create_robot",       "add_link",
                     "add_joint",        "set_joint_state",    "compute_forward_kinematics",
                     "inverse_kinematics", "generate_trajectory", "validate_robot"};
}

EngineResult RoboticsEngine::executeForwardKinematics(const EngineRequest& request) {
    std::string chainSource;
    const std::vector<DhLink> links =
        parseChain(request.parameters, "forward_kinematics", chainSource);
    const bool anglesGiven = request.parameters.contains("joint_angles") &&
                             !request.parameters["joint_angles"].is_null();
    const std::vector<double> angles = parseJointArray(
        request.parameters, "joint_angles", "forward_kinematics", false, links.size());

    Mat4 total = identity4();
    core::Json frames = core::Json::array();
    for (size_t i = 0; i < links.size(); ++i) {
        total = mul4(total, dhMatrix(links[i], links[i].thetaOffset + angles[i]));
        frames.push_back(core::Json{
            {"index", static_cast<long long>(i + 1)},
            {"name", links[i].name},
            {"position",
             core::Json{{"x", round6(total[0][3])},
                        {"y", round6(total[1][3])},
                        {"z", round6(total[2][3])}}}});
    }

    core::Json rotation = core::Json::array();
    for (int r = 0; r < 3; ++r) {
        core::Json row = core::Json::array();
        for (int c = 0; c < 3; ++c) {
            row.push_back(round6(total[r][c]));
        }
        rotation.push_back(row);
    }

    core::Json angleArray = core::Json::array();
    for (const double value : angles) {
        angleArray.push_back(round6(value));
    }

    core::Json data{
        {"chain_source", chainSource},
        {"joint_count", static_cast<long long>(links.size())},
        {"joint_angles", angleArray},
        {"angles_source", anglesGiven ? "parameters" : "default_zero"},
        {"units", core::Json{{"length", "m"}, {"angle", "rad"}}},
        {"end_effector",
         core::Json{{"position",
                     core::Json{{"x", round6(total[0][3])},
                                {"y", round6(total[1][3])},
                                {"z", round6(total[2][3])}}},
                    {"rotation", rotation}}},
        {"frames", frames}};

    lastChain_ = chainToJson(links);

    EngineResult out = successResult(request, data);
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "robotics forward kinematics",
        core::Json{{"joints", static_cast<long long>(links.size())},
                   {"chain_source", chainSource}});
    return out;
}

EngineResult RoboticsEngine::executePlanTrajectory(const EngineRequest& request) {
    const std::vector<double> start =
        parseJointArray(request.parameters, "joint_start", "plan_trajectory", true, 0);
    const std::vector<double> goal = parseJointArray(
        request.parameters, "joint_goal", "plan_trajectory", true, start.size());
    if (start.size() > static_cast<size_t>(kMaxJoints)) {
        throw core::RequestValidationError(
            "plan_trajectory supports at most " + std::to_string(kMaxJoints) + " joints",
            {{"param", "joint_start"}}, "engines");
    }

    double durationS = 2.0;
    if (request.parameters.contains("duration_s") &&
        !request.parameters["duration_s"].is_null()) {
        durationS = requireFinite(request.parameters["duration_s"], "duration_s",
                                  "plan_trajectory");
    }
    if (!(durationS > 0.0) || durationS > kMaxDurationS) {
        throw core::RequestValidationError("duration_s must be in (0, 3600]",
                                           {{"param", "duration_s"}}, "engines");
    }
    double dt = 0.01;
    if (request.parameters.contains("dt") && !request.parameters["dt"].is_null()) {
        dt = requireFinite(request.parameters["dt"], "dt", "plan_trajectory");
    }
    if (!(dt > 0.0) || dt > 10.0) {
        throw core::RequestValidationError("dt must be in (0, 10]",
                                           {{"param", "dt"}}, "engines");
    }
    const double rawSteps = durationS / dt;
    if (rawSteps > static_cast<double>(kMaxTrajSteps)) {
        throw core::RequestValidationError(
            "duration_s/dt exceeds " + std::to_string(kMaxTrajSteps) + " steps",
            {{"param", "duration_s"}}, "engines");
    }

    // One closed-form linear_motion integration per joint; identical
    // dt/duration keeps every joint on the same sample grid.
    std::vector<std::vector<double>> positions(start.size());
    std::vector<std::vector<double>> velocities(start.size());
    std::vector<double> times;
    long long sampleCount = 0;
    std::vector<double> finalPositions(start.size(), 0.0);
    for (size_t k = 0; k < start.size(); ++k) {
        simulation::SimulationProject project;
        project.type = "kinematics";
        project.model = "linear_motion";
        project.dt = dt;
        project.durationS = durationS;
        project.initial.position.x = start[k];
        project.initial.velocity.x = (goal[k] - start[k]) / durationS;
        project.initial.acceleration.x = 0.0;
        const simulation::IntegrateOutcome outcome = simulation::integrate(project);
        if (!outcome.success) {
            throw core::EngineExecutionError(
                "Joint " + std::to_string(k + 1) + " trajectory failed: " + outcome.error,
                {{"joint", static_cast<long long>(k + 1)}}, "engines");
        }
        const simulation::SimulationResult& result = outcome.result;
        if (k == 0) {
            sampleCount = result.sampleCount;
            times.reserve(result.samples.size());
            for (const simulation::SimulationState& state : result.samples) {
                times.push_back(state.t);
            }
        } else if (result.samples.size() != positions[0].size()) {
            throw core::EngineExecutionError("Joint sample grids diverged", {}, "engines");
        }
        positions[k].reserve(result.samples.size());
        velocities[k].reserve(result.samples.size());
        for (const simulation::SimulationState& state : result.samples) {
            positions[k].push_back(state.position.x);
            velocities[k].push_back(state.velocity.x);
        }
        finalPositions[k] = result.finalState.position.x;
    }

    bool startReached = true;
    bool goalReached = true;
    for (size_t k = 0; k < start.size(); ++k) {
        if (positions[k].empty() || std::fabs(positions[k].front() - start[k]) > 1e-6) {
            startReached = false;
        }
        if (std::fabs(finalPositions[k] - goal[k]) > 1e-6) {
            goalReached = false;
        }
    }

    core::Json trajectory = core::Json::array();
    const size_t n = times.empty() ? 0 : positions[0].size();
    for (size_t i = 0; i < n; ++i) {
        core::Json posArr = core::Json::array();
        core::Json velArr = core::Json::array();
        for (size_t k = 0; k < start.size(); ++k) {
            posArr.push_back(round6(positions[k][i]));
            velArr.push_back(round6(velocities[k][i]));
        }
        trajectory.push_back(core::Json{{"t", round6(times[i])},
                                        {"positions", posArr},
                                        {"velocities", velArr}});
    }

    core::Json startArr = core::Json::array();
    core::Json goalArr = core::Json::array();
    core::Json finalArr = core::Json::array();
    for (size_t k = 0; k < start.size(); ++k) {
        startArr.push_back(round6(start[k]));
        goalArr.push_back(round6(goal[k]));
        finalArr.push_back(round6(finalPositions[k]));
    }

    core::Json data{
        {"integrator", "simulation::integrate (closed_form)"},
        {"duration_s", durationS},
        {"dt", dt},
        {"sample_count", sampleCount},
        {"joint_count", static_cast<long long>(start.size())},
        {"joint_start", startArr},
        {"joint_goal", goalArr},
        {"final_positions", finalArr},
        {"start_reached", startReached},
        {"goal_reached", goalReached},
        {"trajectory", trajectory}};

    EngineResult out = successResult(request, data);
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "robotics trajectory planned",
        core::Json{{"joints", static_cast<long long>(start.size())},
                   {"samples", sampleCount},
                   {"goal_reached", goalReached}});
    return out;
}

EngineResult RoboticsEngine::executeExportUrdf(const EngineRequest& request) {
    std::string chainSource = "last_fk_chain";
    core::Json chainJson;
    if (request.parameters.contains("dh_params") &&
        !request.parameters["dh_params"].is_null()) {
        chainJson = request.parameters["dh_params"];
        chainSource = "parameters";
    } else if (lastChain_.is_array() && !lastChain_.empty()) {
        chainJson = lastChain_;
    } else {
        throw core::RequestValidationError(
            "export_urdf requires dh_params (no previous forward_kinematics chain)",
            {{"operation", "export_urdf"}, {"param", "dh_params"}}, "engines");
    }
    core::Json wrapper = core::Json{{"dh_params", chainJson}};
    // parseChain would overwrite chainSource (the wrapper always carries
    // dh_params); keep the explicit source computed above.
    std::string parsedSource;
    const std::vector<DhLink> links = parseChain(wrapper, "export_urdf", parsedSource);

    std::string robotName = "trinity_robot";
    if (request.parameters.contains("robot_name") &&
        request.parameters["robot_name"].is_string()) {
        robotName = sanitizeName(request.parameters["robot_name"].get<std::string>(),
                                 "trinity_robot");
    }
    double radius = kDefaultRadiusM;
    if (request.parameters.contains("radius_m") &&
        !request.parameters["radius_m"].is_null()) {
        radius = requireFinite(request.parameters["radius_m"], "radius_m", "export_urdf");
    }
    if (!(radius >= 0.001) || radius > 0.5) {
        throw core::RequestValidationError("radius_m must be in [0.001, 0.5]",
                                           {{"param", "radius_m"}}, "engines");
    }

    const size_t n = links.size();
    std::vector<Mat4> origins(n);
    origins[0] = hMatrix(links[0]);
    for (size_t i = 1; i < n; ++i) {
        origins[i] = mul4(bMatrix(links[i - 1]), hMatrix(links[i]));
    }
    const Mat4 toolOrigin = bMatrix(links[n - 1]);

    std::ostringstream urdf;
    urdf << "<?xml version=\"1.0\"?>\n";
    urdf << "<!-- Generated by Trinity RoboticsEngine: standard DH rows mapped to "
            "fixed joint origins (xyz/rpy) + revolute z axes + fixed tool joint. "
            "Geometry only; no dynamics. -->\n";
    urdf << "<robot name=\"" << robotName << "\">\n";
    urdf << "  <link name=\"base_link\">\n"
         << "    <visual><origin xyz=\"0 0 -0.02\"/><geometry>"
         << "<box size=\"0.04 0.04 0.04\"/></geometry></visual>\n"
         << "  </link>\n";
    for (size_t i = 0; i < n; ++i) {
        const std::string linkName = "link_" + std::to_string(i + 1);
        const double length = std::max(std::fabs(links[i].a), 0.02);
        urdf << "  <link name=\"" << linkName << "\">\n"
             << "    <visual><origin xyz=\"" << fmt(links[i].a / 2.0)
             << " 0 0\" rpy=\"0 1.57079632679 0\"/><geometry>"
             << "<cylinder radius=\"" << fmt(radius) << "\" length=\"" << fmt(length)
             << "\"/></geometry></visual>\n"
             << "  </link>\n";
    }
    urdf << "  <link name=\"tool0\">\n"
         << "    <visual><geometry><sphere radius=\"0.01\"/></geometry></visual>\n"
         << "  </link>\n";
    for (size_t i = 0; i < n; ++i) {
        const std::string parent = i == 0 ? "base_link" : "link_" + std::to_string(i);
        urdf << "  <joint name=\"joint_" << (i + 1) << "\" type=\"revolute\">\n"
             << "    <parent link=\"" << parent << "\"/>\n"
             << "    <child link=\"link_" << (i + 1) << "\"/>\n"
             << "    " << originXml(origins[i]) << "\n"
             << "    <axis xyz=\"0 0 1\"/>\n"
             << "    <limit lower=\"-3.14159265\" upper=\"3.14159265\" effort=\"10\" "
                "velocity=\"2\"/>\n"
             << "  </joint>\n";
    }
    urdf << "  <joint name=\"tool_joint\" type=\"fixed\">\n"
         << "    <parent link=\"link_" << n << "\"/>\n"
         << "    <child link=\"tool0\"/>\n"
         << "    " << originXml(toolOrigin) << "\n"
         << "  </joint>\n";
    urdf << "</robot>\n";
    const std::string xml = urdf.str();

    std::error_code ec;
    const fs::path workDir = fs::temp_directory_path(ec) / "trinity_robotics";
    fs::create_directories(workDir, ec);
    if (ec) {
        throw core::EngineExecutionError("Cannot create robotics scratch directory: " +
                                             ec.message(),
                                         {}, "engines");
    }
    std::string id = core::newUuid();
    id.erase(std::remove(id.begin(), id.end(), '-'), id.end());
    const fs::path outPath = workDir / (robotName + "_" + id.substr(0, 8) + ".urdf");
    {
        std::ofstream stream(outPath, std::ios::binary);
        if (!stream) {
            throw core::EngineExecutionError("Cannot open URDF file for writing", {},
                                             "engines");
        }
        stream << xml;
        if (!stream) {
            throw core::EngineExecutionError("Failed while writing URDF", {}, "engines");
        }
    }
    const std::string sha256 = artifacts::sha256File(outPath.string());

    core::Json data{
        {"robot_name", robotName},
        {"chain_source", chainSource},
        {"path", outPath.string()},
        {"sha256", sha256},
        {"bytes", static_cast<long long>(xml.size())},
        {"link_count", static_cast<long long>(n + 2)},
        {"joint_count", static_cast<long long>(n + 1)},
        {"revolute_joints", static_cast<long long>(n)},
        {"convention",
         "standard DH to URDF: fixed joint origins (xyz/rpy) + revolute z axes + "
         "fixed tool joint"}};

    EngineResult out = successResult(request, data);
    out.pendingArtifacts.emplace_back(outPath.string(), "urdf");
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "robotics URDF exported",
        core::Json{{"robot", robotName},
                   {"joints", static_cast<long long>(n)},
                   {"sha256", sha256}});
    return out;
}

std::string firstFailure(const robotics::RobotValidation& validation) {
    for (const robotics::ValidationRule& rule : validation.rules) {
        if (!rule.passed) {
            return rule.rule + ": " + rule.message;
        }
    }
    return "validation failed";
}

void requireValidModel(const robotics::RobotProject& project, const std::string& operation) {
    const robotics::RobotValidation validation = robotics::validateRobotProject(project);
    if (!validation.passed) {
        throw core::RequestValidationError("Robot model failed validation: " + firstFailure(validation),
                                           {{"operation", operation},
                                            {"param", "robot"},
                                            {"rules", validation.toJson()}},
                                           "engines");
    }
}

std::optional<robotics::JointState> jointStateParam(const core::Json& params,
                                                    const robotics::RobotProject* project,
                                                    const std::string& operation) {
    if (hasKey(params, "joint_state") && params["joint_state"].is_object()) {
        return robotics::JointState::fromJson(params["joint_state"]);
    }
    if (hasKey(params, "joint_positions")) {
        const core::Json& value = params["joint_positions"];
        if (value.is_object()) {
            return robotics::JointState::fromJson(core::Json{{"positions", value}});
        }
        if (value.is_array()) {
            if (project == nullptr) {
                throw core::RequestValidationError(
                    "joint_positions as an array requires a robot model; pass an object "
                    "of joint-name keys instead",
                    {{"operation", operation}, {"param", "joint_positions"}}, "engines");
            }
            std::vector<std::string> order;
            std::string error;
            if (!robotics::actuatedJointOrder(*project, order, error)) {
                throw core::RequestValidationError(error,
                                                   {{"operation", operation},
                                                    {"param", "joint_positions"}},
                                                   "engines");
            }
            const std::vector<double> values =
                parseJointArray(params, "joint_positions", operation, true, order.size());
            robotics::JointState state;
            for (size_t i = 0; i < order.size(); ++i) {
                state.positions[order[i]] = values[i];
            }
            return state;
        }
        throw core::RequestValidationError(
            "joint_positions must be an object of joint names or an array in actuated-joint "
            "order",
            {{"operation", operation}, {"param", "joint_positions"}}, "engines");
    }
    return std::nullopt;
}

core::Json positionsJson(const robotics::JointState& state) {
    core::Json out = core::Json::object();
    for (const auto& [name, value] : state.positions) {
        out[name] = value;
    }
    return out;
}

robotics::JointState flatStateFrom(const core::Json& value) {
    if (value.is_object() && value.contains("positions")) {
        return robotics::JointState::fromJson(value);
    }
    robotics::JointState state;
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.value().is_number()) {
                state.positions[it.key()] = it.value().get<double>();
            }
        }
    }
    return state;
}

std::optional<robotics::RobotProject> RoboticsEngine::findProject(
    const std::string& projectId) const {
    if (projectId.empty()) {
        return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(projectsMutex_);
    const auto it = projects_.find(projectId);
    if (it == projects_.end()) {
        return std::nullopt;
    }
    return it->second;
}

robotics::RobotProject RoboticsEngine::storeProject(robotics::RobotProject project) {
    if (project.projectId.empty()) {
        project.projectId = core::newUuid();
    }
    if (project.robotName.empty()) {
        project.robotName = "trinity_robot";
    }
    if (project.model.name.empty()) {
        project.model.name = project.robotName;
    }
    if (project.baseFrame.empty()) {
        project.baseFrame = "world";
    }
    project.model.baseFrame = project.baseFrame;
    project.syncJointLimits();
    std::lock_guard<std::mutex> lock(projectsMutex_);
    projects_[project.projectId] = project;
    return project;
}

robotics::RobotProject RoboticsEngine::resolveProject(const core::Json& params,
                                                      const std::string& operation) {
    if (hasKey(params, "robot") && params["robot"].is_object()) {
        return storeProject(robotics::RobotProject::fromJson(params["robot"]));
    }
    if (hasKey(params, "project_id")) {
        if (!params["project_id"].is_string()) {
            throw core::RequestValidationError("project_id must be a string",
                                               {{"operation", operation},
                                                {"param", "project_id"}},
                                               "engines");
        }
        const std::string projectId = params["project_id"].get<std::string>();
        if (projectId.empty()) {
            throw core::RequestValidationError("project_id must not be empty",
                                               {{"operation", operation},
                                                {"param", "project_id"}},
                                               "engines");
        }
        std::optional<robotics::RobotProject> stored = findProject(projectId);
        if (!stored) {
            throw core::RequestValidationError("Unknown robot project_id '" + projectId + "'",
                                               {{"operation", operation},
                                                {"param", "project_id"}},
                                               "engines");
        }
        return *stored;
    }
    throw core::RequestValidationError(
        "Operation '" + operation + "' requires 'project_id' (stored robot) or an inline "
        "'robot' model",
        {{"operation", operation}, {"param", "project_id"}}, "engines");
}

EngineResult RoboticsEngine::executeCreateRobot(const EngineRequest& request) {
    const core::Json& params = request.parameters;
    const std::string operation = "create_robot";
    robotics::RobotProject project;
    std::string axisSource = "robot_json";
    std::string lengthSource = "robot_json";
    double linkLength = 0.0;
    long long jointCount = 0;
    std::string jointTypeText = "revolute";

    if (hasKey(params, "robot") && params["robot"].is_object()) {
        project = robotics::RobotProject::fromJson(params["robot"]);
        jointCount = static_cast<long long>(project.model.joints.size());
        if (project.model.links.size() > 1) {
            linkLength = project.model.links[1].lengthM;
        }
        if (!project.model.joints.empty()) {
            jointTypeText = robotics::toString(project.model.joints[0].type);
        }
    } else {
        project.robotName =
            sanitizeName(params.value("robot_name", params.value("name", "trinity_robot")),
                         "trinity_robot");
        project.baseFrame = params.value("base_frame", "world");
        const std::string countKey =
            hasKey(params, "link_count")
                ? "link_count"
                : (hasKey(params, "joint_count") ? "joint_count" : std::string());
        if (countKey.empty()) {
            throw core::RequestValidationError(
                "Operation 'create_robot' requires 'link_count' or 'joint_count' (1 to " +
                    std::to_string(robotics::kMaxJoints) + ")",
                {{"operation", operation}, {"param", "link_count"}}, "engines");
        }
        const double rawCount = requireFinite(params[countKey], countKey, operation);
        if (rawCount != std::floor(rawCount)) {
            throw core::RequestValidationError(countKey + " must be an integer",
                                               {{"operation", operation}, {"param", countKey}},
                                               "engines");
        }
        jointCount = static_cast<long long>(rawCount);
        if (jointCount < 1 || jointCount > robotics::kMaxJoints) {
            throw core::RequestValidationError(
                countKey + " must be between 1 and " + std::to_string(robotics::kMaxJoints),
                {{"operation", operation}, {"param", countKey}}, "engines");
        }
        bool found = false;
        linkLength = lengthMeters(params, "link_length_m", "link_length_mm", operation, false,
                                  found);
        if (!found) {
            linkLength = robotics::kDefaultLinkLengthM;
            lengthSource = "engine_default";
        } else if (linkLength < 0.0) {
            throw core::RequestValidationError("link_length must be non-negative",
                                               {{"operation", operation},
                                                {"param", "link_length_m"}},
                                               "engines");
        } else {
            lengthSource = "parameters";
        }
        jointTypeText = params.value("joint_type", "revolute");
        robotics::JointType type;
        try {
            type = robotics::jointTypeFromString(jointTypeText);
        } catch (const std::exception&) {
            throw core::RequestValidationError("joint_type must be revolute|prismatic|fixed",
                                               {{"operation", operation},
                                                {"param", "joint_type"}},
                                               "engines");
        }
        bool axisFound = false;
        robotics::Vec3 axis = vec3Param(params, "axis", "axis", operation, false, axisFound);
        axisSource = axisFound ? "parameters" : "default_001";
        if (!axisFound) {
            axis = robotics::Vec3{0.0, 0.0, 1.0};
        }
        robotics::JointLimit limit;
        if (type != robotics::JointType::Fixed) {
            limit = limitParam(params, type, operation);
        }

        project.model.links.clear();
        project.model.joints.clear();
        project.model.links.push_back(robotics::Link{"base_link", 0.0});
        for (long long i = 1; i <= jointCount; ++i) {
            project.model.links.push_back(
                robotics::Link{"link_" + std::to_string(i), linkLength});
        }
        for (long long i = 1; i <= jointCount; ++i) {
            robotics::Joint joint;
            joint.name = "joint_" + std::to_string(i);
            joint.type = type;
            if (i == 1) {
                joint.parentLink = "base_link";
                joint.childLink = "link_1";
                joint.originXYZ = robotics::Vec3{0.0, 0.0, 0.0};
            } else {
                joint.parentLink = "link_" + std::to_string(i - 1);
                joint.childLink = "link_" + std::to_string(i);
                joint.originXYZ = robotics::Vec3{linkLength, 0.0, 0.0};
            }
            joint.axis = axis;
            joint.limit = limit;
            project.model.joints.push_back(joint);
        }
        project.endEffector.name =
            sanitizeName(params.value("end_effector_name", "tool0"), "tool0");
        project.endEffector.parentLink = "link_" + std::to_string(jointCount);
        project.endEffector.originXYZ = robotics::Vec3{linkLength, 0.0, 0.0};
        project.metadata = core::Json{{"created_by", "create_robot"},
                                      {"generation", "serial_chain"},
                                      {"joint_type", jointTypeText},
                                      {"axis_source", axisSource},
                                      {"link_length_m", linkLength},
                                      {"link_length_source", lengthSource},
                                      {"joint_count", jointCount}};
    }

    requireValidModel(project, operation);
    project = storeProject(std::move(project));

    long long actuated = 0;
    core::Json linksJson = core::Json::array();
    core::Json jointsJson = core::Json::array();
    for (const robotics::Link& link : project.model.links) {
        linksJson.push_back(link.toJson());
    }
    for (const robotics::Joint& joint : project.model.joints) {
        jointsJson.push_back(joint.toJson());
        if (joint.type != robotics::JointType::Fixed) {
            ++actuated;
        }
    }
    const robotics::RobotValidation validation = robotics::validateRobotProject(project);
    core::Json data{
        {"project_id", project.projectId},
        {"robot_name", project.robotName},
        {"base_frame", project.baseFrame},
        {"link_count", static_cast<long long>(project.model.links.size())},
        {"joint_count", static_cast<long long>(project.model.joints.size())},
        {"actuated_joint_count", actuated},
        {"joint_type", jointTypeText},
        {"axis_source", axisSource},
        {"link_length_m", linkLength},
        {"link_length_source", lengthSource},
        {"model", project.model.toJson()},
        {"links", linksJson},
        {"joints", jointsJson},
        {"end_effector", project.endEffector.toJson()},
        {"initial_state", project.initialState.toJson()},
        {"metadata", project.metadata},
        {"model_validation", validation.toJson()}};
    EngineResult out = successResult(request, data);
    out.validation = validate(out);
    return out;
}

EngineResult RoboticsEngine::executeAddLink(const EngineRequest& request) {
    const core::Json& params = request.parameters;
    const std::string operation = "add_link";
    robotics::RobotProject project = resolveProject(params, operation);
    const std::string name = params.value("link_name", params.value("name", ""));
    if (name.empty()) {
        throw core::RequestValidationError("Operation 'add_link' requires 'link_name'",
                                           {{"operation", operation}, {"param", "link_name"}},
                                           "engines");
    }
    if (project.model.findLink(name) != nullptr) {
        throw core::RequestValidationError("Link '" + name + "' already exists",
                                           {{"operation", operation}, {"param", "link_name"}},
                                           "engines");
    }
    if (project.model.links.size() + 1 > static_cast<size_t>(robotics::kMaxLinks)) {
        throw core::RequestValidationError(
            "Robot model exceeds the maximum of " + std::to_string(robotics::kMaxLinks) +
                " links",
            {{"operation", operation}, {"param", "link_name"}}, "engines");
    }
    bool found = false;
    double length = lengthMeters(params, "length_m", "length_mm", operation, false, found);
    if (!found) {
        length = lengthMeters(params, "link_length_m", "link_length_mm", operation, false,
                              found);
    }
    const std::string lengthSource = found ? "parameters" : "engine_default";
    if (!found) {
        length = robotics::kDefaultLinkLengthM;
    }
    if (length < 0.0) {
        throw core::RequestValidationError("Link length must be non-negative",
                                           {{"operation", operation}, {"param", "length_m"}},
                                           "engines");
    }
    project.model.links.push_back(robotics::Link{name, length});
    requireValidModel(project, operation);
    project = storeProject(std::move(project));

    core::Json linksJson = core::Json::array();
    for (const robotics::Link& link : project.model.links) {
        linksJson.push_back(link.toJson());
    }
    core::Json data{{"project_id", project.projectId},
                    {"robot_name", project.robotName},
                    {"link", robotics::Link{name, length}.toJson()},
                    {"length_source", lengthSource},
                    {"link_count", static_cast<long long>(project.model.links.size())},
                    {"links", linksJson},
                    {"model", project.model.toJson()}};
    EngineResult out = successResult(request, data);
    out.validation = validate(out);
    return out;
}

EngineResult RoboticsEngine::executeAddJoint(const EngineRequest& request) {
    const core::Json& params = request.parameters;
    const std::string operation = "add_joint";
    robotics::RobotProject project = resolveProject(params, operation);
    const std::string name = params.value("joint_name", params.value("name", ""));
    if (name.empty()) {
        throw core::RequestValidationError("Operation 'add_joint' requires 'joint_name'",
                                           {{"operation", operation}, {"param", "joint_name"}},
                                           "engines");
    }
    if (project.model.findJoint(name) != nullptr) {
        throw core::RequestValidationError("Joint '" + name + "' already exists",
                                           {{"operation", operation}, {"param", "joint_name"}},
                                           "engines");
    }
    const std::string typeText = params.value("type", params.value("joint_type", ""));
    if (typeText.empty()) {
        throw core::RequestValidationError(
            "Operation 'add_joint' requires 'type' (revolute|prismatic|fixed)",
            {{"operation", operation}, {"param", "type"}}, "engines");
    }
    robotics::JointType type;
    try {
        type = robotics::jointTypeFromString(typeText);
    } catch (const std::exception&) {
        throw core::RequestValidationError("joint type must be revolute|prismatic|fixed",
                                           {{"operation", operation}, {"param", "type"}},
                                           "engines");
    }
    robotics::Joint joint;
    joint.name = name;
    joint.type = type;
    joint.parentLink = params.value("parent_link", "");
    joint.childLink = params.value("child_link", "");
    if (joint.parentLink.empty() || joint.childLink.empty()) {
        throw core::RequestValidationError(
            "add_joint requires 'parent_link' and 'child_link'",
            {{"operation", operation}, {"param", "parent_link"}}, "engines");
    }
    bool axisFound = false;
    joint.axis = vec3Param(params, "axis", "axis", operation, false, axisFound);
    const std::string axisSource = axisFound ? "parameters" : "default_001";
    if (!axisFound) {
        joint.axis = robotics::Vec3{0.0, 0.0, 1.0};
    }
    bool originFound = false;
    joint.originXYZ =
        vec3Param(params, "origin_xyz_m", "origin_xyz_mm", operation, false, originFound);
    const std::string originSource = originFound ? "parameters" : "default_000";
    bool rpyFound = false;
    joint.originRPYRad =
        vec3Param(params, "origin_rpy_rad", "origin_rpy_rad", operation, false, rpyFound);
    if (type != robotics::JointType::Fixed) {
        joint.limit = limitParam(params, type, operation);
    }
    project.model.joints.push_back(joint);
    requireValidModel(project, operation);
    project = storeProject(std::move(project));

    long long actuated = 0;
    for (const robotics::Joint& stored : project.model.joints) {
        if (stored.type != robotics::JointType::Fixed) {
            ++actuated;
        }
    }
    core::Json data{{"project_id", project.projectId},
                    {"robot_name", project.robotName},
                    {"joint", joint.toJson()},
                    {"axis_source", axisSource},
                    {"origin_source", originSource},
                    {"joint_count", static_cast<long long>(project.model.joints.size())},
                    {"actuated_joint_count", actuated},
                    {"model", project.model.toJson()}};
    EngineResult out = successResult(request, data);
    out.validation = validate(out);
    return out;
}

EngineResult RoboticsEngine::executeSetJointState(const EngineRequest& request) {
    const core::Json& params = request.parameters;
    const std::string operation = "set_joint_state";
    robotics::RobotProject project = resolveProject(params, operation);
    std::optional<robotics::JointState> state = jointStateParam(params, &project, operation);
    if (!state) {
        throw core::RequestValidationError(
            "Operation 'set_joint_state' requires 'joint_state' (object) or "
            "'joint_positions' (object or array)",
            {{"operation", operation}, {"param", "joint_state"}}, "engines");
    }
    const robotics::RobotValidation stateValidation =
        robotics::validateJointState(project, *state);
    if (!stateValidation.passed) {
        throw core::RequestValidationError("Joint state rejected: " + firstFailure(stateValidation),
                                           {{"operation", operation},
                                            {"rules", stateValidation.toJson()}},
                                           "engines");
    }
    project.initialState = *state;
    project = storeProject(std::move(project));
    core::Json data{{"project_id", project.projectId},
                    {"robot_name", project.robotName},
                    {"joint_state", project.initialState.toJson()},
                    {"positions", positionsJson(project.initialState)},
                    {"joint_state_source", "parameters"},
                    {"state_validation", stateValidation.toJson()}};
    EngineResult out = successResult(request, data);
    out.validation = validate(out);
    return out;
}

EngineResult RoboticsEngine::executeRobotFk(const EngineRequest& request) {
    const core::Json& params = request.parameters;
    const std::string operation = "compute_forward_kinematics";
    robotics::RobotProject project = resolveProject(params, operation);
    requireValidModel(project, operation);
    const std::optional<robotics::JointState> stateParam =
        jointStateParam(params, &project, operation);
    const robotics::JointState state = stateParam.value_or(project.initialState);
    const std::string stateSource = stateParam ? "parameters" : "initial_state";
    const robotics::RobotValidation stateValidation =
        robotics::validateJointState(project, state);
    if (!stateValidation.passed) {
        throw core::RequestValidationError("Joint state rejected: " + firstFailure(stateValidation),
                                           {{"operation", operation},
                                            {"rules", stateValidation.toJson()}},
                                           "engines");
    }
    const robotics::FkResult fk = robotics::computeForwardKinematics(project, state);
    if (!fk.success) {
        throw core::EngineExecutionError("Forward kinematics failed: " + fk.error,
                                         {{"operation", operation},
                                          {"project_id", project.projectId}},
                                         "engines");
    }
    core::Json frames = core::Json::object();
    core::Json frameOrder = core::Json::array();
    for (const std::string& frameName : fk.frameOrder) {
        frameOrder.push_back(frameName);
        const auto it = fk.frameTransforms.find(frameName);
        if (it != fk.frameTransforms.end()) {
            frames[frameName] = robotics::Pose::fromTransform(it->second).toJson();
        }
    }
    core::Json data{{"project_id", project.projectId},
                    {"robot_name", project.robotName},
                    {"method", "transform_chain"},
                    {"convention",
                     "right-handed frames, URDF-style joint origins (xyz + rpy), SI units"},
                    {"link_count", fk.linkCount},
                    {"joint_count", fk.jointCount},
                    {"actuated_joint_count", fk.actuatedCount},
                    {"joint_positions", positionsJson(state)},
                    {"joint_positions_source", stateSource},
                    {"frame_order", frameOrder},
                    {"frames", frames},
                    {"end_effector", fk.endEffectorPose.toJson()}};
    EngineResult out = successResult(request, data);
    out.validation = validate(out);
    return out;
}

EngineResult RoboticsEngine::executeInverseKinematics(const EngineRequest& request) {
    const core::Json& params = request.parameters;
    const std::string operation = "inverse_kinematics";
    robotics::RobotProject project = resolveProject(params, operation);
    requireValidModel(project, operation);
    const robotics::Vec3 target = targetParam(params, operation);

    robotics::IkOptions options;
    if (hasKey(params, "max_iterations")) {
        const double raw = requireFinite(params["max_iterations"], "max_iterations", operation);
        if (raw < 1.0 || raw > 100000.0) {
            throw core::RequestValidationError("max_iterations must be in [1, 100000]",
                                               {{"operation", operation},
                                                {"param", "max_iterations"}},
                                               "engines");
        }
        options.maxIterations = static_cast<long long>(raw);
    }
    if (hasKey(params, "tolerance_m")) {
        options.toleranceM = requireFinite(params["tolerance_m"], "tolerance_m", operation);
    } else if (hasKey(params, "tolerance_mm")) {
        options.toleranceM =
            requireFinite(params["tolerance_mm"], "tolerance_mm", operation) * 0.001;
    }
    if (!(options.toleranceM > 0.0) || options.toleranceM > 10.0) {
        throw core::RequestValidationError("IK tolerance must be in (0, 10] m",
                                           {{"operation", operation},
                                            {"param", "tolerance_m"}},
                                           "engines");
    }
    options.stepSize = optionalNumber(params, "step_size", options.stepSize);
    if (!(options.stepSize > 0.0) || options.stepSize > 10.0) {
        throw core::RequestValidationError("step_size must be in (0, 10]",
                                           {{"operation", operation}, {"param", "step_size"}},
                                           "engines");
    }
    options.damping = optionalNumber(params, "damping", options.damping);
    if (!(options.damping >= 0.0) || options.damping > 1e6) {
        throw core::RequestValidationError("damping must be in [0, 1e6]",
                                           {{"operation", operation}, {"param", "damping"}},
                                           "engines");
    }
    options.cancelCheck = request.cancelCheck;

    const std::optional<robotics::JointState> seedParam =
        jointStateParam(params, &project, operation);
    const robotics::JointState seed = seedParam.value_or(project.initialState);
    const std::string seedSource = seedParam ? "parameters" : "initial_state";
    const robotics::RobotValidation seedValidation = robotics::validateJointState(project, seed);
    if (!seedValidation.passed) {
        throw core::RequestValidationError("IK seed rejected: " + firstFailure(seedValidation),
                                           {{"operation", operation},
                                            {"rules", seedValidation.toJson()}},
                                           "engines");
    }

    const robotics::IkResult ik =
        robotics::solveInverseKinematics(project, target, seed, options);
    if (ik.cancelled) {
        EngineResult out =
            failureResult(request, "Inverse kinematics cancelled",
                          {{"operation", operation}, {"iterations", ik.iterations}});
        out.validation = validate(out);
        return out;
    }
    const bool structuredNonConvergence =
        !ik.converged && !ik.solution.positions.empty() &&
        ik.error.rfind("IK did not converge", 0) == 0;
    if (!ik.converged && !structuredNonConvergence) {
        EngineResult out = failureResult(
            request, ik.error.empty() ? "Inverse kinematics failed" : ik.error,
            {{"operation", operation}, {"iterations", ik.iterations}});
        out.validation = validate(out);
        return out;
    }

    core::Json ikJson{{"converged", ik.converged},
                      {"iterations", ik.iterations},
                      {"final_error_m", ik.finalErrorM},
                      {"tolerance_m", options.toleranceM},
                      {"max_iterations", options.maxIterations},
                      {"step_size", options.stepSize},
                      {"damping", options.damping},
                      {"within_limits", ik.withinLimits},
                      {"position_only", true}};
    if (!ik.error.empty()) {
        ikJson["message"] = ik.error;
    }
    core::Json data{{"project_id", project.projectId},
                    {"robot_name", project.robotName},
                    {"method", "damped_least_squares"},
                    {"position_only", true},
                    {"position_only_note",
                     "V1 solves end-effector position only; orientation is not controlled"},
                    {"target_position_m", target.toJson()},
                    {"seed_source", seedSource},
                    {"joint_positions", positionsJson(ik.solution)},
                    {"solution", ik.solution.toJson()},
                    {"ik", ikJson},
                    {"end_effector", ik.finalPose.toJson()}};
    {
        const robotics::FkResult solutionFrames =
            robotics::computeForwardKinematics(project, ik.solution);
        if (solutionFrames.success) {
            core::Json frames = core::Json::object();
            core::Json frameOrder = core::Json::array();
            for (const std::string& frameName : solutionFrames.frameOrder) {
                frameOrder.push_back(frameName);
                const auto it = solutionFrames.frameTransforms.find(frameName);
                if (it != solutionFrames.frameTransforms.end()) {
                    frames[frameName] = robotics::Pose::fromTransform(it->second).toJson();
                }
            }
            data["frame_order"] = frameOrder;
            data["frames"] = frames;
        }
    }
    EngineResult out = successResult(request, data);
    out.validation = validate(out);
    return out;
}

EngineResult RoboticsEngine::executeGenerateTrajectory(const EngineRequest& request) {
    const core::Json& params = request.parameters;
    const std::string operation = "generate_trajectory";
    const bool haveProject =
        (hasKey(params, "robot") && params["robot"].is_object()) || hasKey(params, "project_id");
    robotics::RobotProject project;
    if (haveProject) {
        project = resolveProject(params, operation);
        requireValidModel(project, operation);
    }

    std::vector<std::string> order;
    std::vector<double> start;
    std::vector<double> goal;
    if (hasKey(params, "joint_start") || hasKey(params, "joint_goal")) {
        if (!(hasKey(params, "joint_start") && hasKey(params, "joint_goal"))) {
            throw core::RequestValidationError(
                "generate_trajectory requires both 'joint_start' and 'joint_goal'",
                {{"operation", operation}, {"param", "joint_start"}}, "engines");
        }
        start = parseJointArray(params, "joint_start", operation, true, 0);
        goal = parseJointArray(params, "joint_goal", operation, true, 0);
        if (start.size() != goal.size()) {
            throw core::RequestValidationError(
                "joint_start and joint_goal must have equal length",
                {{"operation", operation}, {"param", "joint_goal"}}, "engines");
        }
        if (haveProject) {
            std::string error;
            if (!robotics::actuatedJointOrder(project, order, error)) {
                throw core::RequestValidationError(error,
                                                   {{"operation", operation}},
                                                   "engines");
            }
            if (order.size() != start.size()) {
                throw core::RequestValidationError(
                    "joint_start has " + std::to_string(start.size()) +
                        " entries but the model has " + std::to_string(order.size()) +
                        " actuated joints",
                    {{"operation", operation}, {"param", "joint_start"}}, "engines");
            }
        } else {
            for (size_t i = 1; i <= start.size(); ++i) {
                order.push_back("joint_" + std::to_string(i));
            }
        }
    } else if (hasKey(params, "start_state") || hasKey(params, "goal_state")) {
        if (!haveProject) {
            throw core::RequestValidationError(
                "start_state/goal_state require a robot model ('project_id' or 'robot')",
                {{"operation", operation}, {"param", "start_state"}}, "engines");
        }
        if (!(hasKey(params, "start_state") && hasKey(params, "goal_state"))) {
            throw core::RequestValidationError(
                "generate_trajectory requires both 'start_state' and 'goal_state'",
                {{"operation", operation}, {"param", "start_state"}}, "engines");
        }
        std::string error;
        if (!robotics::actuatedJointOrder(project, order, error)) {
            throw core::RequestValidationError(error, {{"operation", operation}}, "engines");
        }
        const robotics::JointState startState = flatStateFrom(params["start_state"]);
        const robotics::JointState goalState = flatStateFrom(params["goal_state"]);
        for (const std::string& jointName : order) {
            bool found = false;
            const double startValue = startState.positionOf(jointName, found);
            if (!found) {
                throw core::RequestValidationError("start_state is missing joint '" +
                                                       jointName + "'",
                                                   {{"operation", operation},
                                                    {"param", "start_state"}},
                                                   "engines");
            }
            const double goalValue = goalState.positionOf(jointName, found);
            if (!found) {
                throw core::RequestValidationError("goal_state is missing joint '" +
                                                       jointName + "'",
                                                   {{"operation", operation},
                                                    {"param", "goal_state"}},
                                                   "engines");
            }
            start.push_back(startValue);
            goal.push_back(goalValue);
        }
    } else {
        throw core::RequestValidationError(
            "generate_trajectory requires joint_start/joint_goal (arrays) or "
            "start_state/goal_state (objects)",
            {{"operation", operation}, {"param", "joint_start"}}, "engines");
    }

    double duration = 2.0;
    std::string durationSource = "engine_default";
    if (hasKey(params, "duration_s")) {
        duration = requireFinite(params["duration_s"], "duration_s", operation);
        durationSource = "parameters";
    }
    if (!(duration > 0.0) || duration > kMaxDurationS) {
        throw core::RequestValidationError(
            "duration_s must be in (0, " + std::to_string(kMaxDurationS) + "]",
            {{"operation", operation}, {"param", "duration_s"}}, "engines");
    }
    double dt = 0.01;
    std::string dtSource = "engine_default";
    if (hasKey(params, "dt")) {
        dt = requireFinite(params["dt"], "dt", operation);
        dtSource = "parameters";
    }
    if (!(dt > 0.0) || dt > 10.0) {
        throw core::RequestValidationError("dt must be in (0, 10]",
                                           {{"operation", operation}, {"param", "dt"}},
                                           "engines");
    }

    const robotics::TrajectoryResult gen =
        robotics::generateLinearJointTrajectory(haveProject ? &project : nullptr, order, start,
                                                goal, duration, dt, request.cancelCheck);
    if (gen.cancelled) {
        EngineResult out = failureResult(request, "Trajectory generation cancelled",
                                         {{"operation", operation}});
        out.validation = validate(out);
        return out;
    }
    if (!gen.success) {
        throw core::RequestValidationError("Trajectory rejected: " + gen.error,
                                           {{"operation", operation}}, "engines");
    }

    const robotics::Trajectory& trajectory = gen.trajectory;
    const long long total = trajectory.sampleCount();
    bool startOk = total > 0;
    bool goalOk = total > 0;
    if (startOk) {
        for (size_t k = 0; k < order.size(); ++k) {
            if (std::fabs(trajectory.positions[k].front() - start[k]) > 1e-9) {
                startOk = false;
            }
            if (std::fabs(trajectory.positions[k].back() - goal[k]) > 1e-9) {
                goalOk = false;
            }
        }
    }
    const long long stride = std::max<long long>(1, (total + 1999) / 2000);
    core::Json samples = core::Json::array();
    const auto pushSample = [&](long long index) {
        core::Json positions = core::Json::array();
        core::Json velocities = core::Json::array();
        for (size_t k = 0; k < trajectory.positions.size(); ++k) {
            positions.push_back(trajectory.positions[k][static_cast<size_t>(index)]);
            velocities.push_back(trajectory.velocities[k][static_cast<size_t>(index)]);
        }
        samples.push_back(core::Json{{"t", trajectory.timestampsS[static_cast<size_t>(index)]},
                                     {"positions", positions},
                                     {"velocities", velocities}});
    };
    for (long long i = 0; i < total; i += stride) {
        pushSample(i);
    }
    if (total > 0 && ((total - 1) % stride) != 0) {
        pushSample(total - 1);
    }

    core::Json orderJson = core::Json::array();
    core::Json startJson = core::Json::array();
    core::Json goalJson = core::Json::array();
    for (size_t k = 0; k < order.size(); ++k) {
        orderJson.push_back(order[k]);
        startJson.push_back(start[k]);
        goalJson.push_back(goal[k]);
    }

    core::Json data{{"project_id", project.projectId},
                    {"robot_name", project.robotName},
                    {"method", "linear_joint_space"},
                    {"joint_order", orderJson},
                    {"duration_s", duration},
                    {"duration_source", durationSource},
                    {"dt", dt},
                    {"dt_source", dtSource},
                    {"sample_count", total},
                    {"samples_returned", static_cast<long long>(samples.size())},
                    {"sample_stride", stride},
                    {"start_positions", startJson},
                    {"goal_positions", goalJson},
                    {"start_reached", startOk},
                    {"goal_reached", goalOk},
                    {"limits_enforced", gen.limitsEnforced},
                    {"within_limits", gen.withinLimits},
                    {"write_artifacts", params.value("write_artifacts", true)}};
    EngineResult out = successResult(request, data);
    out.result["trajectory"] = samples;
    out.validation = validate(out);

    if (params.value("write_artifacts", true)) {
        std::error_code ec;
        const fs::path workDir =
            fs::temp_directory_path(ec) / "trinity_robotics" / core::newUuid();
        fs::create_directories(workDir, ec);
        if (ec) {
            throw core::EngineExecutionError("Cannot create robotics artifact directory: " +
                                                 ec.message(),
                                             {}, "engines");
        }
        const fs::path csvPath = workDir / "trajectory.csv";
        robotics::writeTrajectoryCsv(csvPath.string(), trajectory);
        long long csvBytes = static_cast<long long>(fs::file_size(csvPath, ec));
        if (ec) {
            throw core::EngineExecutionError("Cannot stat trajectory CSV: " + ec.message(),
                                             {}, "engines");
        }
        const core::Json csvRef{
            {"filename", "trajectory.csv"},
            {"type", "csv"},
            {"path", csvPath.string()},
            {"size_bytes", csvBytes},
            {"checksum", artifacts::sha256File(csvPath.string())}};

        robotics::RobotResult exportResult;
        exportResult.projectId = project.projectId;
        exportResult.operation = "generate_trajectory";
        exportResult.linkCount = static_cast<long long>(project.model.links.size());
        exportResult.jointCount = static_cast<long long>(order.size());
        exportResult.trajectory = trajectory;
        exportResult.checks = core::Json{{"limits_enforced", gen.limitsEnforced},
                                         {"within_limits", gen.withinLimits},
                                         {"endpoints_verified", startOk && goalOk},
                                         {"sample_count", total}};
        exportResult.summary = core::Json{{"method", "linear_joint_space"},
                                          {"duration_s", duration},
                                          {"dt", dt}};
        const fs::path jsonPath = workDir / "robot.json";
        robotics::writeRobotJson(jsonPath.string(), project, &exportResult,
                                 out.validation->toJson(), core::Json::array({csvRef}));
        long long jsonBytes = static_cast<long long>(fs::file_size(jsonPath, ec));
        if (ec) {
            throw core::EngineExecutionError("Cannot stat robot JSON: " + ec.message(), {},
                                             "engines");
        }
        const core::Json jsonRef{
            {"filename", "robot.json"},
            {"type", "json"},
            {"path", jsonPath.string()},
            {"size_bytes", jsonBytes},
            {"checksum", artifacts::sha256File(jsonPath.string())}};
        out.result["artifacts"] = core::Json::array({csvRef, jsonRef});
        out.pendingArtifacts.emplace_back(csvPath.string(), "csv");
        out.pendingArtifacts.emplace_back(jsonPath.string(), "json");
    }
    return out;
}

EngineResult RoboticsEngine::executeValidateRobot(const EngineRequest& request) {
    const core::Json& params = request.parameters;
    const std::string operation = "validate_robot";
    robotics::RobotProject project = resolveProject(params, operation);
    const std::optional<robotics::JointState> state =
        jointStateParam(params, &project, operation);
    const robotics::JointState currentState = state.value_or(project.initialState);
    const robotics::RobotValidation modelValidation = robotics::validateRobotProject(project);
    const robotics::RobotValidation stateValidation =
        robotics::validateJointState(project, currentState);
    const bool passed = modelValidation.passed && stateValidation.passed;
    core::Json rules = core::Json::array();
    for (const robotics::ValidationRule& rule : modelValidation.rules) {
        rules.push_back(rule.toJson());
    }
    for (const robotics::ValidationRule& rule : stateValidation.rules) {
        rules.push_back(rule.toJson());
    }
    core::Json data{{"project_id", project.projectId},
                    {"robot_name", project.robotName},
                    {"base_frame", project.baseFrame},
                    {"passed", passed},
                    {"joint_state_source", state ? "parameters" : "initial_state"},
                    {"joint_state", currentState.toJson()},
                    {"model_validation", modelValidation.toJson()},
                    {"joint_state_validation", stateValidation.toJson()},
                    {"rules", rules}};
    EngineResult out = successResult(request, data);
    out.validation = validate(out);
    return out;
}

EngineResult RoboticsEngine::execute(const EngineRequest& request) {
    if (request.operation == "describe") {
        EngineResult out =
            successResult(request, {{"engine", name_},
                                    {"version", version_},
                                    {"capabilities", capabilities_},
                                    {"conventions",
                                     core::Json{{"kinematics", "standard DH (rad, m)"},
                                                {"frames",
                                                 "right-handed; URDF-style joint origins "
                                                 "(xyz + rpy), SI units (m, rad, s) inside; "
                                                 "mm/deg normalized at the boundary"},
                                                {"trajectory",
                                                 "closed_form joint interpolation via "
                                                 "simulation::integrate (plan_trajectory) "
                                                 "and deterministic linear joint space "
                                                 "(generate_trajectory)"},
                                                {"ik", "V1 damped least squares, position "
                                                       "only (orientation not solved)"},
                                                {"export", "URDF (geometry only) + SHA-256"}}},
                                    {"limits",
                                     core::Json{{"max_joints", kMaxJoints},
                                                {"max_links", robotics::kMaxLinks},
                                                {"max_duration_s", kMaxDurationS}}}});
        out.validation = validate(out);
        return out;
    }
    try {
        if (request.operation == "forward_kinematics") {
            const bool irModel =
                request.parameters.contains("project_id") || request.parameters.contains("robot");
            if (irModel) {
                return executeRobotFk(request);
            }
            return executeForwardKinematics(request);
        }
        if (request.operation == "compute_forward_kinematics") {
            return executeRobotFk(request);
        }
        if (request.operation == "plan_trajectory") {
            return executePlanTrajectory(request);
        }
        if (request.operation == "export_urdf") {
            return executeExportUrdf(request);
        }
        if (request.operation == "create_robot") {
            return executeCreateRobot(request);
        }
        if (request.operation == "add_link") {
            return executeAddLink(request);
        }
        if (request.operation == "add_joint") {
            return executeAddJoint(request);
        }
        if (request.operation == "set_joint_state") {
            return executeSetJointState(request);
        }
        if (request.operation == "inverse_kinematics") {
            return executeInverseKinematics(request);
        }
        if (request.operation == "generate_trajectory") {
            return executeGenerateTrajectory(request);
        }
        if (request.operation == "validate_robot") {
            return executeValidateRobot(request);
        }
        return capabilityUnavailable(
            request, "Robotics operation '" + request.operation + "' is not implemented yet");
    } catch (const core::TrinityError& exc) {
        EngineResult out = failureResult(request, exc.what(),
                                         exc.toJson().value("details", core::Json::object()));
        out.errors.clear();
        out.addError(exc.info());
        out.validation = validate(out);
        return out;
    }
}

validation::ValidationResult RoboticsEngine::validate(const EngineResult& result) const {
    validation::ValidationResult validation;
    validation.operation = result.operation;
    validation.jobId = result.jobId;
    validation.checks = {{"engine", "robotics"}, {"operation", result.operation}};
    if (!result.success) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "Robotics operation failed";
        validation::ValidationMessage msg;
        msg.rule = "robotics.success";
        msg.severity = validation::Severity::Error;
        msg.passed = false;
        msg.message = "Engine reported failure";
        validation.addMessage(std::move(msg));
        if (!result.errors.empty()) {
            validation.error = result.errors.front();
        }
        return validation;
    }

    auto addPass = [&](const std::string& rule, const std::string& message) {
        validation.status = validation::ValidationStatus::Validated;
        validation.message = message;
        validation::ValidationMessage msg;
        msg.rule = rule;
        msg.severity = validation::Severity::Info;
        msg.passed = true;
        msg.message = message;
        validation.addMessage(std::move(msg));
    };
    auto addFail = [&](const std::string& rule, const std::string& message) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = message;
        validation::ValidationMessage msg;
        msg.rule = rule;
        msg.severity = validation::Severity::Error;
        msg.passed = false;
        msg.message = message;
        validation.addMessage(std::move(msg));
    };
    auto addVerified = [&](const std::string& rule, const std::string& message) {
        validation.status = validation::ValidationStatus::Verified;
        validation.message = message;
        validation::ValidationMessage msg;
        msg.rule = rule;
        msg.severity = validation::Severity::Info;
        msg.passed = true;
        msg.message = message;
        validation.addMessage(std::move(msg));
    };

    if (result.operation == "forward_kinematics" && !result.result.contains("project_id")) {
        const core::Json& data = result.result;
        const bool hasEe =
            data.contains("end_effector") && data["end_effector"].contains("position");
        const bool countsMatch =
            data.contains("joint_count") && data.contains("joint_angles") &&
            data["joint_angles"].is_array() &&
            data["joint_angles"].size() ==
                static_cast<size_t>(data.value("joint_count", 0LL));
        if (hasEe && countsMatch) {
            addPass("robotics.forward_kinematics",
                    "Deterministic DH forward kinematics computed");
        } else {
            addFail("robotics.forward_kinematics", "FK result is structurally incomplete");
        }
    } else if (result.operation == "plan_trajectory") {
        const core::Json& data = result.result;
        const bool endpoints =
            data.value("start_reached", false) && data.value("goal_reached", false);
        const bool hasSamples = data.value("sample_count", 0LL) > 0 &&
                                data.contains("trajectory") &&
                                data["trajectory"].is_array();
        if (endpoints && hasSamples) {
            addPass("robotics.trajectory",
                    "Trajectory endpoints verified against integrator output");
        } else {
            addFail("robotics.trajectory",
                    endpoints ? "Trajectory has no samples"
                              : "Trajectory endpoints do not match requested start/goal");
        }
    } else if (result.operation == "export_urdf") {
        const core::Json& data = result.result;
        const std::string sha = data.value("sha256", "");
        const bool okSha = sha.size() == 64 && !data.value("path", std::string()).empty();
        if (okSha) {
            addPass("robotics.urdf_export", "URDF written with SHA-256 checksum");
        } else {
            addFail("robotics.urdf_export", "URDF export is missing path or checksum");
        }
    } else if (result.operation == "create_robot" || result.operation == "add_link" ||
               result.operation == "add_joint" || result.operation == "set_joint_state" ||
               result.operation == "validate_robot") {
        const core::Json& data = result.result;
        const std::optional<robotics::RobotProject> project =
            findProject(data.value("project_id", ""));
        if (!project) {
            addFail("robotics.project_resolved",
                    "Stored robot project not found for re-validation");
        } else {
            const robotics::RobotValidation modelValidation =
                robotics::validateRobotProject(*project);
            robotics::JointState state = project->initialState;
            if (data.contains("joint_state")) {
                state = robotics::JointState::fromJson(data["joint_state"]);
            }
            const robotics::RobotValidation stateValidation =
                robotics::validateJointState(*project, state);
            if (modelValidation.passed && stateValidation.passed) {
                addPass("robotics.model_valid",
                        "Robot model and joint state pass structural validation");
            } else {
                addFail("robotics.model_valid",
                        firstFailure(modelValidation.passed ? stateValidation
                                                            : modelValidation));
            }
        }
    } else if (result.operation == "compute_forward_kinematics" ||
               (result.operation == "forward_kinematics" &&
                result.result.contains("project_id"))) {
        const core::Json& data = result.result;
        const std::optional<robotics::RobotProject> project =
            findProject(data.value("project_id", ""));
        bool verified = false;
        std::string problem;
        if (!project) {
            problem = "Stored robot project not found for recomputation";
        } else {
            robotics::JointState state;
            if (data.contains("joint_positions") && data["joint_positions"].is_object()) {
                state = robotics::JointState::fromJson(
                    core::Json{{"positions", data["joint_positions"]}});
            }
            const robotics::FkResult fk = robotics::computeForwardKinematics(*project, state);
            if (!fk.success) {
                problem = "Independent FK recomputation failed: " + fk.error;
            } else if (!data.contains("end_effector") ||
                       !data["end_effector"].contains("position")) {
                problem = "FK result has no end-effector position";
            } else {
                const core::Json& position = data["end_effector"]["position"];
                const robotics::Vec3 claimed{position.value("x", 1e30),
                                             position.value("y", 1e30),
                                             position.value("z", 1e30)};
                const robotics::Vec3 actual = fk.endEffectorPose.positionM;
                const double distance =
                    std::sqrt((claimed.x - actual.x) * (claimed.x - actual.x) +
                              (claimed.y - actual.y) * (claimed.y - actual.y) +
                              (claimed.z - actual.z) * (claimed.z - actual.z));
                long long claimedFrames = 0;
                if (data.contains("frame_order") && data["frame_order"].is_array()) {
                    claimedFrames =
                        static_cast<long long>(data["frame_order"].size());
                }
                if (distance > 1e-6) {
                    problem = "End-effector position differs from independent recomputation by " +
                              std::to_string(distance) + " m";
                } else if (claimedFrames != static_cast<long long>(fk.frameOrder.size())) {
                    problem = "Frame count differs from independent recomputation";
                } else {
                    verified = true;
                }
            }
        }
        if (verified) {
            addVerified("robotics.forward_kinematics_verified",
                        "FK recomputed independently from the stored model; end-effector "
                        "position matches within 1e-6 m");
        } else {
            addFail("robotics.forward_kinematics_verified", problem);
        }
    } else if (result.operation == "inverse_kinematics") {
        const core::Json& data = result.result;
        const core::Json ik = data.value("ik", core::Json::object());
        const bool converged = ik.value("converged", false);
        const double tolerance = ik.value("tolerance_m", 1e-4);
        const std::optional<robotics::RobotProject> project =
            findProject(data.value("project_id", ""));
        bool verified = false;
        std::string problem;
        if (!converged) {
            problem = "Inverse kinematics did not converge (final position error " +
                      std::to_string(ik.value("final_error_m", 0.0)) +
                      " m); no reachable solution is claimed";
        } else if (!project) {
            problem = "Stored robot project not found for IK verification";
        } else if (!data.contains("target_position_m")) {
            problem = "IK result has no target position to verify against";
        } else {
            robotics::JointState solution;
            if (data.contains("solution")) {
                solution = robotics::JointState::fromJson(data["solution"]);
            }
            const robotics::FkResult fk = robotics::computeForwardKinematics(*project, solution);
            if (!fk.success) {
                problem = "FK at the IK solution failed: " + fk.error;
            } else {
                const robotics::Vec3 target =
                    robotics::Vec3::fromJson(data["target_position_m"]);
                const robotics::Vec3 reached = fk.endEffectorPose.positionM;
                const double error =
                    std::sqrt((target.x - reached.x) * (target.x - reached.x) +
                              (target.y - reached.y) * (target.y - reached.y) +
                              (target.z - reached.z) * (target.z - reached.z));
                if (error > tolerance) {
                    problem = "FK at the IK solution is " + std::to_string(error) +
                              " m from the target, exceeding tolerance " +
                              std::to_string(tolerance) + " m";
                } else if (!ik.value("within_limits", true)) {
                    problem = "IK solution violates joint limits";
                } else {
                    verified = true;
                }
            }
        }
        if (verified) {
            addVerified("robotics.ik_verified",
                        "IK converged; FK recomputed at the reported solution lands within "
                        "the tolerance of the target with joint limits respected "
                        "(position-only V1)");
        } else {
            addFail("robotics.ik_verified", problem);
        }
    } else if (result.operation == "generate_trajectory") {
        const core::Json& data = result.result;
        const core::Json trajectory =
            data.value("trajectory", core::Json::array());
        const core::Json start = data.value("start_positions", core::Json::array());
        const core::Json goal = data.value("goal_positions", core::Json::array());
        bool verified = false;
        std::string problem;
        const long long samples = data.value("sample_count", 0LL);
        if (samples <= 0 || trajectory.empty() || start.empty() || goal.empty()) {
            problem = "Trajectory has no samples to verify";
        } else {
            const core::Json& first = trajectory.front();
            const core::Json& last = trajectory.back();
            const core::Json firstPos = first.value("positions", core::Json::array());
            const core::Json lastPos = last.value("positions", core::Json::array());
            double maxDeviation = 0.0;
            bool sizesOk = firstPos.size() == start.size() && lastPos.size() == goal.size() &&
                           firstPos.size() == lastPos.size();
            if (sizesOk) {
                for (size_t k = 0; k < start.size(); ++k) {
                    const double startDev =
                        std::fabs(firstPos[k].get<double>() - start[k].get<double>());
                    const double goalDev =
                        std::fabs(lastPos[k].get<double>() - goal[k].get<double>());
                    maxDeviation = std::max(maxDeviation, std::max(startDev, goalDev));
                }
            }
            const bool limitsOk =
                data.value("limits_enforced", false) ? data.value("within_limits", true) : true;
            if (!sizesOk) {
                problem = "Trajectory sample width does not match the requested start/goal";
            } else if (maxDeviation > 1e-9) {
                problem = "Trajectory endpoints deviate from start/goal by " +
                          std::to_string(maxDeviation) + " rad or m";
            } else if (!limitsOk) {
                problem = "Trajectory violates configured joint limits";
            } else if (!data.contains("joint_order") || data["joint_order"].empty()) {
                problem = "Trajectory result has no joint order";
            } else {
                verified = true;
            }
        }
        if (verified) {
            addVerified("robotics.trajectory_verified",
                        "Trajectory endpoints re-checked against the requested start/goal "
                        "within 1e-9; joint limits satisfied");
        } else {
            addFail("robotics.trajectory_verified", problem);
        }
    } else {
        addPass("robotics.metadata", "Robotics metadata operation completed");
    }
    return validation;
}

}  // namespace trinity::engines
