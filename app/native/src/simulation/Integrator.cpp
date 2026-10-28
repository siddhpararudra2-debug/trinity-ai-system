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

// --- multi-body spring chain helpers -----------------------------------

std::vector<double> chainMasses(const SimulationProject& project) {
    std::vector<double> masses;
    masses.reserve(project.objects.size());
    for (const auto& object : project.objects) {
        masses.push_back(object.massKg);
    }
    return masses;
}

struct ChainState {
    std::vector<Vec3> p;  // positions
    std::vector<Vec3> v;  // velocities

    size_t size() const { return p.size(); }
};

/// Hooke forces for the spring chain: adjacent objects share the
/// project-level k and L0; the external project force acts on body 0.
std::vector<Vec3> chainForces(const SimulationProject& project,
                              const ChainState& ys) {
    const size_t n = ys.size();
    std::vector<Vec3> f(n, Vec3{0.0, 0.0, 0.0});
    for (size_t i = 1; i < n; ++i) {
        const double dx = ys.p[i].x - ys.p[i - 1].x;
        const double dy = ys.p[i].y - ys.p[i - 1].y;
        const double dz = ys.p[i].z - ys.p[i - 1].z;
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist <= 1e-12) {
            continue;
        }
        const double fMag = project.springKNm * (dist - project.restLengthM);
        const double ux = dx / dist, uy = dy / dist, uz = dz / dist;
        f[i - 1].x += ux * fMag;
        f[i - 1].y += uy * fMag;
        f[i - 1].z += uz * fMag;
        f[i].x -= ux * fMag;
        f[i].y -= uy * fMag;
        f[i].z -= uz * fMag;
    }
    if (n > 0) {
        f[0].x += project.forceN.x;
        f[0].y += project.forceN.y;
        f[0].z += project.forceN.z;
    }
    return f;
}

