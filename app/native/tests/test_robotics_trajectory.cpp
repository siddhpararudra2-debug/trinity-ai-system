#include <doctest.h>

#include <cmath>
#include <limits>

#include "trinity/robotics/Kinematics.hpp"
#include "trinity/robotics/Types.hpp"

namespace {

trinity::robotics::RobotProject makeLimitedArm() {
    using namespace trinity::robotics;
    RobotProject p;
    p.robotName = "limited";
    p.model.links = {Link{"base_link", 0.0}, Link{"link_1", 0.1}, Link{"link_2", 0.1}};
    Joint j1;
    j1.name = "joint_1";
    j1.type = JointType::Revolute;
    j1.parentLink = "base_link";
    j1.childLink = "link_1";
    j1.axis = Vec3{0.0, 0.0, 1.0};
    j1.limit = JointLimit{true, -1.0, 1.0, 0.0};
    Joint j2;
    j2.name = "joint_2";
    j2.type = JointType::Revolute;
    j2.parentLink = "link_1";
    j2.childLink = "link_2";
    j2.axis = Vec3{0.0, 0.0, 1.0};
    j2.originXYZ = Vec3{0.1, 0.0, 0.0};
    j2.limit = JointLimit{true, -1.0, 1.0, 0.0};
    p.model.joints = {j1, j2};
    p.endEffector.parentLink = "link_2";
    p.endEffector.originXYZ = Vec3{0.1, 0.0, 0.0};
    return p;
}

}  // namespace

TEST_CASE("robotics trajectory: linear interpolation endpoints and samples") {
    using namespace trinity::robotics;
    const TrajectoryResult out = generateLinearJointTrajectory(
        nullptr, {"joint_1", "joint_2"}, {0.0, 0.0}, {1.0, 0.5}, 2.0, 0.01);
    REQUIRE(out.success);
    CHECK_FALSE(out.cancelled);
    CHECK(out.error.empty());
    CHECK(out.limitsEnforced == false);
    CHECK(out.withinLimits);
    const Trajectory& traj = out.trajectory;
    CHECK(traj.sampleCount() == 201);
    REQUIRE(traj.timestampsS.size() == 201);
    CHECK(traj.timestampsS.front() == doctest::Approx(0.0));
    CHECK(traj.timestampsS.back() == doctest::Approx(2.0));
    REQUIRE(traj.positions.size() == 2);
    CHECK(traj.positions[0].front() == doctest::Approx(0.0));
    CHECK(traj.positions[0].back() == doctest::Approx(1.0));
    CHECK(traj.positions[1].back() == doctest::Approx(0.5));
    for (const double v : traj.velocities[0]) {
        CHECK(v == doctest::Approx(0.5));
    }
    for (const double v : traj.velocities[1]) {
        CHECK(v == doctest::Approx(0.25));
    }
    CHECK(traj.jointOrder.size() == 2);
    CHECK(traj.jointOrder[0] == "joint_1");
    CHECK(traj.durationS == doctest::Approx(2.0));
    CHECK(traj.dt == doctest::Approx(0.01));
}

TEST_CASE("robotics trajectory: endpoints hold at 1e-9") {
    using namespace trinity::robotics;
    const std::vector<double> start{0.1, -0.2, 0.3};
    const std::vector<double> goal{0.9, 0.4, -0.7};
    const TrajectoryResult out = generateLinearJointTrajectory(
        nullptr, {"a", "b", "c"}, start, goal, 1.0, 0.03);
    REQUIRE(out.success);
    const Trajectory& traj = out.trajectory;
    for (size_t k = 0; k < 3; ++k) {
        CHECK(std::fabs(traj.positions[k].front() - start[k]) < 1e-9);
        CHECK(std::fabs(traj.positions[k].back() - goal[k]) < 1e-9);
    }
    CHECK(traj.timestampsS.back() == doctest::Approx(1.0));
}

