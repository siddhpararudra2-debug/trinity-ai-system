#include <doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "trinity/engines/RoboticsEngine.hpp"

using trinity::engines::EngineRequest;
using trinity::engines::RoboticsEngine;

namespace {

EngineRequest makeReq(const std::string& operation, trinity::core::Json params) {
    EngineRequest req;
    req.engine = "robotics";
    req.operation = operation;
    req.parameters = std::move(params);
    return req;
}

trinity::core::Json dh(double a, double alpha, double d = 0.0, double thetaOffset = 0.0) {
    return trinity::core::Json{{"a", a},
                               {"alpha", alpha},
                               {"d", d},
                               {"theta_offset", thetaOffset}};
}

}  // namespace

TEST_CASE("robotics describes capabilities") {
    RoboticsEngine engine;
    CHECK(engine.name() == "robotics");
    const auto out = engine.execute(makeReq("describe", {}));
    REQUIRE(out.success);
    CHECK(out.result["engine"] == "robotics");
    REQUIRE(out.result["capabilities"].is_array());
    CHECK(out.result["capabilities"].size() == 12);
    REQUIRE(out.validation.has_value());
    CHECK(out.validation->passed());
}

TEST_CASE("robotics forward kinematics solves the default 2-link chain") {
    RoboticsEngine engine;
    const auto out = engine.execute(
        makeReq("forward_kinematics", {{"joint_angles", {0.0, 0.0}}}));
    REQUIRE(out.success);
    CHECK(out.result["chain_source"] == "engine_default_2link");
    CHECK(out.result["joint_count"] == 2);
    CHECK(out.result["angles_source"] == "parameters");
    const auto& pos = out.result["end_effector"]["position"];
    CHECK(std::fabs(pos.value("x", 0.0) - 2.0) < 1e-6);
    CHECK(std::fabs(pos.value("y", 0.0)) < 1e-6);
    CHECK(std::fabs(pos.value("z", 0.0)) < 1e-6);
    REQUIRE(out.result["frames"].is_array());
    CHECK(out.result["frames"].size() == 2);
    REQUIRE(out.validation.has_value());
    CHECK(out.validation->passed());
    const bool validated =
        out.validation->status == trinity::validation::ValidationStatus::Validated;
    CHECK(validated);
}

TEST_CASE("robotics forward kinematics honors explicit DH chains and angles") {
    RoboticsEngine engine;
    // Quarter turn on a single link of length 1 along x: EE = (0, 1, 0).
    const auto out = engine.execute(makeReq(
        "forward_kinematics",
        {{"dh_params", trinity::core::Json::array({dh(1.0, 0.0)})},
         {"joint_angles", trinity::core::Json::array({1.5707963267948966})}}));
    REQUIRE(out.success);
    CHECK(out.result["chain_source"] == "parameters");
    const auto& pos = out.result["end_effector"]["position"];
    CHECK(std::fabs(pos.value("x", 0.0)) < 1e-6);
    CHECK(std::fabs(pos.value("y", 0.0) - 1.0) < 1e-6);
    CHECK(std::fabs(pos.value("z", 0.0)) < 1e-6);
}

TEST_CASE("robotics forward kinematics defaults missing angles to zero") {
    RoboticsEngine engine;
    const auto out = engine.execute(makeReq("forward_kinematics", {}));
    REQUIRE(out.success);
    CHECK(out.result["angles_source"] == "default_zero");
    const auto& pos = out.result["end_effector"]["position"];
    CHECK(std::fabs(pos.value("x", 0.0) - 2.0) < 1e-6);
}

TEST_CASE("robotics forward kinematics rejects mismatched angles") {
    RoboticsEngine engine;
    const auto out =
        engine.execute(makeReq("forward_kinematics", {{"joint_angles", {0.0, 0.0, 0.0}}}));
    CHECK_FALSE(out.success);
    REQUIRE_FALSE(out.errors.empty());
    REQUIRE(out.validation.has_value());
    CHECK_FALSE(out.validation->passed());
}

