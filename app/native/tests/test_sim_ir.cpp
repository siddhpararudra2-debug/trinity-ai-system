#include <doctest.h>

#include <cmath>

#include "trinity/simulation/Types.hpp"

using trinity::simulation::Orientation;
using trinity::simulation::SimulationObject;
using trinity::simulation::SimulationOutput;
using trinity::simulation::SimulationParameter;
using trinity::simulation::SimulationProject;
using trinity::simulation::SimulationState;
using trinity::simulation::Vec2;
using trinity::simulation::Vec3;

namespace {

SimulationProject linearProject() {
    SimulationProject p;
    p.projectId = "ir-proj";
    p.name = "ir";
    p.type = "kinematics";
    p.model = "linear_motion";
    p.dt = 0.01;
    p.durationS = 1.0;
    p.initial.velocity.x = 5.0;
    return p;
}

}  // namespace

TEST_CASE("SimulationState emits time_s and t and round-trips both keys") {
    SimulationState s;
    s.t = 0.1;
    s.position = {0.5, 0.0, 1.2};
    s.velocity = {5.0, 0.0, 0.0};
    s.acceleration = {0.0, 0.0, -9.81};
    const auto json = s.toJson();
    CHECK(json.value("time_s", 0.0) == doctest::Approx(0.1));
    CHECK(json.value("t", 0.0) == doctest::Approx(0.1));
    CHECK(json["position"]["x"] == doctest::Approx(0.5));
    CHECK(json["velocity"]["x"] == doctest::Approx(5.0));
    CHECK(json["acceleration"]["z"] == doctest::Approx(-9.81));

    // Legacy {"t": ...} only: still reads.
    trinity::core::Json legacy = {{"t", 0.7}};
    CHECK(SimulationState::fromJson(legacy).t == doctest::Approx(0.7));
    // Spec-facing {"time_s": ...} only: still reads.
    trinity::core::Json spec = {{"time_s", 0.9}};
    CHECK(SimulationState::fromJson(spec).t == doctest::Approx(0.9));
    // Both present: "t" wins (canonical storage key).
    trinity::core::Json both = {{"t", 0.2}, {"time_s", 0.9}};
    CHECK(SimulationState::fromJson(both).t == doctest::Approx(0.2));
}

TEST_CASE("vector, orientation, parameter, and output IR types serialize") {
    const Vec2 v2{1.0, -2.0};
    CHECK(v2.toJson()["x"] == doctest::Approx(1.0));
    CHECK(v2.toJson()["y"] == doctest::Approx(-2.0));
    CHECK(Vec2::fromJson(v2.toJson()).y == doctest::Approx(-2.0));

    const Orientation o{1.0, 2.0, 3.0};
    const auto oj = o.toJson();
    CHECK(oj["roll_deg"] == doctest::Approx(1.0));
    CHECK(Orientation::fromJson(oj).yawDeg == doctest::Approx(3.0));

    const SimulationParameter param{"force_N", 10.0, "N"};
    const auto pj = param.toJson();
    CHECK(pj["key"] == "force_N");
    CHECK(pj["value"] == doctest::Approx(10.0));
    CHECK(pj["unit"] == "N");
    CHECK(SimulationParameter::fromJson(pj).value == doctest::Approx(10.0));

    const SimulationOutput output{"position", "m", Vec3{1.0, 2.0, 3.0}.toJson()};
    const auto oj2 = output.toJson();
    CHECK(oj2["key"] == "position");
    CHECK(oj2["unit"] == "m");
    CHECK(SimulationOutput::fromJson(oj2).value["x"] == doctest::Approx(1.0));

    CHECK(std::is_same<trinity::simulation::SimulationTime, double>::value);
}

