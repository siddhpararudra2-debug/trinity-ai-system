// RK4 integrator accuracy and the multi-body spring chain.

#include <doctest.h>

#include <cmath>
#include <string>

#include "trinity/core/Json.hpp"
#include "trinity/engines/SimulationEngine.hpp"
#include "trinity/simulation/Integrator.hpp"
#include "trinity/simulation/Validators.hpp"

using trinity::simulation::integrate;
using trinity::simulation::SimulationObject;
using trinity::simulation::SimulationProject;
using trinity::simulation::validateProject;
using trinity::simulation::validateResult;

namespace {

/// x0 = 1 m, v0 = 2 m/s, constant force → a = F/m. The exact solution is
/// x(t) = x0 + v0 t + a t²/2; RK4 must hit it to ~machine precision while
/// semi-implicit Euler visibly deviates.
SimulationProject rk4Dynamics(double dt) {
    SimulationProject p;
    p.projectId = "proj-rk4";
    p.name = "rk4-accuracy";
    p.type = "basic_dynamics";
    p.model = "force_mass";
    p.dt = dt;
    p.durationS = 2.0;
    p.massKg = 2.0;
    p.forceN.x = 4.0;  // a = 2 m/s^2
    p.initial.position.x = 1.0;
    p.initial.velocity.x = 2.0;
    p.method = "rk4";
    return p;
}

SimulationProject eulerDynamics(double dt) {
    SimulationProject p = rk4Dynamics(dt);
    p.method = "semi_implicit_euler";
    return p;
}

SimulationProject springChain() {
    // Two 1 kg masses, x positions 0 and 0.5 m, rest length 1.0 m,
    // k = 10 N/m. Both drift toward each other; total energy (KE + PE)
    // is conserved by the undamped Hooke system.
    SimulationProject p;
    p.projectId = "proj-chain";
    p.name = "spring-chain";
    p.type = "multi_body";
    p.model = "spring_chain";
    p.dt = 0.001;
    p.durationS = 1.0;
    p.method = "rk4";
    p.springKNm = 10.0;
    p.restLengthM = 1.0;
    SimulationObject a;
    a.objectId = "object-0";
    a.massKg = 1.0;
    a.initial.position = {0.0, 0.0, 0.0};
    a.initial.velocity = {0.1, 0.0, 0.0};
    SimulationObject b;
    b.objectId = "object-1";
    b.massKg = 1.0;
    b.initial.position = {0.5, 0.0, 0.0};
    p.objects = {a, b};
    return p;
}

}  // namespace

TEST_CASE("RK4 force_mass matches the closed-form trajectory") {
    const auto project = rk4Dynamics(0.01);
    REQUIRE(validateProject(project).ok);
    const auto outcome = integrate(project);
    REQUIRE(outcome.success);
    CHECK(outcome.result.method == "rk4");

    const double a = 4.0 / 2.0;
    const double t = 2.0;
    const double exactX = 1.0 + 2.0 * t + 0.5 * a * t * t;
    const double exactVx = 2.0 + a * t;
    CHECK(outcome.result.finalState.position.x ==
          doctest::Approx(exactX).epsilon(1e-9));
    CHECK(outcome.result.finalState.velocity.x ==
          doctest::Approx(exactVx).epsilon(1e-9));

    // Semi-implicit Euler at the same dt deviates measurably.
    const auto euler = integrate(eulerDynamics(0.01));
    REQUIRE(euler.success);
    CHECK(std::fabs(euler.result.finalState.position.x - exactX) > 1e-4);
}

TEST_CASE("coefficients of RK4 dynamics propagate into validation") {
    const auto project = rk4Dynamics(0.01);
    const auto outcome = integrate(project);
    REQUIRE(outcome.success);
    const auto checks = validateResult(project, outcome.result);
    CHECK(checks.ok);
    CHECK(checks.closedFormAgreement);
}

TEST_CASE("spring chain conserves total energy under RK4") {
    const auto project = springChain();
    REQUIRE(validateProject(project).ok);
    const auto outcome = integrate(project);
    REQUIRE(outcome.success);
    CHECK(outcome.result.method == "rk4");
    REQUIRE(outcome.result.summary.contains("final_states"));
    CHECK(outcome.result.summary["final_states"].is_array());
    CHECK(outcome.result.summary["final_states"].size() == 2);

    const double total =
        outcome.result.summary.value("total_energy_J", 0.0);
    // Initial energy: KE = 0.5·1·0.1² = 0.005 J; PE = 0.5·10·(0.5−1.0)²
    // = 1.25 J → 1.255 J.
    CHECK(total == doctest::Approx(1.255).epsilon(1e-6));
    CHECK(outcome.result.summary.value("kinetic_energy_J", 0.0) >= 0.0);
    CHECK(outcome.result.summary.value("spring_potential_energy_J", 0.0) >= 0.0);

    const auto checks = validateResult(project, outcome.result);
    CHECK(checks.ok);
    CHECK(checks.closedFormAgreement);
}

TEST_CASE("spring chain validation rejects bad couplings") {
    auto project = springChain();
    project.springKNm = -1.0;
    CHECK_FALSE(validateProject(project).ok);
    project = springChain();
    project.restLengthM = 0.0;
    CHECK_FALSE(validateProject(project).ok);
    project = springChain();
    project.objects.pop_back();
    CHECK_FALSE(validateProject(project).ok);  // needs ≥ 2 bodies
}

TEST_CASE("engine simulate_spring_chain one-shot reports the capability") {
    trinity::engines::SimulationEngine engine;
    const auto describe = engine.execute([] {
        trinity::engines::EngineRequest req;
        req.engine = "simulation";
        req.operation = "describe";
        return req;
    }());
    REQUIRE(describe.success);
    const auto& caps = describe.result["capabilities"];
    bool listed = false;
    for (const auto& cap : caps) {
        if (cap == "simulate_spring_chain") listed = true;
    }
    CHECK(listed);
    MESSAGE("DESCRIBE_OK");
}

TEST_CASE("engine simulate_spring_chain runs RK4 and reports energy") {
    trinity::engines::SimulationEngine engine;
    trinity::engines::EngineRequest run;
    run.engine = "simulation";
    run.operation = "simulate_spring_chain";
    run.parameters = {{"model", "spring_chain"},
                      {"method", "rk4"},
                      {"dt", 0.001},
                      {"duration_s", 1.0},
                      {"spring_k_n_m", 10.0},
                      {"rest_length_m", 1.0},
                      {"objects",
                       {{{"mass_kg", 1.0},
                         {"position_m", {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}}},
                         {"velocity_m_s", {{"x", 0.1}, {"y", 0.0}, {"z", 0.0}}}},
                        {{"mass_kg", 1.0},
                         {"position_m", {{"x", 0.5}, {"y", 0.0}, {"z", 0.0}}}}}}};
    try {
        const auto out = engine.execute(run);
        REQUIRE(out.success);
        const auto summary = out.result.value("result", trinity::core::Json::object())
                                 .value("summary", trinity::core::Json::object());
        MESSAGE(summary.dump().c_str());
        CHECK(summary.value("total_energy_J", 0.0) ==
              doctest::Approx(1.255).epsilon(1e-6));
        REQUIRE(summary.contains("final_states"));
        CHECK(static_cast<int>(summary["final_states"].size()) == 2);
    } catch (const std::exception& e) {
        FAIL_CHECK(e.what());
    }
}
