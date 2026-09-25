#include <doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/artifacts/Checksum.hpp"
#include "trinity/robotics/Exports.hpp"
#include "trinity/robotics/Kinematics.hpp"
#include "trinity/robotics/Types.hpp"

using namespace trinity;

namespace {

std::string tempPath(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / "trinity-test-robot-exports";
    std::filesystem::create_directories(dir);
    return (dir / name).string();
}

std::vector<std::string> readLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream stream(path);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::vector<double> parseRow(const std::string& row) {
    std::vector<double> values;
    std::istringstream stream(row);
    std::string cell;
    while (std::getline(stream, cell, ',')) {
        values.push_back(std::stod(cell));
    }
    return values;
}

trinity::robotics::TrajectoryResult makeTrajectory() {
    using namespace trinity::robotics;
    return generateLinearJointTrajectory(nullptr, {"joint_1", "joint_2"}, {0.0, 0.0},
                                         {1.0, 0.5}, 1.0, 0.5);
}

}  // namespace

TEST_CASE("robotics exports: CSV header and rows follow the spec") {
    using namespace trinity::robotics;
    const TrajectoryResult gen = makeTrajectory();
    REQUIRE(gen.success);
    const std::string path = tempPath("traj.csv");
    writeTrajectoryCsv(path, gen.trajectory);
    const std::vector<std::string> lines = readLines(path);
    REQUIRE(lines.size() == 4);  // header + 3 samples
    CHECK(lines[0] == "time,joint_1_position,joint_1_velocity,joint_2_position,joint_2_velocity");
    const std::vector<double> first = parseRow(lines[1]);
    REQUIRE(first.size() == 5);
    CHECK(first[0] == doctest::Approx(0.0));
    CHECK(first[1] == doctest::Approx(0.0));
    CHECK(first[2] == doctest::Approx(1.0));
    CHECK(first[3] == doctest::Approx(0.0));
    CHECK(first[4] == doctest::Approx(0.5));
    const std::vector<double> last = parseRow(lines.back());
    REQUIRE(last.size() == 5);
    CHECK(last[0] == doctest::Approx(1.0));
    CHECK(last[1] == doctest::Approx(1.0));
    CHECK(last[3] == doctest::Approx(0.5));
    // Round-trip precision: parsed values match the in-memory trajectory.
    for (size_t i = 1; i < lines.size(); ++i) {
        const std::vector<double> row = parseRow(lines[i]);
        CHECK(row[0] == doctest::Approx(gen.trajectory.timestampsS[i - 1]));
        CHECK(row[1] == doctest::Approx(gen.trajectory.positions[0][i - 1]));
        CHECK(row[2] == doctest::Approx(gen.trajectory.velocities[0][i - 1]));
        CHECK(row[3] == doctest::Approx(gen.trajectory.positions[1][i - 1]));
        CHECK(row[4] == doctest::Approx(gen.trajectory.velocities[1][i - 1]));
    }
    std::filesystem::remove(path);
}

TEST_CASE("robotics exports: CSV writing is deterministic") {
    using namespace trinity::robotics;
    const TrajectoryResult gen = makeTrajectory();
    REQUIRE(gen.success);
    const std::string a = tempPath("traj_a.csv");
    const std::string b = tempPath("traj_b.csv");
    writeTrajectoryCsv(a, gen.trajectory);
    writeTrajectoryCsv(b, gen.trajectory);
    CHECK(trinity::artifacts::sha256File(a) == trinity::artifacts::sha256File(b));
    std::filesystem::remove(a);
    std::filesystem::remove(b);
}

TEST_CASE("robotics exports: robot JSON carries project, validation, and refs") {
    using namespace trinity::robotics;
    RobotProject project;
    project.projectId = "proj-export";
    project.robotName = "export_arm";
    project.model.links = {Link{"base_link", 0.0}, Link{"link_1", 0.2}};
    project.initialState.positions["joint_1"] = 0.1;

    RobotResult result;
    result.projectId = "proj-export";
    result.operation = "generate_trajectory";
    result.linkCount = 2;
    result.jointCount = 1;
    result.checks = core::Json{{"endpoints_verified", true}};
    result.summary = core::Json{{"method", "linear_joint_space"}};

    const core::Json validation =
        core::Json{{"status", "Verified"}, {"operation", "generate_trajectory"}};
    const core::Json refs = core::Json::array(
        {core::Json{{"filename", "trajectory.csv"}, {"type", "csv"}, {"checksum", "abc"}}});

    const std::string path = tempPath("robot.json");
    writeRobotJson(path, project, &result, validation, refs);

    std::ifstream stream(path);
    std::string text((std::istreambuf_iterator<char>(stream)),
                     std::istreambuf_iterator<char>());
    const core::Json doc = core::Json::parse(text);
    CHECK(doc.value("robot_name", "") == "export_arm");
    CHECK(doc["project"].value("project_id", "") == "proj-export");
    CHECK(doc["initial_state"]["positions"].value("joint_1", 0.0) ==
          doctest::Approx(0.1));
    CHECK(doc["validation"].value("status", "") == "Verified");
    REQUIRE(doc["artifacts"].is_array());
    REQUIRE(doc["artifacts"].size() == 1);
    CHECK(doc["artifacts"][0].value("filename", "") == "trajectory.csv");
    CHECK(doc["result"]["checks"].value("endpoints_verified", false) == true);
    stream.close();
    std::filesystem::remove(path);
}

TEST_CASE("robotics exports: JSON trajectory preview is bounded") {
    using namespace trinity::robotics;
    const TrajectoryResult gen = generateLinearJointTrajectory(nullptr, {"joint_1"}, {0.0},
                                                               {1.0}, 1.0, 0.001);
    REQUIRE(gen.success);
    REQUIRE(gen.trajectory.sampleCount() > 500);

    RobotProject project;
    project.robotName = "preview_arm";
    RobotResult result;
    result.operation = "generate_trajectory";
    result.trajectory = gen.trajectory;

    const std::string path = tempPath("preview.json");
    writeRobotJson(path, project, &result, core::Json::object());
    std::ifstream stream(path);
    std::string text((std::istreambuf_iterator<char>(stream)),
                     std::istreambuf_iterator<char>());
    const core::Json doc = core::Json::parse(text);
    REQUIRE(doc["trajectory_preview"].is_array());
    CHECK(doc["trajectory_preview"].size() <= 500);
    stream.close();
    std::filesystem::remove(path);
}
