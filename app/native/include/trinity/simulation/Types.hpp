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

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    core::Json toJson() const;
    static Vec3 fromJson(const core::Json& json);
};

struct SimulationState {
    double t = 0.0;
    Vec3 position;
    Vec3 velocity;
    Vec3 acceleration;

    core::Json toJson() const;
    static SimulationState fromJson(const core::Json& json);
};

struct SimulationProject {
    std::string projectId;
    std::string name;
    std::string type;   // "kinematics" | "basic_dynamics"
    std::string model;  // "linear_motion" | "projectile" | "constant_acceleration" | "force_mass"
    double dt = 0.01;
    double durationS = 1.0;
    SimulationState initial;
    double massKg = 1.0;
    Vec3 forceN;
    double gravity = 9.80665;
    core::Json originalUnits = core::Json::object();

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
