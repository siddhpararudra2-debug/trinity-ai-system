#include "trinity/simulation/Validators.hpp"

#include <cmath>

namespace trinity::simulation {
namespace {

bool finiteVec(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool finiteState(const SimulationState& s) {
    return std::isfinite(s.t) && finiteVec(s.position) && finiteVec(s.velocity) &&
           finiteVec(s.acceleration);
}

double expectedLinearX(const SimulationProject& project, double t) {
    const double x0 = project.initial.position.x;
    const double vx0 = project.initial.velocity.x;
    const double ax = project.initial.acceleration.x;
    return x0 + vx0 * t + 0.5 * ax * t * t;
}

}  // namespace

long long validateStepCount(double dt, double durationS) {
    if (!std::isfinite(dt) || !std::isfinite(durationS) || dt <= 0.0 || durationS < 0.0) {
        return -1;
    }
    const double raw = durationS / dt;
    if (!std::isfinite(raw) || raw < 0.0) {
        return -1;
    }
    const long long steps = static_cast<long long>(std::llround(raw));
    if (steps < 0 || steps > kMaxSteps) {
        return -1;
    }
    return steps;
}

ProjectValidation validateProject(const SimulationProject& project) {
    if (project.type.empty() || project.model.empty()) {
        return {false, "Simulation type and model are required"};
    }
    if (!std::isfinite(project.dt) || project.dt <= 0.0) {
        return {false, "dt must be a positive finite number"};
    }
    if (!std::isfinite(project.durationS) || project.durationS <= 0.0) {
        return {false, "duration_s must be a positive finite number"};
    }
    if (project.durationS > kMaxDurationS) {
        return {false, "duration_s exceeds the supported maximum of 3600 seconds"};
    }
    if (!finiteState(project.initial)) {
        return {false, "Initial state must be finite"};
    }
    if (!finiteVec(project.forceN)) {
        return {false, "force_N must be finite"};
    }
    if (!std::isfinite(project.massKg)) {
        return {false, "mass_kg must be finite"};
    }
    if (!std::isfinite(project.gravity)) {
        return {false, "gravity must be finite"};
    }
    if (project.type == "basic_dynamics") {
        if (project.massKg <= 0.0) {
            return {false, "mass_kg must be positive for basic_dynamics"};
        }
        if (project.model != "force_mass") {
            return {false, "basic_dynamics only supports model 'force_mass'"};
        }
    } else if (project.type == "kinematics") {
        if (project.model != "linear_motion" && project.model != "projectile" &&
            project.model != "constant_acceleration") {
            return {false, "kinematics supports linear_motion, projectile, constant_acceleration"};
        }
    } else {
        return {false, "Unsupported simulation type '" + project.type +
                           "'; supported: kinematics, basic_dynamics"};
    }
    const long long steps = validateStepCount(project.dt, project.durationS);
    if (steps < 0) {
        return {false, "Step count exceeds the supported maximum (1000000)"};
    }
    return {true, ""};
}

ResultChecks validateResult(const SimulationProject& project, const SimulationResult& result) {
    ResultChecks out;
    if (result.samples.empty()) {
        out.message = "Result has no samples";
        return out;
    }
    out.finite = true;
    out.timeMonotonic = true;
    for (size_t i = 0; i < result.samples.size(); ++i) {
        if (!finiteState(result.samples[i])) {
            out.finite = false;
            break;
        }
        if (i > 0 && result.samples[i].t <= result.samples[i - 1].t) {
            out.timeMonotonic = false;
            break;
        }
    }
    if (!out.finite) {
        out.message = "Result contains non-finite samples";
        return out;
    }
    if (!out.timeMonotonic) {
        out.message = "Result time base is not strictly increasing";
        return out;
    }

    if (project.type == "kinematics" && !result.downsampled && result.samples.size() >= 2) {
        out.closedFormAgreement = true;
        double maxErr = 0.0;
        for (const auto& sample : result.samples) {
            const double expected = expectedLinearX(project, sample.t);
            const double err = std::fabs(sample.position.x - expected);
            if (err > maxErr) {
                maxErr = err;
            }
            if (err > 1e-6 * (1.0 + std::fabs(expected))) {
                out.closedFormAgreement = false;
                break;
            }
        }
        out.details["max_position_error_m"] = maxErr;
        if (!out.closedFormAgreement) {
            out.message = "Closed-form position agreement failed";
            return out;
        }
    } else if (project.type == "basic_dynamics" && !result.downsampled &&
               result.samples.size() >= 2 && project.massKg > 0.0) {
        const double ax = project.forceN.x / project.massKg;
        const double expectedVx =
            project.initial.velocity.x + ax * result.finalState.t;
        const double err = std::fabs(result.finalState.velocity.x - expectedVx);
        out.details["velocity_x_error_m_s"] = err;
        out.closedFormAgreement = err <= 1e-6 * (1.0 + std::fabs(expectedVx));
        if (!out.closedFormAgreement) {
            out.message = "Semi-implicit Euler velocity disagrees with closed form";
            return out;
        }
    } else {
        out.closedFormAgreement = true;
    }

    out.ok = true;
    out.message = "Result samples are finite, time-monotonic, and within tolerance";
    return out;
}

}  // namespace trinity::simulation