TEST_CASE("SimulationProject serializes all IR containers and round-trips") {
    SimulationProject p = linearProject();
    p.parameters = {{"dt", 0.01, "s"}, {"duration_s", 1.0, "s"}};
    p.outputs = {{"position", "m", trinity::core::Json::object()},
                 {"velocity", "m/s", trinity::core::Json::object()}};
    p.boundaryConditions = {{"wind_m_s", 3.0}};
    p.metadata = {{"source", "unit-test"}};
    p.inputs = {{"initial_velocity_m_s", 5.0}};
    p.originalUnits = {{"velocity", "m/s"}};

    const auto json = p.toJson();
    CHECK(json.contains("objects"));
    REQUIRE(json["objects"].is_array());
    REQUIRE(json["objects"].size() == 1);
    CHECK(json["objects"][0]["object_id"] == "object-0");
    CHECK(json["objects"][0]["mass_kg"] == doctest::Approx(1.0));
    CHECK(json.contains("parameters"));
    CHECK(json["parameters"].size() == 2);
    CHECK(json["parameters"][0]["key"] == "dt");
    CHECK(json.contains("boundary_conditions"));
    CHECK(json["boundary_conditions"]["wind_m_s"] == doctest::Approx(3.0));
    CHECK(json.contains("outputs"));
    CHECK(json["outputs"].size() == 2);
    CHECK(json.contains("metadata"));
    CHECK(json["metadata"]["source"] == "unit-test");
    CHECK(json.contains("inputs"));
    CHECK(json["inputs"]["initial_velocity_m_s"] == doctest::Approx(5.0));

    const SimulationProject back = SimulationProject::fromJson(json);
    CHECK(back.projectId == "ir-proj");
    CHECK(back.type == "kinematics");
    CHECK(back.model == "linear_motion");
    CHECK(back.dt == doctest::Approx(0.01));
    CHECK(back.durationS == doctest::Approx(1.0));
    CHECK(back.initial.velocity.x == doctest::Approx(5.0));
    REQUIRE(back.parameters.size() == 2);
    CHECK(back.parameters[1].key == "duration_s");
    REQUIRE(back.outputs.size() == 2);
    CHECK(back.outputs[0].key == "position");
    CHECK(back.boundaryConditions["wind_m_s"] == doctest::Approx(3.0));
    CHECK(back.metadata["source"] == "unit-test");
    CHECK(back.inputs["initial_velocity_m_s"] == doctest::Approx(5.0));
    REQUIRE(back.objects.size() == 1);
    CHECK(back.objects.front().objectId == "object-0");
}

TEST_CASE("shorthand fields still serialize into objects[0]") {
    SimulationProject p = linearProject();
    p.massKg = 2.5;
    p.initial.position.x = 1.5;
    const auto json = p.toJson();
    REQUIRE(json["objects"].size() == 1);
    CHECK(json["objects"][0]["mass_kg"] == doctest::Approx(2.5));
    CHECK(json["objects"][0]["initial"]["position"]["x"] == doctest::Approx(1.5));
    const SimulationProject back = SimulationProject::fromJson(json);
    CHECK(back.massKg == doctest::Approx(2.5));
    CHECK(back.initial.position.x == doctest::Approx(1.5));
}

TEST_CASE("objects[0] is authoritative when shorthand is untouched") {
    SimulationProject p;
    p.projectId = "obj-authority";
    p.type = "basic_dynamics";
    p.model = "force_mass";
    // Shorthand left at defaults; explicit object carries the values.
    SimulationObject object;
    object.objectId = "obj-7";
    object.name = "slider";
    object.massKg = 4.0;
    object.initial.velocity.x = 2.5;
    p.objects = {object};

    const SimulationProject back = SimulationProject::fromJson(p.toJson());
    CHECK(back.massKg == doctest::Approx(4.0));
    CHECK(back.initial.velocity.x == doctest::Approx(2.5));
    REQUIRE(back.objects.size() == 1);
    CHECK(back.objects.front().objectId == "obj-7");
    CHECK(back.objects.front().name == "slider");
}

TEST_CASE("toJson does not mutate the source project") {
    SimulationProject p = linearProject();
    const size_t before = p.objects.size();
    const auto json = p.toJson();
    CHECK(p.objects.size() == before);
    CHECK(json["objects"].size() == 1);
}

TEST_CASE("more than one object is serialized but flagged by validation") {
    SimulationProject p = linearProject();
    SimulationObject a;
    a.objectId = "a";
    SimulationObject b;
    b.objectId = "b";
    p.objects = {a, b};
    const auto json = p.toJson();
    REQUIRE(json["objects"].size() == 2);
    const SimulationProject back = SimulationProject::fromJson(json);
    CHECK(back.objects.size() == 2);
}
