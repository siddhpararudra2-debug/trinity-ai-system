#include <doctest.h>

#include <cmath>

#include "trinity/intelligence/RequirementParser.hpp"

using trinity::intelligence::ParseStatus;
using trinity::intelligence::RequirementParser;

namespace {

bool missingContains(const trinity::intelligence::Intent& intent,
                     const std::string& key) {
    for (const std::string& entry : intent.missing) {
        if (entry == key) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("robotics intent: create request without a link length is incomplete") {
    RequirementParser parser;
    const auto parsed = parser.parse("Create a 2-link robotic arm");
    const bool status_Incomplete = parsed.status == ParseStatus::Incomplete; CHECK(status_Incomplete);
    CHECK(parsed.intent.domain == "robotics");
    CHECK(parsed.intent.operation == "create_robot");
    REQUIRE(parsed.intent.parameters.contains("link_count"));
    CHECK(parsed.intent.parameters["link_count"].get<long long>() == 2);
    CHECK(missingContains(parsed.intent, "link_length_m"));
    REQUIRE_FALSE(parsed.errors.empty());
}

TEST_CASE("robotics intent: design request with mm link length is valid") {
    RequirementParser parser;
    const auto parsed = parser.parse("Design a 3-joint robot with 100 mm links");
    const bool status_Valid = parsed.status == ParseStatus::Valid; CHECK(status_Valid);
    CHECK(parsed.intent.domain == "robotics");
    CHECK(parsed.intent.operation == "create_robot");
    REQUIRE(parsed.intent.parameters.contains("link_count"));
    CHECK(parsed.intent.parameters["link_count"].get<long long>() == 3);
    REQUIRE(parsed.intent.parameters.contains("link_length_mm"));
    CHECK(std::fabs(parsed.intent.parameters["link_length_mm"].get<double>() - 100.0) < 1e-9);
    REQUIRE_FALSE(parsed.intent.assumptions.empty());
}

TEST_CASE("robotics intent: FK joint angles in degrees normalize to radians") {
    RequirementParser parser;
    const auto parsed =
        parser.parse("Compute forward kinematics for joint angles 30 and 45 degrees");
    const bool status_Valid = parsed.status == ParseStatus::Valid; CHECK(status_Valid);
    CHECK(parsed.intent.domain == "robotics");
    CHECK(parsed.intent.operation == "forward_kinematics");
    REQUIRE(parsed.intent.parameters.contains("joint_angles"));
    const auto& angles = parsed.intent.parameters["joint_angles"];
    REQUIRE(angles.size() == 2);
    CHECK(std::fabs(angles[0].get<double>() - 0.5235987755982988) < 1e-9);
    CHECK(std::fabs(angles[1].get<double>() - 0.7853981633974483) < 1e-9);
    CHECK(parsed.intent.rawMetadata.value("original_units", "") == "degrees");
    REQUIRE(parsed.intent.rawMetadata.contains("joint_angles_original"));
    CHECK(parsed.intent.rawMetadata["joint_angles_original"][0].get<double>() == 30.0);
}

TEST_CASE("robotics intent: IK without a z coordinate is incomplete") {
    RequirementParser parser;
    const auto parsed = parser.parse("Solve inverse kinematics to reach the point 200, 0");
    const bool status_Incomplete = parsed.status == ParseStatus::Incomplete; CHECK(status_Incomplete);
    CHECK(parsed.intent.domain == "robotics");
    CHECK(parsed.intent.operation == "inverse_kinematics");
    CHECK(missingContains(parsed.intent, "target_z_mm"));
    REQUIRE_FALSE(parsed.errors.empty());
}

TEST_CASE("robotics intent: IK with a full point is valid") {
    RequirementParser parser;
    const auto parsed =
        parser.parse("Solve inverse kinematics to reach the point 200, 0, 150");
    const bool status_Valid = parsed.status == ParseStatus::Valid; CHECK(status_Valid);
    CHECK(parsed.intent.domain == "robotics");
    CHECK(parsed.intent.operation == "inverse_kinematics");
    REQUIRE(parsed.intent.parameters.contains("target_xyz_mm"));
    const auto& target = parsed.intent.parameters["target_xyz_mm"];
    REQUIRE(target.size() == 3);
    CHECK(target[0].get<double>() == 200.0);
    CHECK(target[1].get<double>() == 0.0);
    CHECK(target[2].get<double>() == 150.0);
    CHECK(parsed.intent.rawMetadata.value("original_units", "") == "millimeters");
}

TEST_CASE("robotics intent: generate phrasing routes to generate_trajectory") {
    RequirementParser parser;
    const auto parsed = parser.parse(
        "Generate a joint trajectory from 0,0 to 1,0.5 over 2 seconds");
    const bool status_Valid = parsed.status == ParseStatus::Valid; CHECK(status_Valid);
    CHECK(parsed.intent.domain == "robotics");
    CHECK(parsed.intent.operation == "generate_trajectory");
    REQUIRE(parsed.intent.parameters.contains("joint_start"));
    REQUIRE(parsed.intent.parameters.contains("joint_goal"));
    CHECK(parsed.intent.parameters["joint_start"].size() == 2);
    CHECK(parsed.intent.parameters["joint_goal"].size() == 2);
    REQUIRE(parsed.intent.parameters.contains("duration_s"));
    CHECK(std::fabs(parsed.intent.parameters["duration_s"].get<double>() - 2.0) < 1e-9);
}

TEST_CASE("robotics intent: calculate end-effector position with degree angles") {
    RequirementParser parser;
    const auto parsed = parser.parse(
        "Calculate the end-effector position for joint angles 30 and 45 degrees");
    const bool status_Valid = parsed.status == ParseStatus::Valid; CHECK(status_Valid);
    CHECK(parsed.intent.domain == "robotics");
    CHECK(parsed.intent.operation == "forward_kinematics");
    REQUIRE(parsed.intent.parameters.contains("joint_angles"));
    const auto& angles = parsed.intent.parameters["joint_angles"];
    REQUIRE(angles.size() == 2);
    CHECK(std::fabs(angles[0].get<double>() - 0.5235987755982988) < 1e-9);
    CHECK(std::fabs(angles[1].get<double>() - 0.7853981633974483) < 1e-9);
    CHECK(parsed.intent.rawMetadata.value("original_units", "") == "degrees");
}

TEST_CASE("robotics intent: move end effector with x and y reports missing z") {
    RequirementParser parser;
    const auto parsed = parser.parse("Move the robot end effector to x=100 mm y=50 mm");
    const bool status_Incomplete = parsed.status == ParseStatus::Incomplete; CHECK(status_Incomplete);
    CHECK(parsed.intent.domain == "robotics");
    CHECK(parsed.intent.operation == "inverse_kinematics");
    CHECK(missingContains(parsed.intent, "target_z_mm"));
    REQUIRE_FALSE(parsed.errors.empty());
}

TEST_CASE("robotics intent: move end effector with x, y and z is valid") {
    RequirementParser parser;
    const auto parsed =
        parser.parse("Move the robot end effector to x=100 mm y=50 mm z=20 mm");
    const bool status_Valid = parsed.status == ParseStatus::Valid; CHECK(status_Valid);
    CHECK(parsed.intent.domain == "robotics");
    CHECK(parsed.intent.operation == "inverse_kinematics");
    REQUIRE(parsed.intent.parameters.contains("target_xyz_mm"));
    const auto& target = parsed.intent.parameters["target_xyz_mm"];
    REQUIRE(target.size() == 3);
    CHECK(target[0].get<double>() == 100.0);
    CHECK(target[1].get<double>() == 50.0);
    CHECK(target[2].get<double>() == 20.0);
}

TEST_CASE("robotics intent: trajectory with degree endpoints normalizes to radians") {
    RequirementParser parser;
    const auto parsed = parser.parse(
        "Generate a joint trajectory from 0 degrees to 90 degrees in 2 seconds");
    const bool status_Valid = parsed.status == ParseStatus::Valid; CHECK(status_Valid);
    CHECK(parsed.intent.domain == "robotics");
    CHECK(parsed.intent.operation == "generate_trajectory");
    REQUIRE(parsed.intent.parameters.contains("joint_start"));
    REQUIRE(parsed.intent.parameters.contains("joint_goal"));
    CHECK(std::fabs(parsed.intent.parameters["joint_start"][0].get<double>()) < 1e-12);
    CHECK(std::fabs(parsed.intent.parameters["joint_goal"][0].get<double>() -
                    1.5707963267948966) < 1e-9);
    REQUIRE(parsed.intent.parameters.contains("duration_s"));
    CHECK(std::fabs(parsed.intent.parameters["duration_s"].get<double>() - 2.0) < 1e-9);
    CHECK(parsed.intent.rawMetadata.value("original_units", "") == "degrees");
    REQUIRE(parsed.intent.rawMetadata.contains("joint_goal_original"));
    CHECK(parsed.intent.rawMetadata["joint_goal_original"][0].get<double>() == 90.0);
}
