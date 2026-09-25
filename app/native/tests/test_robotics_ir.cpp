#include <doctest.h>

#include <cmath>
#include <stdexcept>

#include "trinity/robotics/Kinematics.hpp"
#include "trinity/robotics/Types.hpp"

namespace {

trinity::robotics::RobotProject makeArm() {
    using namespace trinity::robotics;
    RobotProject p;
    p.robotName = "test_arm";
    p.baseFrame = "world";
    p.model.name = "test_arm";
    p.model.baseFrame = "world";
    p.model.links = {Link{"base_link", 0.0}, Link{"link_1", 0.1}, Link{"link_2", 0.1}};
    Joint j1;
    j1.name = "joint_1";
    j1.type = JointType::Revolute;
    j1.parentLink = "base_link";
    j1.childLink = "link_1";
    j1.axis = Vec3{0.0, 0.0, 1.0};
    j1.originXYZ = Vec3{0.0, 0.0, 0.0};
    Joint j2;
    j2.name = "joint_2";
    j2.type = JointType::Revolute;
    j2.parentLink = "link_1";
    j2.childLink = "link_2";
    j2.axis = Vec3{0.0, 0.0, 1.0};
    j2.originXYZ = Vec3{0.1, 0.0, 0.0};
    p.model.joints = {j1, j2};
    p.endEffector.name = "tool0";
    p.endEffector.parentLink = "link_2";
    p.endEffector.originXYZ = Vec3{0.1, 0.0, 0.0};
    return p;
}

}  // namespace

TEST_CASE("robotics IR: transform compose and inverse") {
    using namespace trinity::robotics;
    const double halfPi = 1.5707963267948966;
    const Transform t =
        Transform::fromTranslation(Vec3{1.0, 2.0, 3.0})
            .compose(Transform::fromAxisAngle(Vec3{0.0, 0.0, 1.0}, halfPi));
    const Vec3 p = t.transformPoint(Vec3{1.0, 0.0, 0.0});
    CHECK(p.x == doctest::Approx(1.0));
    CHECK(p.y == doctest::Approx(3.0));
    CHECK(p.z == doctest::Approx(3.0));
    const Vec3 back = t.inverse().transformPoint(p);
    CHECK(back.x == doctest::Approx(1.0));
    CHECK(back.y == doctest::Approx(0.0));
    CHECK(back.z == doctest::Approx(0.0));

    const Transform roundTripped = Transform::fromJson(t.toJson());
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            CHECK(roundTripped.m[static_cast<size_t>(r)][static_cast<size_t>(c)] ==
                  doctest::Approx(t.m[static_cast<size_t>(r)][static_cast<size_t>(c)]));
        }
    }
}

TEST_CASE("robotics IR: joint type strings round-trip and reject unknown") {
    using namespace trinity::robotics;
    CHECK(toString(JointType::Revolute) == "revolute");
    CHECK(toString(JointType::Prismatic) == "prismatic");
    CHECK(toString(JointType::Fixed) == "fixed");
    bool revolute = jointTypeFromString("revolute") == JointType::Revolute;
    bool prismatic = jointTypeFromString("prismatic") == JointType::Prismatic;
    bool fixed = jointTypeFromString("fixed") == JointType::Fixed;
    CHECK(revolute);
    CHECK(prismatic);
    CHECK(fixed);
    CHECK_THROWS_AS(jointTypeFromString("ball"), std::invalid_argument);
    CHECK_THROWS_AS(jointTypeFromString(""), std::invalid_argument);
}

TEST_CASE("robotics IR: Vec3, JointState, and Pose JSON round-trips") {
    using namespace trinity::robotics;
    const Vec3 v{1.0, -2.5, 3.25};
    const Vec3 vBack = Vec3::fromJson(v.toJson());
    CHECK(vBack.x == doctest::Approx(1.0));
    CHECK(vBack.y == doctest::Approx(-2.5));
    CHECK(vBack.z == doctest::Approx(3.25));

    JointState state;
    state.positions["joint_1"] = 0.5;
    state.positions["joint_2"] = -0.25;
    state.velocities["joint_1"] = 0.1;
    const JointState stateBack = JointState::fromJson(state.toJson());
    CHECK(stateBack.positions.at("joint_1") == doctest::Approx(0.5));
    CHECK(stateBack.positions.at("joint_2") == doctest::Approx(-0.25));
    CHECK(stateBack.velocities.at("joint_1") == doctest::Approx(0.1));
    bool found = false;
    CHECK(stateBack.positionOf("joint_1", found) == doctest::Approx(0.5));
    CHECK(found);
    const double missing = stateBack.positionOf("joint_9", found);
    CHECK(missing == doctest::Approx(0.0));
    CHECK_FALSE(found);

    const Pose identity = Pose::fromTransform(Transform::identity());
    CHECK(identity.positionM.x == doctest::Approx(0.0));
    CHECK(identity.positionM.y == doctest::Approx(0.0));
    CHECK(identity.positionM.z == doctest::Approx(0.0));
    const Pose poseBack = Pose::fromJson(identity.toJson());
    CHECK(poseBack.positionM.x == doctest::Approx(0.0));
    CHECK(poseBack.rotation[0][0] == doctest::Approx(1.0));
    CHECK(poseBack.rotation[1][1] == doctest::Approx(1.0));
    CHECK(poseBack.rotation[2][2] == doctest::Approx(1.0));
    CHECK(poseBack.rotation[0][1] == doctest::Approx(0.0));
}

