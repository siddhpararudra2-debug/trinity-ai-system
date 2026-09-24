#include <doctest.h>

#include <cmath>

#include "trinity/simulation/Integrator.hpp"
#include "trinity/simulation/Validators.hpp"

using trinity::simulation::integrate;
using trinity::simulation::SimulationProject;
using trinity::simulation::validateProject;
using trinity::simulation::validateResult;

namespace {

SimulationProject linearProject(double duration, double v0, double ax) {
    SimulationProject p;
    p.projectId = "proj-linear";
    p.name = "linear";
    p.type = "kinematics";
    p.model = "linear_motion";
    p.dt = 0.01;
    p.durationS = duration;
    p.initial.velocity.x = v0;
    p.initial.acceleration.x = ax;
    return p;
}

SimulationProject projectileProject(double duration, double speed, double angleDeg) {
    SimulationProject p;
    p.projectId = "proj-proj";
    p.name = "projectile";
    p.type = "kinematics";
    p.model = "projectile";
    p.dt = 0.01;
    p.durationS = duration;
    const double rad = angleDeg * 3.14159265358979323846 / 180.0;
    p.initial.velocity = {speed * std::cos(rad), speed * std::sin(rad), 0.0};
    return p;
}

SimulationProject constantAccelProject(double duration, double v0, double ax) {
    SimulationProject p = linearProject(duration, v0, ax);
    p.model = "constant_acceleration";
    p.projectId = "proj-const";
    return p;
}

}  // namespace

TEST_CASE("linear motion closed form matches x = x0 + v0 t + 0.5 a t^2") {
    const auto project = linearProject(2.0, 5.0, 2.0);
    REQUIRE(validateProject(project).ok);
    const auto outcome = integrate(project);
    REQUIRE(outcome.success);
    CHECK_FALSE(outcome.cancelled);
    CHECK(outcome.result.method == "closed_form");
    CHECK(outcome.result.stepCount == 200);
    REQUIRE(outcome.result.samples.size() == 201);
    const auto& finalState = outcome.result.finalState;
    const double expectedX = 5.0 * 2.0 + 0.5 * 2.0 * 2.0 * 2.0;
    CHECK(finalState.position.x == doctest::Approx(expectedX).epsilon(1e-12));
    CHECK(finalState.velocity.x == doctest::Approx(5.0 + 2.0 * 2.0).epsilon(1e-12));
    const auto checks = validateResult(project, outcome.result);
    CHECK(checks.ok);
    CHECK(checks.finite);
    CHECK(checks.timeMonotonic);
    CHECK(checks.closedFormAgreement);
}

TEST_CASE("projectile closed form uses gravity on y and keeps vx constant") {
    const auto project = projectileProject(3.0, 20.0, 30.0);
    REQUIRE(validateProject(project).ok);
    const auto outcome = integrate(project);
    REQUIRE(outcome.success);
    CHECK(outcome.result.method == "closed_form");
    const auto& finalState = outcome.result.finalState;
    const double rad = 30.0 * 3.14159265358979323846 / 180.0;
    const double vx0 = 20.0 * std::cos(rad);
    const double vy0 = 20.0 * std::sin(rad);
    const double t = 3.0;
    const double g = project.gravity;
    CHECK(finalState.position.x == doctest::Approx(vx0 * t).epsilon(1e-9));
    CHECK(finalState.position.y == doctest::Approx(vy0 * t - 0.5 * g * t * t).epsilon(1e-9));
    CHECK(finalState.velocity.x == doctest::Approx(vx0).epsilon(1e-9));
    CHECK(finalState.acceleration.y == doctest::Approx(-g).epsilon(1e-12));
}

TEST_CASE("constant acceleration closed form agrees with linear model") {
    const auto project = constantAccelProject(1.5, 3.0, -4.0);
    REQUIRE(validateProject(project).ok);
    const auto outcome = integrate(project);
    REQUIRE(outcome.success);
    const auto& finalState = outcome.result.finalState;
    const double expectedX = 3.0 * 1.5 + 0.5 * -4.0 * 1.5 * 1.5;
    CHECK(finalState.position.x == doctest::Approx(expectedX).epsilon(1e-12));
    CHECK(finalState.velocity.x == doctest::Approx(3.0 + -4.0 * 1.5).epsilon(1e-12));
    const auto checks = validateResult(project, outcome.result);
    CHECK(checks.ok);
    CHECK(checks.closedFormAgreement);
}

TEST_CASE("zero acceleration constant motion keeps velocity and position consistent") {
    const auto project = linearProject(1.0, 7.0, 0.0);
    const auto outcome = integrate(project);
    REQUIRE(outcome.success);
    CHECK(outcome.result.finalState.position.x == doctest::Approx(7.0).epsilon(1e-12));
    CHECK(outcome.result.finalState.velocity.x == doctest::Approx(7.0).epsilon(1e-12));
    const auto checks = validateResult(project, outcome.result);
    CHECK(checks.ok);
}
