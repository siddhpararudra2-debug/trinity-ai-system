#include "trinity/simulation/Integrator.hpp"

#include <cmath>

#include "trinity/core/Uuid.hpp"
#include "trinity/simulation/Validators.hpp"

namespace trinity::simulation {
namespace {

void downsample(std::vector<SimulationState>& samples, size_t maxSamples) {
    if (samples.size() <= maxSamples || maxSamples < 2) {
        return;
    }
    const size_t stride = (samples.size() + maxSamples - 2) / (maxSamples - 1);
    std::vector<SimulationState> kept;
    kept.reserve(maxSamples);
    for (size_t i = 0; i < samples.size(); i += stride) {
        kept.push_back(samples[i]);
    }
    if (kept.back().t < samples.back().t) {
        kept.push_back(samples.back());
    }
    samples.swap(kept);
}

SimulationResult baseResult(const SimulationProject& project, const std::string& method) {
    SimulationResult out;
    out.projectId = project.projectId;
    out.type = project.type;
    out.model = project.model;
    out.method = method;
    out.dt = project.dt;
    out.durationS = project.durationS;
    out.originalUnits = project.originalUnits;
    return out;
}

IntegrateOutcome cancelledOutcome(const SimulationProject& project) {
    IntegrateOutcome out;
    out.success = false;
    out.cancelled = true;
    out.error = "Simulation cancelled";
    out.result = baseResult(project, "cancelled");
    return out;
}

IntegrateOutcome errorOutcome(const SimulationProject& project, const std::string& message) {
    IntegrateOutcome out;
    out.success = false;
    out.error = message;
    out.result = baseResult(project, "failed");
    return out;
}

IntegrateOutcome integrateLinear(const SimulationProject& project,
                                 const CancelProbe& cancel) {
    const double x0 = project.initial.position.x;
    const double y0 = project.initial.position.y;
    const double z0 = project.initial.position.z;
    const double vx0 = project.initial.velocity.x;
    const double vy0 = project.initial.velocity.y;
    const double vz0 = project.initial.velocity.z;
    const double ax = project.initial.acceleration.x;
    const double ay = project.initial.acceleration.y;
    const double az = project.initial.acceleration.z;

    SimulationResult result = baseResult(project, "closed_form");
    const long long steps = validateStepCount(project.dt, project.durationS);
    if (steps < 0) {
        return errorOutcome(project, "Step count exceeds the supported maximum");
    }
    const long long capacity = steps + 1;
    result.samples.reserve(static_cast<size_t>(capacity));
    for (long long i = 0; i <= steps; ++i) {
        if (cancel && (i % kCancelCheckInterval) == 0 && cancel()) {
            return cancelledOutcome(project);
        }
        const double t = static_cast<double>(i) * project.dt;
        SimulationState state;
        state.t = t;
        state.position = {x0 + vx0 * t + 0.5 * ax * t * t, y0 + vy0 * t + 0.5 * ay * t * t,
                          z0 + vz0 * t + 0.5 * az * t * t};
        state.velocity = {vx0 + ax * t, vy0 + ay * t, vz0 + az * t};
        state.acceleration = {ax, ay, az};
        result.samples.push_back(state);
    }
    result.stepCount = steps;
    result.sampleCount = static_cast<long long>(result.samples.size());
    result.finalState = result.samples.back();
    if (result.samples.size() > kMaxInMemorySamples) {
        downsample(result.samples, kMaxInMemorySamples);
        result.downsampled = true;
    }
    result.summary = core::Json{{"final_position_m", result.finalState.position.toJson()},
                                {"final_velocity_m_s", result.finalState.velocity.toJson()},
                                {"method", "closed_form"}};
    return {true, false, "", std::move(result)};
}

IntegrateOutcome integrateProjectile(const SimulationProject& project,
                                     const CancelProbe& cancel) {
    SimulationProject copy = project;
    copy.initial.acceleration = {0.0, -copy.gravity, 0.0};
    IntegrateOutcome out = integrateLinear(copy, cancel);
    if (!out.success) {
        out.result.model = project.model;
        out.result.originalUnits = project.originalUnits;
        return out;
    }
    out.result.model = project.model;
    out.result.method = "closed_form";
    out.result.summary = core::Json{
        {"final_position_m", out.result.finalState.position.toJson()},
        {"final_velocity_m_s", out.result.finalState.velocity.toJson()},
        {"gravity_m_s2", project.gravity},
        {"method", "closed_form"}};
    return out;
}

IntegrateOutcome integrateDynamics(const SimulationProject& project,
                                   const CancelProbe& cancel) {
    if (project.massKg <= 0.0) {
        return errorOutcome(project, "mass_kg must be positive");
    }
    SimulationResult result = baseResult(project, "semi_implicit_euler");
    const long long steps = validateStepCount(project.dt, project.durationS);
    if (steps < 0) {
        return errorOutcome(project, "Step count exceeds the supported maximum");
    }
    const double ax = project.forceN.x / project.massKg;
    const double ay = project.forceN.y / project.massKg;
    const double az = project.forceN.z / project.massKg;

    SimulationState state = project.initial;
    state.acceleration = {ax, ay, az};
    result.samples.reserve(static_cast<size_t>(steps) + 1);
    result.samples.push_back(state);
    for (long long i = 1; i <= steps; ++i) {
        if (cancel && (i % kCancelCheckInterval) == 0 && cancel()) {
            return cancelledOutcome(project);
        }
        state.velocity.x += ax * project.dt;
        state.velocity.y += ay * project.dt;
        state.velocity.z += az * project.dt;
        state.position.x += state.velocity.x * project.dt;
        state.position.y += state.velocity.y * project.dt;
        state.position.z += state.velocity.z * project.dt;
        state.t = static_cast<double>(i) * project.dt;
        state.acceleration = {ax, ay, az};
        result.samples.push_back(state);
    }
    result.stepCount = steps;
    result.sampleCount = static_cast<long long>(result.samples.size());
    result.finalState = result.samples.back();
    if (result.samples.size() > kMaxInMemorySamples) {
        downsample(result.samples, kMaxInMemorySamples);
        result.downsampled = true;
    }
    const double expectedVx = project.initial.velocity.x + ax * project.durationS;
    result.summary = core::Json{
        {"final_position_m", result.finalState.position.toJson()},
        {"final_velocity_m_s", result.finalState.velocity.toJson()},
        {"expected_velocity_x_m_s", expectedVx},
        {"acceleration_m_s2", core::Json{{"x", ax}, {"y", ay}, {"z", az}}},
        {"method", "semi_implicit_euler"}};
    return {true, false, "", std::move(result)};
}

}  // namespace

IntegrateOutcome integrate(const SimulationProject& project, const CancelProbe& cancel) {
    const ProjectValidation validation = validateProject(project);
    if (!validation.ok) {
        return errorOutcome(project, validation.error);
    }
    if (project.type == "kinematics") {
        if (project.model == "projectile") {
            return integrateProjectile(project, cancel);
        }
        if (project.model == "linear_motion" || project.model == "constant_acceleration") {
            return integrateLinear(project, cancel);
        }
        return errorOutcome(project, "Unsupported kinematics model '" + project.model + "'");
    }
    if (project.type == "basic_dynamics") {
        if (project.model == "force_mass") {
            return integrateDynamics(project, cancel);
        }
        return errorOutcome(project, "Unsupported basic_dynamics model '" + project.model + "'");
    }
    return errorOutcome(project, "Unsupported simulation type '" + project.type + "'");
}

}  // namespace trinity::simulation