TEST_CASE("robotics plan_trajectory integrates joints to the goal") {
    RoboticsEngine engine;
    const auto out = engine.execute(makeReq(
        "plan_trajectory",
        {{"joint_start", {0.0, 0.0}},
         {"joint_goal", trinity::core::Json::array({1.0, 0.5})},
         {"duration_s", 2.0},
         {"dt", 0.01}}));
    REQUIRE(out.success);
    CHECK(out.result["start_reached"] == true);
    CHECK(out.result["goal_reached"] == true);
    CHECK(out.result["sample_count"] > 1);
    CHECK(out.result["integrator"] == "simulation::integrate (closed_form)");
    const auto& finalPositions = out.result["final_positions"];
    REQUIRE(finalPositions.is_array());
    CHECK(finalPositions.size() == 2);
    CHECK(std::fabs(finalPositions[0].get<double>() - 1.0) < 1e-6);
    CHECK(std::fabs(finalPositions[1].get<double>() - 0.5) < 1e-6);
    REQUIRE(out.result["trajectory"].is_array());
    REQUIRE(out.result["trajectory"].size() >= 2);
    CHECK(out.result["trajectory"][0]["t"] == 0.0);
    REQUIRE(out.validation.has_value());
    CHECK(out.validation->passed());
}

TEST_CASE("robotics plan_trajectory refuses missing or bad parameters") {
    RoboticsEngine engine;
    const auto missing = engine.execute(makeReq("plan_trajectory", {}));
    CHECK_FALSE(missing.success);
    REQUIRE_FALSE(missing.errors.empty());

    const auto badDuration = engine.execute(
        makeReq("plan_trajectory", {{"joint_start", {0.0}},
                                    {"joint_goal", {1.0}},
                                    {"duration_s", -1.0}}));
    CHECK_FALSE(badDuration.success);

    const auto mismatched = engine.execute(
        makeReq("plan_trajectory", {{"joint_start", {0.0, 0.0}},
                                    {"joint_goal", {1.0}}}));
    CHECK_FALSE(mismatched.success);
}

TEST_CASE("robotics export_urdf writes a checksummed URDF artifact") {
    RoboticsEngine engine;
    const auto out = engine.execute(makeReq(
        "export_urdf",
        {{"dh_params", trinity::core::Json::array({dh(1.0, 0.0), dh(1.0, 0.0)})},
         {"robot_name", "test_arm"}}));
    REQUIRE(out.success);
    CHECK(out.result["robot_name"] == "test_arm");
    CHECK(out.result["revolute_joints"] == 2);
    CHECK(out.result["joint_count"] == 3);
    CHECK(out.result["link_count"] == 4);
    const std::string sha = out.result.value("sha256", "");
    CHECK(sha.size() == 64);
    REQUIRE(out.pendingArtifacts.size() == 1);
    const std::string path = out.pendingArtifacts[0].first;
    CHECK(out.pendingArtifacts[0].second == "urdf");
    REQUIRE(std::filesystem::exists(path));
    {
        std::ifstream stream(path);
        std::string xml((std::istreambuf_iterator<char>(stream)),
                        std::istreambuf_iterator<char>());
        CHECK(xml.find("<robot name=\"test_arm\">") != std::string::npos);
        CHECK(xml.find("type=\"revolute\"") != std::string::npos);
        CHECK(xml.find("tool0") != std::string::npos);
    }
    REQUIRE(out.validation.has_value());
    CHECK(out.validation->passed());
    std::filesystem::remove(path);
}

TEST_CASE("robotics export_urdf reuses the last FK chain when no chain is given") {
    RoboticsEngine engine;
    const auto fk = engine.execute(makeReq("forward_kinematics", {}));
    REQUIRE(fk.success);
    const auto out = engine.execute(makeReq("export_urdf", {}));
    REQUIRE(out.success);
    CHECK(out.result["chain_source"] == "last_fk_chain");
    REQUIRE(out.pendingArtifacts.size() == 1);
    std::filesystem::remove(out.pendingArtifacts[0].first);
}

TEST_CASE("robotics refuses unknown operations without faking results") {
    RoboticsEngine engine;
    for (const std::string& op : {"path_planning", "simulate", "collision_check"}) {
        const auto out = engine.execute(makeReq(op, {}));
        CHECK_FALSE(out.success);
        REQUIRE_FALSE(out.errors.empty());
        CHECK(out.errors.front().value("code", "") == "capability_unavailable");
        REQUIRE(out.validation.has_value());
        CHECK_FALSE(out.validation->passed());
    }
}