IntegrateOutcome integrateSpringChain(const SimulationProject& project,
                                      const CancelProbe& cancel) {
    const auto masses = chainMasses(project);
    if (masses.empty()) {
        return errorOutcome(project, "Spring chain needs at least two objects");
    }
    for (size_t i = 0; i < masses.size(); ++i) {
        if (masses[i] <= 0.0 || !std::isfinite(masses[i])) {
            return errorOutcome(project, "Object masses must be positive "
                                         "finite numbers");
        }
        const Vec3& v0 = project.objects[i].initial.velocity;
        if (!std::isfinite(v0.x) || !std::isfinite(v0.y) ||
            !std::isfinite(v0.z)) {
            return errorOutcome(project, "Object initial states must be finite");
        }
    }
    const long long steps = validateStepCount(project.dt, project.durationS);
    if (steps < 0) {
        return errorOutcome(project, "Step count exceeds the supported maximum");
    }

    const bool useRk4 = project.method == "rk4";
    SimulationResult result =
        baseResult(project, useRk4 ? "rk4" : "semi_implicit_euler");

    ChainState ys;
    ys.p.resize(masses.size());
    ys.v.resize(masses.size());
    for (size_t i = 0; i < masses.size(); ++i) {
        ys.p[i] = project.objects[i].initial.position;
        ys.v[i] = project.objects[i].initial.velocity;
    }
    auto stepEuler = [&](double h) {
        const auto f = chainForces(project, ys);
        for (size_t i = 0; i < masses.size(); ++i) {
            ys.v[i].x += (f[i].x / masses[i]) * h;
            ys.v[i].y += (f[i].y / masses[i]) * h;
            ys.v[i].z += (f[i].z / masses[i]) * h;
            ys.p[i].x += ys.v[i].x * h;
            ys.p[i].y += ys.v[i].y * h;
            ys.p[i].z += ys.v[i].z * h;
        }
    };
    // RK4 on the coupled first-order ODE system Y' = G(Y).
    const auto derivative = [&](const ChainState& y, ChainState& out) {
        const auto f = chainForces(project, y);
        out.p.resize(y.size());
        out.v.resize(y.size());
        for (size_t i = 0; i < y.size(); ++i) {
            out.p[i] = y.v[i];
            out.v[i].x = f[i].x / masses[i];
            out.v[i].y = f[i].y / masses[i];
            out.v[i].z = f[i].z / masses[i];
        }
    };

    result.samples.reserve(static_cast<size_t>(steps) + 1);
    // Samples track body 0 (the anchored side of the chain); the
    // summary carries every body's final state.
    auto pushBody0 = [&](long long step) {
        SimulationState state = project.objects.front().initial;
        state.t = static_cast<double>(step) * project.dt;
        state.position = ys.p[0];
        state.velocity = ys.v[0];
        const auto f = chainForces(project, ys);
        state.acceleration = {f[0].x / masses[0], f[0].y / masses[0],
                              f[0].z / masses[0]};
        result.samples.push_back(state);
    };
    pushBody0(0);
    for (long long i = 1; i <= steps; ++i) {
        if (cancel && (i % kCancelCheckInterval) == 0 && cancel()) {
            return cancelledOutcome(project);
        }
        if (useRk4) {
            const double h = project.dt;
            ChainState k1, k2, k3, k4;
            derivative(ys, k1);
            auto evaluate = [&](const ChainState& y, const ChainState& k,
                                double half) {
                ChainState ev;
                ev.p.resize(y.size());
                ev.v.resize(y.size());
                for (size_t b = 0; b < y.size(); ++b) {
                    ev.p[b].x = y.p[b].x + k.p[b].x * half;
                    ev.p[b].y = y.p[b].y + k.p[b].y * half;
                    ev.p[b].z = y.p[b].z + k.p[b].z * half;
                    ev.v[b].x = y.v[b].x + k.v[b].x * half;
                    ev.v[b].y = y.v[b].y + k.v[b].y * half;
                    ev.v[b].z = y.v[b].z + k.v[b].z * half;
                }
                return ev;
            };
            const ChainState y2 = evaluate(ys, k1, h / 2.0);
            derivative(y2, k2);
            const ChainState y3 = evaluate(ys, k2, h / 2.0);
            derivative(y3, k3);
            const ChainState y4 = evaluate(ys, k3, h);
            derivative(y4, k4);
            for (size_t b = 0; b < ys.size(); ++b) {
                ys.p[b].x += h / 6.0 *
                             (k1.p[b].x + 2.0 * k2.p[b].x + 2.0 * k3.p[b].x +
                              k4.p[b].x);
                ys.p[b].y += h / 6.0 *
                             (k1.p[b].y + 2.0 * k2.p[b].y + 2.0 * k3.p[b].y +
                              k4.p[b].y);
                ys.p[b].z += h / 6.0 *
                             (k1.p[b].z + 2.0 * k2.p[b].z + 2.0 * k3.p[b].z +
                              k4.p[b].z);
                ys.v[b].x += h / 6.0 *
                             (k1.v[b].x + 2.0 * k2.v[b].x + 2.0 * k3.v[b].x +
                              k4.v[b].x);
                ys.v[b].y += h / 6.0 *
                             (k1.v[b].y + 2.0 * k2.v[b].y + 2.0 * k3.v[b].y +
                              k4.v[b].y);
                ys.v[b].z += h / 6.0 *
                             (k1.v[b].z + 2.0 * k2.v[b].z + 2.0 * k3.v[b].z +
                              k4.v[b].z);
            }
        } else {
            stepEuler(project.dt);
        }
        pushBody0(i);
    }
    result.stepCount = steps;
    result.sampleCount = static_cast<long long>(result.samples.size());
    result.finalState = result.samples.back();
    if (result.samples.size() > kMaxInMemorySamples) {
        downsample(result.samples, kMaxInMemorySamples);
        result.downsampled = true;
    }
    core::Json finalStates = core::Json::array();
    for (size_t i = 0; i < ys.size(); ++i) {
        core::Json pj = core::Json::object();
        pj["t"] = result.durationS;
        pj["position"] = ys.p[i].toJson();
        pj["velocity"] = ys.v[i].toJson();
        pj["acceleration"] = Vec3{0.0, 0.0, 0.0}.toJson();
        finalStates.push_back(pj);
    }
    double kinetic = 0.0;
    for (size_t i = 0; i < ys.size(); ++i) {
        kinetic += 0.5 * masses[i] *
                   (ys.v[i].x * ys.v[i].x + ys.v[i].y * ys.v[i].y +
                    ys.v[i].z * ys.v[i].z);
    }
    double potential = 0.0;
    for (size_t i = 1; i < ys.size(); ++i) {
        const double dx = ys.p[i].x - ys.p[i - 1].x;
        const double dy = ys.p[i].y - ys.p[i - 1].y;
        const double dz = ys.p[i].z - ys.p[i - 1].z;
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        potential += 0.5 * project.springKNm *
                     (dist - project.restLengthM) * (dist - project.restLengthM);
    }
    result.summary = core::Json{
        {"bodies", static_cast<int>(masses.size())},
        {"method", result.method},
        {"spring_k_n_m", project.springKNm},
        {"rest_length_m", project.restLengthM},
        {"final_states", finalStates},
        {"kinetic_energy_J", kinetic},
        {"spring_potential_energy_J", potential},
        {"total_energy_J", kinetic + potential}};
    return {true, false, "", std::move(result)};
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
    const bool useRk4 = project.method == "rk4";
    SimulationResult result = baseResult(project, useRk4 ? "rk4" : "semi_implicit_euler");
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
    if (useRk4) {
        // Constant acceleration: RK4 on the coupled (p', p=v; v'=a)
        // system. p4 = p + h v + h²/2 a (exact for quadratic), so RK4
        // integrates a constant-force trajectory to machine precision.
        for (long long i = 1; i <= steps; ++i) {
            if (cancel && (i % kCancelCheckInterval) == 0 && cancel()) {
                return cancelledOutcome(project);
            }
            const double h = project.dt;
            const double h2 = h * h * 0.5;
            state.position.x += state.velocity.x * h + ax * h2;
            state.position.y += state.velocity.y * h + ay * h2;
            state.position.z += state.velocity.z * h + az * h2;
            state.velocity.x += ax * h;
            state.velocity.y += ay * h;
            state.velocity.z += az * h;
            state.t = static_cast<double>(i) * project.dt;
            state.acceleration = {ax, ay, az};
            result.samples.push_back(state);
        }
    } else {
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
        {"method", result.method}};
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
    if (project.type == "multi_body") {
        if (project.model == "spring_chain") {
            return integrateSpringChain(project, cancel);
        }
        return errorOutcome(project, "Unsupported multi_body model '" + project.model + "'");
    }
    return errorOutcome(project, "Unsupported simulation type '" + project.type + "'");
}

}  // namespace trinity::simulation
