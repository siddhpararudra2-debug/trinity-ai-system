#include <doctest.h>

#include "trinity/simulation/Integrator.hpp"
#include "trinity/simulation/Validators.hpp"

using trinity::simulation::integrate;
using trinity::simulation::SimulationProject;
using trinity::simulation::validateProject;
using trinity::simulation::validateResult;

namespace {

SimulationProject dynamicsProject(double durationS, double forceN, double massG,
                                   double v0 = 0.0) {
    SimulationProject p;
    p.projectId = "proj-dyn";
    p.name = "force-mass";
    p.type = "basic_dynamics";
    p.model = "force_mass";
    p.dt = 0.01;
    p.durationS = durationS;
    p.massKg = massG / 1000.0;
    p.forceN.x = forceN;
    p.initial.velocity.x = v0;
    return p;
}

}  // namespace

TEST_CASE("dynamics uses semi-implicit Euler and positive mass") {
    const auto project = dynamicsProject(2.0, 10.0, 1000.0, 1.0);
    REQUIRE(validateProject(project).ok);
    const auto outcome = integrate(project);
    REQUIRE(outcome.success);
    CHECK(outcome.result.method == "semi_implicit_euler");
    CHECK(outcome.result.stepCount == 200);
    REQUIRE(outcome.result.sampleCount == 201);
    CHECK(outcome.result.summary.value("method", "") == "semi_implicit_euler");
}

TEST_CASE("F=m*a accelerates mass according to force") {
    // 10 N on 1 kg for 2 s: a = 10 m/s^2, vx = 1 + 20 = 21
    const auto project = dynamicsProject(2.0, 10.0, 1000.0, 1.0);
    const auto outcome = integrate(project);
    REQUIRE(outcome.success);
    const auto& finalState = outcome.result.finalState;
    const double a = 10.0 / 1.0;
    const double expectedVx = 1.0 + a * 2.0;
    CHECK(finalState.velocity.x == doctest::Approx(expectedVx).epsilon(1e-6));
    // Semi-implicit Euler position differs slightly from continuous 0.5 a t^2
    // but must be positive and finite for constant force from rest.
    CHECK(finalState.position.x > 0.0);
    CHECK(std::isfinite(finalState.position.x));
    const auto checks = validateResult(project, outcome.result);
    CHECK(checks.ok);
    CHECK(checks.finite);
    CHECK(checks.timeMonotonic);
    CHECK(checks.closedFormAgreement);
}

TEST_CASE("larger mass with same force accelerates less") {
    const auto light = dynamicsProject(1.0, 10.0, 500.0);
    const auto heavy = dynamicsProject(1.0, 10.0, 2000.0);
    REQUIRE(integrate(light).success);
    REQUIRE(integrate(heavy).success);
    const double lightAx = 10.0 / 0.5;
    const double heavyAx = 10.0 / 2.0;
    CHECK(lightAx > heavyAx);
    CHECK(integrate(light).result.finalState.velocity.x ==
          doctest::Approx(lightAx * 1.0).epsilon(1e-6));
    CHECK(integrate(heavy).result.finalState.velocity.x ==
          doctest::Approx(heavyAx * 1.0).epsilon(1e-6));
}

TEST_CASE("zero mass for basic_dynamics is rejected without samples") {
    SimulationProject p = dynamicsProject(1.0, 10.0, 0.0);
    const auto projectCheck = validateProject(p);
    CHECK_FALSE(projectCheck.ok);
    const auto outcome = integrate(p);
    CHECK_FALSE(outcome.success);
    CHECK_FALSE(outcome.cancelled);
    CHECK(outcome.result.samples.empty());
}
