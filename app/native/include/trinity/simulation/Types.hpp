#pragma once

// Simulation IR: SI units internally (kg, N, s, m, m/s, m/s^2).
// Supported types: kinematics (linear_motion, projectile,
// constant_acceleration) and basic_dynamics (F = m*a). Units outside
// SI are normalized at parse time; original quantities are preserved
// on the project for traceability.

#include <functional>
#include <string>
#include <vector>

#include "../core/Json.hpp"

namespace trinity::simulation {

/// Simulation clock in seconds (SI). Kept as a distinct name so time
/// quantities are never confused with lengths or counts in the IR.
using SimulationTime = double;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    core::Json toJson() const;
    static Vec2 fromJson(const core::Json& json);
};

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    core::Json toJson() const;
    static Vec3 fromJson(const core::Json& json);
};

/// Orientation in degrees (roll/pitch/yaw). Reserved for oriented
/// objects; V1 integrators are point-mass and leave it at zero.
struct Orientation {
    double rollDeg = 0.0;
    double pitchDeg = 0.0;
    double yawDeg = 0.0;

    core::Json toJson() const;
    static Orientation fromJson(const core::Json& json);
};

struct SimulationState {
    SimulationTime t = 0.0;
    Vec3 position;
    Vec3 velocity;
    Vec3 acceleration;

    core::Json toJson() const;
    static SimulationState fromJson(const core::Json& json);
};

/// One simulated body. V1 supports exactly one object per project;
/// the container exists so multi-body models can be added later
/// without changing the IR shape.
struct SimulationObject {
    std::string objectId;
    std::string name;
    double massKg = 1.0;
    SimulationState initial;
    Orientation orientation;

    core::Json toJson() const;
    static SimulationObject fromJson(const core::Json& json);
};

/// A single normalized input value recorded for traceability
/// (e.g. {key: "force_N", value: 10, unit: "N"}).
struct SimulationParameter {
    std::string key;
    double value = 0.0;
    std::string unit;

    core::Json toJson() const;
    static SimulationParameter fromJson(const core::Json& json);
};

/// Structured inputs handed to the simulation (typed JSON, never
/// formatted display strings).
using SimulationInput = core::Json;

/// A declared output produced by a run (e.g. {"position", "m"}).
struct SimulationOutput {
    std::string key;
    std::string unit;
    core::Json value = core::Json::object();

    core::Json toJson() const;
    static SimulationOutput fromJson(const core::Json& json);
};

struct SimulationProject {
    std::string projectId;
    std::string name;
    std::string type;   // "kinematics" | "basic_dynamics"
    std::string model;  // "linear_motion" | "projectile" | "constant_acceleration" | "force_mass"
    SimulationTime dt = 0.01;
    SimulationTime durationS = 1.0;
    SimulationState initial;
    double massKg = 1.0;
    Vec3 forceN;
    double gravity = 9.80665;
    core::Json originalUnits = core::Json::object();

    // IR containers: the single simulated body is mirrored into
    // objects[0] (authoritative on input) so the shorthand fields
    // above stay valid for existing integrators and tests.
    std::vector<SimulationObject> objects;
    std::vector<SimulationParameter> parameters;
    SimulationInput inputs = core::Json::object();
    core::Json boundaryConditions = core::Json::object();
    std::vector<SimulationOutput> outputs;
    core::Json metadata = core::Json::object();

    /// Adopt objects[0] into the shorthand fields when they carry
    /// explicit values and the shorthand is untouched (called by
    /// fromJson so externally built IR round-trips identically).
    void syncPrimaryObject();
    /// Compute the authoritative primary object without mutating this
    /// project (shorthand wins unless it is still at defaults while
    /// objects[0] carries explicit values).
    SimulationObject resolvePrimary() const;

    core::Json toJson() const;
    static SimulationProject fromJson(const core::Json& json);
};

struct SimulationResult {
    std::string projectId;
    std::string type;
    std::string model;
    std::string method;  // "closed_form" | "semi_implicit_euler"
    double dt = 0.0;
    double durationS = 0.0;
    long long stepCount = 0;
    long long sampleCount = 0;
    bool downsampled = false;
    std::vector<SimulationState> samples;
    SimulationState finalState;
    core::Json summary = core::Json::object();
    core::Json originalUnits = core::Json::object();

    core::Json toJson() const;
    static SimulationResult fromJson(const core::Json& json);
};

using CancelProbe = std::function<bool()>;

constexpr double kDefaultDt = 0.01;
constexpr double kDefaultDurationS = 1.0;
constexpr double kMaxDurationS = 3600.0;
constexpr long long kMaxSteps = 1000000;
constexpr size_t kMaxInMemorySamples = 20000;
constexpr int kCancelCheckInterval = 256;

}  // namespace trinity::simulation
