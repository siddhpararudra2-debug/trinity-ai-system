#include "trinity/engines/RoboticsEngine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "trinity/artifacts/Checksum.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/core/Uuid.hpp"
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

}  // namespace

RoboticsEngine::RoboticsEngine() {
    name_ = "robotics";
    version_ = "0.1.0";
    capabilities_ = {"describe", "forward_kinematics", "plan_trajectory", "export_urdf"};
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

EngineResult RoboticsEngine::execute(const EngineRequest& request) {
    if (request.operation == "describe") {
        EngineResult out =
            successResult(request, {{"engine", name_},
                                    {"version", version_},
                                    {"capabilities", capabilities_},
                                    {"conventions",
                                     core::Json{{"kinematics", "standard DH (rad, m)"},
                                                {"trajectory",
                                                 "closed_form joint interpolation via "
                                                 "simulation::integrate"},
                                                {"export", "URDF (geometry only) + SHA-256"}}},
                                    {"limits",
                                     core::Json{{"max_joints", kMaxJoints},
                                                {"max_duration_s", kMaxDurationS}}}});
        out.validation = validate(out);
        return out;
    }
    try {
        if (request.operation == "forward_kinematics") {
            return executeForwardKinematics(request);
        }
        if (request.operation == "plan_trajectory") {
            return executePlanTrajectory(request);
        }
        if (request.operation == "export_urdf") {
            return executeExportUrdf(request);
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

    if (result.operation == "forward_kinematics") {
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
    } else {
        addPass("robotics.metadata", "Robotics metadata operation completed");
    }
    return validation;
}

}  // namespace trinity::engines
