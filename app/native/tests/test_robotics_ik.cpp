#include <doctest.h>

#include <cmath>
#include <limits>

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

const double kQ1 = 0.5235987755982988;
const double kQ2 = -0.2617993877991494;

}  // namespace

TEST_CASE("robotics FK: zero state puts the arm along +x") {
    using namespace trinity::robotics;
    const RobotProject arm = makeArm();
    const FkResult fk = computeForwardKinematics(arm, JointState{});
    REQUIRE(fk.success);
    CHECK(fk.linkCount == 3);
    CHECK(fk.jointCount == 2);
    CHECK(fk.actuatedCount == 2);
    CHECK(fk.endEffectorPose.positionM.x == doctest::Approx(0.2));
    CHECK(fk.endEffectorPose.positionM.y == doctest::Approx(0.0));
    CHECK(fk.endEffectorPose.positionM.z == doctest::Approx(0.0));
    REQUIRE(fk.frameOrder.empty() == false);
    CHECK(fk.frameOrder.front() == "world");
    CHECK(fk.frameTransforms.count("base_link") == 1);
    CHECK(fk.frameTransforms.count("tool0") == 1);
}

TEST_CASE("robotics FK: two-link pose matches the analytic chain") {
    using namespace trinity::robotics;
    const RobotProject arm = makeArm();
    JointState state;
    state.positions["joint_1"] = kQ1;
    state.positions["joint_2"] = kQ2;
    const FkResult fk = computeForwardKinematics(arm, state);
    REQUIRE(fk.success);
    CHECK(fk.endEffectorPose.positionM.x == doctest::Approx(0.183195));
    CHECK(fk.endEffectorPose.positionM.y == doctest::Approx(0.075882));
    CHECK(fk.endEffectorPose.positionM.z == doctest::Approx(0.0));
}

TEST_CASE("robotics FK: joints missing from the state default to zero") {
    using namespace trinity::robotics;
    const RobotProject arm = makeArm();
    JointState partial;
    partial.positions["joint_1"] = 0.5;
    JointState full = partial;
    full.positions["joint_2"] = 0.0;
    const FkResult a = computeForwardKinematics(arm, partial);
    const FkResult b = computeForwardKinematics(arm, full);
    REQUIRE(a.success);
    REQUIRE(b.success);
    CHECK(a.endEffectorPose.positionM.x == doctest::Approx(b.endEffectorPose.positionM.x));
    CHECK(a.endEffectorPose.positionM.y == doctest::Approx(b.endEffectorPose.positionM.y));
    CHECK(a.endEffectorPose.positionM.z == doctest::Approx(b.endEffectorPose.positionM.z));
}

TEST_CASE("robotics IK: reachable target converges and verifies by FK") {
    using namespace trinity::robotics;
    const RobotProject arm = makeArm();
    const Vec3 target{0.183195, 0.075882, 0.0};
    IkOptions options;
    const IkResult ik = solveInverseKinematics(arm, target, JointState{}, options);
    CHECK(ik.converged);
    CHECK_FALSE(ik.cancelled);
    CHECK(ik.positionOnly);
    CHECK(ik.withinLimits);
    CHECK(ik.finalErrorM <= options.toleranceM);
    REQUIRE(ik.solution.positions.count("joint_1") == 1);
    REQUIRE(ik.solution.positions.count("joint_2") == 1);
    // Independent verification: FK at the solution must reproduce the target.
    const FkResult verify = computeForwardKinematics(arm, ik.solution);
    REQUIRE(verify.success);
    const Vec3& reached = verify.endEffectorPose.positionM;
    const double error =
        std::sqrt((reached.x - target.x) * (reached.x - target.x) +
                  (reached.y - target.y) * (reached.y - target.y) +
                  (reached.z - target.z) * (reached.z - target.z));
    CHECK(error <= options.toleranceM);
}

TEST_CASE("robotics IK: unreachable target reports non-convergence truthfully") {
    using namespace trinity::robotics;
    const RobotProject arm = makeArm();
    IkOptions options;
    options.maxIterations = 50;
    const IkResult ik =
        solveInverseKinematics(arm, Vec3{10.0, 0.0, 0.0}, JointState{}, options);
    CHECK_FALSE(ik.converged);
    CHECK_FALSE(ik.cancelled);
    CHECK(ik.iterations == 50);
    CHECK(ik.error.rfind("IK did not converge", 0) == 0);
    CHECK(ik.solution.positions.empty() == false);
    CHECK(ik.finalErrorM > options.toleranceM);
}

TEST_CASE("robotics IK: iterates stay clamped to joint limits") {
    using namespace trinity::robotics;
    RobotProject arm = makeArm();
    for (auto& joint : arm.model.joints) {
        joint.limit = JointLimit{true, -0.1, 0.1, 0.0};
    }
    IkOptions options;
    const IkResult ik =
        solveInverseKinematics(arm, Vec3{0.0, 0.19, 0.0}, JointState{}, options);
    CHECK(ik.withinLimits);
    for (const auto& [name, value] : ik.solution.positions) {
        CHECK(value >= -0.1 - 1e-9);
        CHECK(value <= 0.1 + 1e-9);
    }
}

TEST_CASE("robotics IK: non-finite target is rejected without solving") {
    using namespace trinity::robotics;
    const RobotProject arm = makeArm();
    const double inf = std::numeric_limits<double>::infinity();
    const IkResult ik =
        solveInverseKinematics(arm, Vec3{inf, 0.0, 0.0}, JointState{}, IkOptions{});
    CHECK_FALSE(ik.converged);
    CHECK(ik.error == "IK target must be finite");
    CHECK(ik.solution.positions.empty());
}

TEST_CASE("robotics IK: cancelled solve reports cancellation") {
    using namespace trinity::robotics;
    const RobotProject arm = makeArm();
    IkOptions options;
    options.cancelCheck = []() { return true; };
    const IkResult ik =
        solveInverseKinematics(arm, Vec3{0.15, 0.05, 0.0}, JointState{}, options);
    CHECK(ik.cancelled);
    CHECK_FALSE(ik.converged);
    CHECK(ik.error == "IK cancelled by request");
}