TEST_CASE("robotics IR: model lookup and actuated order") {
    using namespace trinity::robotics;
    const RobotProject arm = makeArm();
    REQUIRE(arm.model.findLink("link_1") != nullptr);
    CHECK(arm.model.findLink("link_1")->lengthM == doctest::Approx(0.1));
    CHECK(arm.model.findLink("nope") == nullptr);
    REQUIRE(arm.model.findJoint("joint_2") != nullptr);
    CHECK(arm.model.findJoint("joint_2")->parentLink == "link_1");
    CHECK(arm.model.findJoint("nope") == nullptr);

    std::vector<std::string> order;
    std::string error;
    REQUIRE(actuatedJointOrder(arm, order, error));
    REQUIRE(order.size() == 2);
    CHECK(order[0] == "joint_1");
    CHECK(order[1] == "joint_2");

    RobotProject withFixed = arm;
    Joint fixed;
    fixed.name = "weld";
    fixed.type = JointType::Fixed;
    fixed.parentLink = "link_2";
    fixed.childLink = "link_2";
    withFixed.model.joints.push_back(fixed);
    REQUIRE(actuatedJointOrder(withFixed, order, error));
    CHECK(order.size() == 2);
}

TEST_CASE("robotics IR: project JSON round-trip preserves model and state") {
    using namespace trinity::robotics;
    RobotProject arm = makeArm();
    arm.projectId = "proj-1";
    arm.initialState.positions["joint_1"] = 0.3;
    arm.initialState.positions["joint_2"] = -0.1;
    arm.model.joints[0].limit = JointLimit{true, -1.0, 1.0, 0.0};
    arm.metadata["origin"] = "test";

    const RobotProject back = RobotProject::fromJson(arm.toJson());
    CHECK(back.projectId == "proj-1");
    CHECK(back.robotName == "test_arm");
    CHECK(back.baseFrame == "world");
    REQUIRE(back.model.links.size() == 3);
    REQUIRE(back.model.joints.size() == 2);
    CHECK(back.model.joints[1].originXYZ.x == doctest::Approx(0.1));
    CHECK(back.model.joints[1].axis.z == doctest::Approx(1.0));
    CHECK(back.initialState.positions.at("joint_1") == doctest::Approx(0.3));
    CHECK(back.endEffector.parentLink == "link_2");
    CHECK(back.metadata.value("origin", "") == "test");
    REQUIRE(back.jointLimits.count("joint_1") == 1);
    CHECK(back.jointLimits.at("joint_1").lower == doctest::Approx(-1.0));
    CHECK(back.jointLimits.at("joint_1").upper == doctest::Approx(1.0));
}

TEST_CASE("robotics IR: documented engine limits") {
    CHECK(trinity::robotics::kMaxJoints == 12);
    CHECK(trinity::robotics::kMaxLinks == 16);
    CHECK(trinity::robotics::kDefaultLinkLengthM == doctest::Approx(0.1));
}

TEST_CASE("robotics IR: prismatic FK translates along its axis") {
    using namespace trinity::robotics;
    RobotProject p;
    p.robotName = "slider";
    p.model.links = {Link{"base_link", 0.0}, Link{"link_1", 0.1}};
    Joint j;
    j.name = "slide";
    j.type = JointType::Prismatic;
    j.parentLink = "base_link";
    j.childLink = "link_1";
    j.axis = Vec3{1.0, 0.0, 0.0};
    p.model.joints = {j};
    p.endEffector.name = "tool0";
    p.endEffector.parentLink = "link_1";
    p.endEffector.originXYZ = Vec3{0.1, 0.0, 0.0};

    JointState state;
    state.positions["slide"] = 0.05;
    const FkResult fk = computeForwardKinematics(p, state);
    REQUIRE(fk.success);
    CHECK(fk.actuatedCount == 1);
    CHECK(fk.endEffectorPose.positionM.x == doctest::Approx(0.15));
    CHECK(fk.endEffectorPose.positionM.y == doctest::Approx(0.0));
    CHECK(fk.endEffectorPose.positionM.z == doctest::Approx(0.0));
}