TEST_CASE("robotics trajectory: generation is deterministic") {
    using namespace trinity::robotics;
    const TrajectoryResult a = generateLinearJointTrajectory(
        nullptr, {"joint_1"}, {0.0}, {2.0}, 1.0, 0.01);
    const TrajectoryResult b = generateLinearJointTrajectory(
        nullptr, {"joint_1"}, {0.0}, {2.0}, 1.0, 0.01);
    REQUIRE(a.success);
    REQUIRE(b.success);
    REQUIRE(a.trajectory.positions[0].size() == b.trajectory.positions[0].size());
    for (size_t i = 0; i < a.trajectory.positions[0].size(); ++i) {
        CHECK(a.trajectory.positions[0][i] == doctest::Approx(b.trajectory.positions[0][i]));
    }
}

TEST_CASE("robotics trajectory: limit violations are rejected, never clamped") {
    using namespace trinity::robotics;
    const RobotProject arm = makeLimitedArm();
    const TrajectoryResult badGoal = generateLinearJointTrajectory(
        &arm, {"joint_1", "joint_2"}, {0.0, 0.0}, {2.0, 0.0}, 1.0, 0.01);
    CHECK_FALSE(badGoal.success);
    CHECK(badGoal.error.empty() == false);
    const TrajectoryResult badStart = generateLinearJointTrajectory(
        &arm, {"joint_1", "joint_2"}, {0.0, -2.0}, {0.0, 0.0}, 1.0, 0.01);
    CHECK_FALSE(badStart.success);
    CHECK(badStart.error.empty() == false);
    const TrajectoryResult ok = generateLinearJointTrajectory(
        &arm, {"joint_1", "joint_2"}, {-0.5, 0.0}, {0.5, 0.5}, 1.0, 0.01);
    REQUIRE(ok.success);
    CHECK(ok.limitsEnforced);
    CHECK(ok.withinLimits);
}

TEST_CASE("robotics trajectory: velocity limits are enforced") {
    using namespace trinity::robotics;
    RobotProject arm = makeLimitedArm();
    arm.model.joints[0].limit.velocityMax = 0.1;
    const TrajectoryResult out = generateLinearJointTrajectory(
        &arm, {"joint_1", "joint_2"}, {0.0, 0.0}, {1.0, 0.0}, 2.0, 0.01);
    CHECK_FALSE(out.success);
    CHECK(out.error.empty() == false);
}

TEST_CASE("robotics trajectory: bad parameters fail with a message") {
    using namespace trinity::robotics;
    const TrajectoryResult empty = generateLinearJointTrajectory(nullptr, {}, {}, {}, 1.0, 0.01);
    CHECK_FALSE(empty.success);
    CHECK(empty.error.empty() == false);
    const TrajectoryResult mismatch = generateLinearJointTrajectory(
        nullptr, {"joint_1", "joint_2"}, {0.0}, {0.0, 0.0}, 1.0, 0.01);
    CHECK_FALSE(mismatch.success);
    const TrajectoryResult noDuration =
        generateLinearJointTrajectory(nullptr, {"joint_1"}, {0.0}, {1.0}, 0.0, 0.01);
    CHECK_FALSE(noDuration.success);
    const TrajectoryResult badDt =
        generateLinearJointTrajectory(nullptr, {"joint_1"}, {0.0}, {1.0}, 1.0, -1.0);
    CHECK_FALSE(badDt.success);
    const double inf = std::numeric_limits<double>::infinity();
    const TrajectoryResult nonFinite = generateLinearJointTrajectory(
        nullptr, {"joint_1"}, {inf}, {1.0}, 1.0, 0.01);
    CHECK_FALSE(nonFinite.success);
}

TEST_CASE("robotics trajectory: cancellation is honored") {
    using namespace trinity::robotics;
    CancelProbe probe = []() { return true; };
    const TrajectoryResult out = generateLinearJointTrajectory(
        nullptr, {"joint_1"}, {0.0}, {1.0}, 1.0, 0.01, probe);
    CHECK(out.cancelled);
    CHECK_FALSE(out.success);
}

TEST_CASE("robotics trajectory: non-divisible dt still ends exactly at duration") {
    using namespace trinity::robotics;
    const TrajectoryResult out = generateLinearJointTrajectory(
        nullptr, {"joint_1"}, {0.0}, {1.0}, 1.0, 0.3);
    REQUIRE(out.success);
    CHECK(out.trajectory.sampleCount() == 5);
    CHECK(out.trajectory.timestampsS.back() == doctest::Approx(1.0));
    CHECK(out.trajectory.positions[0].back() == doctest::Approx(1.0));
}
