#include <doctest.h>

#include "trinity/engines/SimulationEngine.hpp"
#include "trinity/simulation/Integrator.hpp"
#include "trinity/simulation/Types.hpp"

using trinity::engines::EngineRequest;
using trinity::engines::SimulationEngine;
using trinity::simulation::integrate;
using trinity::simulation::SimulationProject;

namespace {

SimulationProject makeLinear(double duration = 1.0) {
    SimulationProject p;
    p.projectId = "det-linear";
    p.name = "linear";
    p.type = "kinematics";
    p.model = "linear_motion";
    p.dt = 0.01;
    p.durationS = duration;
    p.initial.velocity.x = 5.0;
    p.initial.acceleration.x = 2.0;
    return p;
}

SimulationProject makeDynamics(double duration = 1.0) {
    SimulationProject p;
    p.projectId = "det-dyn";
    p.name = "dyn";
    p.type = "basic_dynamics";
    p.model = "force_mass";
    p.dt = 0.01;
    p.durationS = duration;
    p.massKg = 1.0;
    p.forceN.x = 10.0;
    return p;
}

EngineRequest linearRequest(const std::string& op, double duration) {
    EngineRequest req;
    req.engine = "simulation";
    req.operation = op;
    req.parameters = {{"duration_s", duration},
                      {"dt", 0.01},
                      {"initial_velocity_m_s", 5.0},
                      {"acceleration_m_s2", 2.0},
                      {"write_artifacts", false}};
    return req;
}

}  // namespace

TEST_CASE("integrator is deterministic for the same project") {
    const auto a = integrate(makeLinear(2.0));
    const auto b = integrate(makeLinear(2.0));
    REQUIRE(a.success);
    REQUIRE(b.success);
    CHECK(a.result.stepCount == b.result.stepCount);
    CHECK(a.result.sampleCount == b.result.sampleCount);
    REQUIRE(a.result.samples.size() == b.result.samples.size());
    for (size_t i = 0; i < a.result.samples.size(); ++i) {
        CHECK(a.result.samples[i].t == doctest::Approx(b.result.samples[i].t));
        CHECK(a.result.samples[i].position.x == doctest::Approx(b.result.samples[i].position.x));
        CHECK(a.result.samples[i].velocity.x == doctest::Approx(b.result.samples[i].velocity.x));
    }
}

TEST_CASE("integrator is deterministic for dynamics") {
    const auto a = integrate(makeDynamics(1.5));
    const auto b = integrate(makeDynamics(1.5));
    REQUIRE(a.success);
    REQUIRE(b.success);
    CHECK(a.result.finalState.velocity.x == doctest::Approx(b.result.finalState.velocity.x));
    CHECK(a.result.finalState.position.x == doctest::Approx(b.result.finalState.position.x));
    CHECK(a.result.method == "semi_implicit_euler");
}

TEST_CASE("engine one-shot run without artifacts is deterministic") {
    SimulationEngine engine;
    const auto first = engine.execute(linearRequest("simulate_linear_motion", 2.0));
    const auto second = engine.execute(linearRequest("simulate_linear_motion", 2.0));
    REQUIRE(first.success);
    REQUIRE(second.success);
    CHECK(first.result["result"]["step_count"] == second.result["result"]["step_count"]);
    CHECK(first.result["result"]["method"] == "closed_form");
    CHECK(first.pendingArtifacts.empty());
    CHECK(second.pendingArtifacts.empty());
    REQUIRE(first.result["result"].contains("final_state"));
    CHECK(first.result["result"]["final_state"]["position"]["x"] ==
          doctest::Approx(second.result["result"]["final_state"]["position"]["x"].get<double>()));
}

TEST_CASE("kinematics and dynamics integrations stay distinct methods") {
    const auto kin = integrate(makeLinear(1.0));
    const auto dyn = integrate(makeDynamics(1.0));
    REQUIRE(kin.success);
    REQUIRE(dyn.success);
    CHECK(kin.result.method == "closed_form");
    CHECK(dyn.result.method == "semi_implicit_euler");
}
