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

double kineticEnergyJ(const std::vector<double>& masses,
                      const std::vector<SimulationState>& states) {
    double ke = 0.0;
    for (size_t i = 0; i < masses.size(); ++i) {
        const Vec3& v = states[i].velocity;
        ke += 0.5 * masses[i] * (v.x * v.x + v.y * v.y + v.z * v.z);
    }
    return ke;
}

double springPotentialEnergyJ(const SimulationProject& project,
                              const std::vector<SimulationState>& states) {
    double pe = 0.0;
    for (size_t i = 1; i < states.size(); ++i) {
        const double dx = states[i].position.x - states[i - 1].position.x;
        const double dy = states[i].position.y - states[i - 1].position.y;
        const double dz = states[i].position.z - states[i - 1].position.z;
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        pe += 0.5 * project.springKNm * (dist - project.restLengthM) *
              (dist - project.restLengthM);
    }
    return pe;
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
    ProjectValidation out;
    core::Json rules = core::Json::array();
    std::string firstError;
    auto rule = [&](const std::string& name, bool passed, const std::string& message) {
        rules.push_back(core::Json{{"rule", name}, {"passed", passed}, {"message", message}});
        if (!passed && firstError.empty()) {
            firstError = message;
        }
    };

    rule("project.type_model_required",
         !project.type.empty() && !project.model.empty(),
         "Simulation type and model are required");
    rule("project.dt_positive", std::isfinite(project.dt) && project.dt > 0.0,
         "dt must be a positive finite number");
    rule("project.duration_positive",
         std::isfinite(project.durationS) && project.durationS > 0.0,
         "duration_s must be a positive finite number");
    rule("project.duration_max", project.durationS <= kMaxDurationS,
         "duration_s exceeds the supported maximum of 3600 seconds");
    rule("project.time_step_within_duration", project.dt <= project.durationS,
         "dt must not exceed duration_s");
    rule("project.initial_finite", finiteState(project.initial),
         "Initial state must be finite");
    rule("project.force_finite", finiteVec(project.forceN), "force_N must be finite");
    rule("project.mass_finite", std::isfinite(project.massKg), "mass_kg must be finite");
    rule("project.gravity_finite", std::isfinite(project.gravity), "gravity must be finite");

    rule("project.method_supported",
         project.method == "semi_implicit_euler" || project.method == "rk4",
         "method must be 'semi_implicit_euler' or 'rk4'");

    if (project.type == "multi_body") {
        rule("project.multi_body_model", project.model == "spring_chain",
             "multi_body only supports model 'spring_chain'");
        rule("project.multi_body_count", project.objects.size() >= 2,
             "multi_body needs at least two simulation objects");
        bool massesOk = true;
        bool initialsOk = true;
        for (const auto& object : project.objects) {
            if (object.massKg <= 0.0 || !std::isfinite(object.massKg)) {
                massesOk = false;
            }
            if (!finiteState(object.initial)) {
                initialsOk = false;
            }
        }
        rule("project.multi_body_masses_positive", massesOk,
             "Every object needs a positive finite mass_kg");
        rule("project.multi_body_initials_finite", initialsOk,
             "Every object initial state must be finite");
        rule("project.spring_stiffness_positive",
             std::isfinite(project.springKNm) && project.springKNm > 0.0,
             "spring_k_n_m must be positive finite");
        rule("project.rest_length_positive",
             std::isfinite(project.restLengthM) && project.restLengthM > 0.0,
             "rest_length_m must be positive finite");
    } else if (project.type == "basic_dynamics") {
        rule("project.mass_positive", project.massKg > 0.0,
             "mass_kg must be positive for basic_dynamics");
        rule("project.force_mass_model", project.model == "force_mass",
             "basic_dynamics only supports model 'force_mass'");
    } else if (project.type == "kinematics") {
        rule("project.kinematics_model",
             project.model == "linear_motion" || project.model == "projectile" ||
                 project.model == "constant_acceleration",
             "kinematics supports linear_motion, projectile, constant_acceleration");
    } else {
        rule("project.supported_type", false,
             "Unsupported simulation type '" + project.type +
                 "'; supported: kinematics, basic_dynamics, multi_body");
    }

    if (project.objects.size() > 1 && project.type != "multi_body") {
        rule("project.single_object", false,
             "Single-body models mirror exactly objects[0]");
    }

    rule("project.step_count", validateStepCount(project.dt, project.durationS) >= 0,
         "Step count exceeds the supported maximum (1000000)");

    out.ok = firstError.empty();
    out.error = firstError;
    out.rules = std::move(rules);
    return out;
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
    } else if (project.type == "multi_body" && !result.downsampled &&
               result.samples.size() >= 2) {
        // Energy conservation for the undamped spring chain: RK4 must
        // close E_final ≈ E_initial (1% relative tolerance).
        std::vector<double> masses;
        std::vector<SimulationState> initialStates;
        for (const auto& object : project.objects) {
            masses.push_back(object.massKg);
            initialStates.push_back(object.initial);
        }
        if (masses.empty()) {
            masses.push_back(project.massKg);
            initialStates.push_back(project.initial);
        }
        std::vector<SimulationState> finalStates;
        bool parsedFinal = result.summary.contains("final_states") &&
                           result.summary["final_states"].is_array();
        if (parsedFinal) {
            for (const auto& item : result.summary["final_states"]) {
                finalStates.push_back(SimulationState::fromJson(item));
            }
        }
        const double e0 =
            kineticEnergyJ(masses, initialStates) +
            springPotentialEnergyJ(project, initialStates);
        double e1 = e0;
        if (parsedFinal && finalStates.size() == masses.size()) {
            e1 = kineticEnergyJ(masses, finalStates) +
                 springPotentialEnergyJ(project, finalStates);
        } else {
            out.closedFormAgreement = false;
            out.message = "Result summary lacks spring-chain final states";
            return out;
        }
        out.details["initial_energy_J"] = e0;
        out.details["final_energy_J"] = e1;
        const double scale = std::max(std::fabs(e0), 1.0);
        out.closedFormAgreement = std::fabs(e1 - e0) <= 0.01 * scale;
        if (!out.closedFormAgreement) {
            out.message = "Spring-chain energy conservation failed";
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
