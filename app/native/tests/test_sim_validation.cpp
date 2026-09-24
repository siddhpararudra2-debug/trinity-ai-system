#include <doctest.h>

#include <cmath>
#include <limits>

#include "trinity/simulation/Validators.hpp"

using trinity::simulation::ResultChecks;
using trinity::simulation::SimulationProject;
using trinity::simulation::SimulationResult;
using trinity::simulation::SimulationState;
using trinity::simulation::validateProject;
using trinity::simulation::validateResult;
using trinity::simulation::validateStepCount;

namespace {

SimulationProject baseProject() {
    SimulationProject p;
    p.projectId = "proj-val";
    p.name = "val";
    p.type = "kinematics";
    p.model = "linear_motion";
    p.dt = 0.1;
    p.durationS = 1.0;
    return p;
}

}  // namespace

TEST_CASE("validateStepCount accepts finite positive dt and caps at max") {
    CHECK(validateStepCount(0.01, 1.0) == 100);
    CHECK(validateStepCount(0.5, 2.0) == 4);
    CHECK(validateStepCount(0.0, 1.0) < 0);
    CHECK(validateStepCount(-0.1, 1.0) < 0);
    CHECK(validateStepCount(0.01, -1.0) < 0);
    CHECK(validateStepCount(std::numeric_limits<double>::quiet_NaN(), 1.0) < 0);
    CHECK(validateStepCount(0.01, 1e9) < 0);
}

TEST_CASE("validateProject rejects invalid types and unsupported models") {
    auto p = baseProject();
    CHECK(validateProject(p).ok);

    p.dt = 0.0;
    CHECK_FALSE(validateProject(p).ok);
    p = baseProject();
    p.durationS = 0.0;
    CHECK_FALSE(validateProject(p).ok);
    p = baseProject();
    p.durationS = 99999.0;
    CHECK_FALSE(validateProject(p).ok);
    p = baseProject();
    p.type = "cfd";
    CHECK_FALSE(validateProject(p).ok);
    p = baseProject();
    p.model = "fluid_flow";
    CHECK_FALSE(validateProject(p).ok);
    p = baseProject();
    p.type = "basic_dynamics";
    p.model = "force_mass";
    p.massKg = -1.0;
    CHECK_FALSE(validateProject(p).ok);
    p = baseProject();
    p.initial.velocity.x = std::numeric_limits<double>::infinity();
    CHECK_FALSE(validateProject(p).ok);
    p = baseProject();
    p.type = "basic_dynamics";
    p.model = "linear_motion";
    p.massKg = 1.0;
    CHECK_FALSE(validateProject(p).ok);
}

TEST_CASE("validateResult rejects empty, non-finite, and non-monotonic samples") {
    const auto project = baseProject();
    SimulationResult empty;
    const auto emptyChecks = validateResult(project, empty);
    CHECK_FALSE(emptyChecks.ok);
    CHECK(emptyChecks.message.find("no samples") != std::string::npos);

    SimulationResult nonFinite;
    nonFinite.samples.push_back(SimulationState{});
    nonFinite.samples[0].position.x = std::numeric_limits<double>::quiet_NaN();
    const auto nanChecks = validateResult(project, nonFinite);
    CHECK_FALSE(nanChecks.ok);
    CHECK_FALSE(nanChecks.finite);

    SimulationResult nonMono;
    SimulationState a;
    a.t = 0.0;
    SimulationState b;
    b.t = 0.0;
    nonMono.samples = {a, b};
    const auto monoChecks = validateResult(project, nonMono);
    CHECK_FALSE(monoChecks.ok);
    CHECK_FALSE(monoChecks.timeMonotonic);
}

TEST_CASE("validateResult verifies finite monotonic samples against closed form") {
    auto project = baseProject();
    project.initial.velocity.x = 2.0;
    SimulationResult result;
    result.downsampled = false;
    for (int i = 0; i <= 10; ++i) {
        SimulationState s;
        s.t = i * 0.1;
        s.position.x = 2.0 * s.t;
        s.velocity.x = 2.0;
        s.acceleration.x = 0.0;
        result.samples.push_back(s);
    }
    result.finalState = result.samples.back();
    const ResultChecks checks = validateResult(project, result);
    CHECK(checks.ok);
    CHECK(checks.finite);
    CHECK(checks.timeMonotonic);
    CHECK(checks.closedFormAgreement);
}

TEST_CASE("downsampled kinematics skips closed-form sample agreement") {
    auto project = baseProject();
    SimulationResult result;
    result.downsampled = true;
    for (int i = 0; i <= 10; ++i) {
        SimulationState s;
        s.t = i * 0.1;
        result.samples.push_back(s);
    }
    result.finalState = result.samples.back();
    const ResultChecks checks = validateResult(project, result);
    CHECK(checks.ok);
    CHECK(checks.finite);
    CHECK(checks.timeMonotonic);
}
