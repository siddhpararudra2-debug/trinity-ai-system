#include "trinity/engines/SimulationEngine.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "trinity/core/Logger.hpp"
#include "trinity/core/Time.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/simulation/Exports.hpp"
#include "trinity/simulation/Integrator.hpp"
#include "trinity/simulation/Validators.hpp"

namespace trinity::engines {
namespace fs = std::filesystem;

namespace {

std::string shortId() {
    std::string uuid = core::newUuid();
    uuid.erase(std::remove(uuid.begin(), uuid.end(), '-'), uuid.end());
    return uuid.substr(0, 8);
}

double optionalNumber(const core::Json& params, const std::string& key, double fallback) {
    if (!params.contains(key) || params[key].is_null()) {
        return fallback;
    }
    if (!params[key].is_number()) {
        throw core::RequestValidationError("Parameter '" + key + "' must be numeric",
                                           {{"param", key}}, "engines");
    }
    const double value = params[key].get<double>();
    if (!std::isfinite(value)) {
        throw core::RequestValidationError("Parameter '" + key + "' must be finite",
                                           {{"param", key}}, "engines");
    }
    return value;
}

double requiredNumber(const core::Json& params, const std::string& key,
                      const std::string& operation) {
    if (!params.contains(key) || !params[key].is_number()) {
        throw core::RequestValidationError(
            "Operation '" + operation + "' requires numeric '" + key + "'",
            {{"operation", operation}, {"param", key}}, "engines");
    }
    const double value = params[key].get<double>();
    if (!std::isfinite(value)) {
        throw core::RequestValidationError("Parameter '" + key + "' must be finite",
                                           {{"operation", operation}, {"param", key}},
                                           "engines");
    }
    return value;
}

double massKgFromParams(const core::Json& params) {
    if (params.contains("mass_kg") && params["mass_kg"].is_number()) {
        return params["mass_kg"].get<double>();
    }
    if (params.contains("mass_g") && params["mass_g"].is_number()) {
        return params["mass_g"].get<double>() / 1000.0;
    }
    if (params.contains("mass") && params["mass"].is_number()) {
        return params["mass"].get<double>();
    }
    return 1.0;
}

double mmToM(double mm) { return mm / 1000.0; }

std::vector<simulation::SimulationOutput> defaultOutputs() {
    return {{"position", "m", core::Json::object()},
            {"velocity", "m/s", core::Json::object()},
            {"acceleration", "m/s^2", core::Json::object()}};
}

/// Ledger of the exact normalized values the integrator will use.
/// Recorded after all defaults/aliases are resolved so runs stay
/// reproducible and auditable (no silent input changes).
std::vector<simulation::SimulationParameter> parameterLedger(
    const simulation::SimulationProject& project) {
    std::vector<simulation::SimulationParameter> ledger;
    ledger.push_back({"dt", project.dt, "s"});
    ledger.push_back({"duration_s", project.durationS, "s"});
    if (project.type == "basic_dynamics") {
        ledger.push_back({"mass_kg", project.massKg, "kg"});
        ledger.push_back({"force_N_x", project.forceN.x, "N"});
        ledger.push_back({"force_N_y", project.forceN.y, "N"});
        ledger.push_back({"force_N_z", project.forceN.z, "N"});
    } else {
        ledger.push_back({"initial_position_x_m", project.initial.position.x, "m"});
        ledger.push_back({"initial_velocity_x_m_s", project.initial.velocity.x, "m/s"});
        ledger.push_back({"initial_velocity_y_m_s", project.initial.velocity.y, "m/s"});
        ledger.push_back({"initial_velocity_z_m_s", project.initial.velocity.z, "m/s"});
        ledger.push_back({"acceleration_x_m_s2", project.initial.acceleration.x, "m/s^2"});
        ledger.push_back({"acceleration_y_m_s2", project.initial.acceleration.y, "m/s^2"});
        ledger.push_back({"acceleration_z_m_s2", project.initial.acceleration.z, "m/s^2"});
        if (project.model == "projectile") {
            ledger.push_back({"gravity_m_s2", project.gravity, "m/s^2"});
        }
    }
    return ledger;
}

core::Json inputsFromParams(const core::Json& params) {
    // Structured request inputs, minus the two keys that can carry a
    // full result/project payload (never duplicate large series here).
    core::Json inputs = core::Json::object();
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (it.key() == "project" || it.key() == "result") {
            continue;
        }
        inputs[it.key()] = it.value();
    }
    return inputs;
}

core::Json validationJson(const simulation::ResultChecks& checks) {
    return core::Json{{"finite", checks.finite},
                      {"time_monotonic", checks.timeMonotonic},
                      {"closed_form_agreement", checks.closedFormAgreement},
                      {"ok", checks.ok},
                      {"message", checks.message},
                      {"details", checks.details}};
}

}  // namespace

SimulationEngine::SimulationEngine() {
    name_ = "simulation";
    version_ = "0.1.0";
    capabilities_ = {"describe",
                     "create_simulation",
                     "run_simulation",
                     "validate_simulation",
                     "export_results",
                     "simulate_linear_motion",
                     "simulate_projectile",
                     "simulate_constant_acceleration",
                     "simulate_dynamics"};
}

simulation::SimulationProject SimulationEngine::projectFromParams(
    const core::Json& params, const std::string& model) const {
    simulation::SimulationProject project;
    project.projectId = params.value("project_id", "");
    if (project.projectId.empty()) {
        project.projectId = core::newUuid();
    }
    project.name = params.value("name", model);
    project.model = model;
    // Time step: "dt" is canonical; "dt_s" is the intent-pipeline alias.
    if (params.contains("dt")) {
        project.dt = optionalNumber(params, "dt", simulation::kDefaultDt);
    } else {
        project.dt = optionalNumber(params, "dt_s", simulation::kDefaultDt);
    }
    project.durationS = optionalNumber(params, "duration_s", simulation::kDefaultDurationS);
    project.originalUnits = params.value("original_units", core::Json::object());
    project.boundaryConditions = params.value("boundary_conditions", core::Json::object());
    project.metadata = params.value("metadata", core::Json::object());
    project.inputs = inputsFromParams(params);

    const core::Json initial = params.value("initial", core::Json::object());
    project.initial.t = 0.0;
    project.initial.position = {
        mmToM(optionalNumber(initial, "position_mm_x",
                             mmToM(optionalNumber(params, "position_mm", 0.0)))),
        mmToM(optionalNumber(initial, "position_mm_y", 0.0)),
        mmToM(optionalNumber(initial, "position_mm_z", 0.0))};
    if (model == "projectile") {
        const double speed = requiredNumber(params, "initial_velocity_m_s", "simulate_projectile");
        const double angleDeg = optionalNumber(params, "launch_angle_deg", 0.0);
        const double angleRad = angleDeg * 3.14159265358979323846 / 180.0;
        project.initial.velocity = {speed * std::cos(angleRad), speed * std::sin(angleRad), 0.0};
        project.initial.position.y = mmToM(optionalNumber(params, "initial_height_mm", 0.0));
        project.gravity = optionalNumber(params, "gravity_m_s2", 9.80665);
    } else if (params.contains("initial_velocity_m_s") &&
               params["initial_velocity_m_s"].is_number()) {
        const double v = params["initial_velocity_m_s"].get<double>();
        if (params.contains("initial_velocity_y_m_s") &&
            params["initial_velocity_y_m_s"].is_number()) {
            project.initial.velocity = {v, params["initial_velocity_y_m_s"].get<double>(),
                                        optionalNumber(params, "initial_velocity_z_m_s", 0.0)};
        } else {
            project.initial.velocity = {v, 0.0, 0.0};
        }
    } else if (initial.contains("velocity") && initial["velocity"].is_object()) {
        project.initial.velocity = simulation::Vec3::fromJson(initial["velocity"]);
    }

    if (model == "constant_acceleration" || model == "linear_motion") {
        if (params.contains("acceleration_m_s2") && params["acceleration_m_s2"].is_number()) {
            project.initial.acceleration = {params["acceleration_m_s2"].get<double>(), 0.0, 0.0};
        } else if (params.contains("acceleration") && params["acceleration"].is_number()) {
            project.initial.acceleration = {params["acceleration"].get<double>(), 0.0, 0.0};
        }
    }

    if (model == "force_mass") {
        project.type = "basic_dynamics";
        project.massKg = massKgFromParams(params);
        const double forceN =
            params.contains("force_N") && params["force_N"].is_number()
                ? params["force_N"].get<double>()
                : requiredNumber(params, "force", "simulate_dynamics");
        project.forceN = {forceN, 0.0, 0.0};
        if (params.contains("force_y_N") && params["force_y_N"].is_number()) {
            project.forceN.y = params["force_y_N"].get<double>();
        }
        if (params.contains("force_z_N") && params["force_z_N"].is_number()) {
            project.forceN.z = params["force_z_N"].get<double>();
        }
    } else {
        project.type = "kinematics";
    }
    project.outputs = defaultOutputs();
    project.parameters = parameterLedger(project);
    return project;
}

EngineResult SimulationEngine::runProject(const EngineRequest& request,
                                          const simulation::SimulationProject& input,
                                          bool writeArtifacts) {
    // Backfill IR containers for externally supplied projects so every
    // recorded run carries its normalized parameter ledger + outputs.
    simulation::SimulationProject project = input;
    if (project.outputs.empty()) {
        project.outputs = defaultOutputs();
    }
    if (project.parameters.empty()) {
        project.parameters = parameterLedger(project);
    }
    const simulation::ProjectValidation projectCheck = simulation::validateProject(project);
    if (!projectCheck.ok) {
        throw core::RequestValidationError(
            projectCheck.error,
            {{"project", project.toJson()}, {"project_rules", projectCheck.rules}},
            "engines");
    }

    simulation::CancelProbe cancel;
    if (request.cancelCheck) {
        cancel = request.cancelCheck;
    }
    simulation::IntegrateOutcome outcome = simulation::integrate(project, cancel);
    if (outcome.cancelled) {
        EngineResult out;
        out.success = false;
        out.engine = name_;
        out.operation = request.operation;
        out.requestId = request.requestId;
        out.result = core::Json::object();
        out.metadata = {{"engine_version", version_}, {"attempted_at", core::utcNowIso()}};
        out.addError(core::makeError(core::ErrorCode::TrinityError, outcome.error, "engines",
                                     {{"project_id", project.projectId}}));
        out.validation = validate(out);
        return out;
    }
    if (!outcome.success) {
        throw core::EngineExecutionError(outcome.error, {{"project", project.toJson()}},
                                         "engines");
    }

    const simulation::ResultChecks checks = simulation::validateResult(project, outcome.result);
    EngineResult out = successResult(
        request,
        core::Json{{"project", project.toJson()},
                   {"result", outcome.result.toJson()},
                   {"checks", validationJson(checks)},
                   {"project_rules", projectCheck.rules}});
    out.metadata["sim_checks"] = validationJson(checks);
    out.metadata["integration_method"] = outcome.result.method;

    if (writeArtifacts) {
        std::error_code ec;
        const fs::path workDir =
            fs::temp_directory_path(ec) / ("trinity_sim_" + shortId());
        fs::create_directories(workDir, ec);
        if (ec) {
            throw core::EngineExecutionError("Cannot create simulation scratch directory: " +
                                                 ec.message(),
                                             {}, "engines");
        }
        const std::string base = project.model + "_" + shortId();
        const std::string csvPath = (workDir / (base + ".csv")).string();
        const std::string jsonPath = (workDir / (base + ".json")).string();
        try {
            simulation::writeCsv(csvPath, outcome.result);
            simulation::writeJson(jsonPath, project, outcome.result, validationJson(checks));
        } catch (const std::exception& exc) {
            throw core::EngineExecutionError(std::string("Cannot write simulation artifacts: ") +
                                                 exc.what(),
                                             {}, "engines");
        }
        out.pendingArtifacts.emplace_back(csvPath, "csv");
        out.pendingArtifacts.emplace_back(jsonPath, "json");
    }

    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "simulation run",
        core::Json{{"model", project.model},
                   {"steps", outcome.result.stepCount},
                   {"method", outcome.result.method},
                   {"artifacts", static_cast<int>(out.pendingArtifacts.size())}});
    return out;
}

EngineResult SimulationEngine::executeCreate(const EngineRequest& request) {
    requireParams(request, {"model"});
    const std::string model = request.parameters.value("model", "");
    std::string type = "kinematics";
    if (model == "force_mass") {
        type = "basic_dynamics";
    } else if (model != "linear_motion" && model != "projectile" &&
               model != "constant_acceleration") {
        throw core::RequestValidationError(
            "Unsupported model '" + model +
                "'; supported: linear_motion, projectile, constant_acceleration, force_mass",
            {{"model", model}}, "engines");
    }
    simulation::SimulationProject project = projectFromParams(request.parameters, model);
    project.type = type;
    const simulation::ProjectValidation check = simulation::validateProject(project);
    if (!check.ok) {
        throw core::RequestValidationError(
            check.error, {{"project", project.toJson()}, {"project_rules", check.rules}},
            "engines");
    }
    EngineResult out =
        successResult(request, core::Json{{"project", project.toJson()},
                                          {"valid", true},
                                          {"project_rules", check.rules}});
    out.validation = validate(out);
    return out;
}

EngineResult SimulationEngine::executeRun(const EngineRequest& request) {
    simulation::SimulationProject project;
    if (request.parameters.contains("project") && request.parameters["project"].is_object()) {
        project = simulation::SimulationProject::fromJson(request.parameters["project"]);
        if (project.projectId.empty()) {
            project.projectId = core::newUuid();
        }
    } else {
        const std::string model = request.parameters.value("model", "");
        if (model.empty()) {
            throw core::RequestValidationError(
                "run_simulation requires a 'project' object or a 'model' parameter", {},
                "engines");
        }
        project = projectFromParams(request.parameters, model);
    }
    const bool wantArtifacts = request.parameters.value("write_artifacts", true);
    return runProject(request, project, wantArtifacts);
}

EngineResult SimulationEngine::executeValidate(const EngineRequest& request) {
    if (!request.parameters.contains("project") || !request.parameters["project"].is_object()) {
        throw core::RequestValidationError("validate_simulation requires a 'project' object", {},
                                           "engines");
    }
    const simulation::SimulationProject project =
        simulation::SimulationProject::fromJson(request.parameters["project"]);
    const simulation::ProjectValidation projectCheck = simulation::validateProject(project);
    core::Json data{{"project_ok", projectCheck.ok},
                    {"project_error", projectCheck.error},
                    {"project_rules", projectCheck.rules}};
    if (!projectCheck.ok) {
        EngineResult out = successResult(request, data);
        out.success = false;
        out.addError(core::makeError(core::ErrorCode::RequestValidationError, projectCheck.error,
                                     "engines"));
        out.validation = validate(out);
        return out;
    }
    simulation::IntegrateOutcome outcome = simulation::integrate(project, request.cancelCheck);
    if (!outcome.success) {
        throw core::EngineExecutionError(outcome.error.empty() ? "Validation run failed"
                                                               : outcome.error,
                                         {}, "engines");
    }
    const simulation::ResultChecks checks = simulation::validateResult(project, outcome.result);
    data["checks"] = validationJson(checks);
    data["ok"] = checks.ok;
    EngineResult out = successResult(request, data);
    out.metadata["sim_checks"] = validationJson(checks);
    out.validation = validate(out);
    return out;
}

EngineResult SimulationEngine::executeExport(const EngineRequest& request) {
    if (!request.parameters.contains("result") || !request.parameters["result"].is_object()) {
        throw core::RequestValidationError("export_results requires a 'result' object", {},
                                           "engines");
    }
    const simulation::SimulationResult result =
        simulation::SimulationResult::fromJson(request.parameters["result"]);
    simulation::SimulationProject project;
    if (request.parameters.contains("project") && request.parameters["project"].is_object()) {
        project = simulation::SimulationProject::fromJson(request.parameters["project"]);
    }
    std::error_code ec;
    const fs::path workDir = fs::temp_directory_path(ec) / ("trinity_sim_" + shortId());
    fs::create_directories(workDir, ec);
    if (ec) {
        throw core::EngineExecutionError("Cannot create simulation scratch directory: " +
                                             ec.message(),
                                         {}, "engines");
    }
    const std::string base = "export_" + shortId();
    const std::string csvPath = (workDir / (base + ".csv")).string();
    const std::string jsonPath = (workDir / (base + ".json")).string();
    simulation::writeCsv(csvPath, result);
    simulation::writeJson(jsonPath, project, result, core::Json::object());
    EngineResult out = successResult(request, core::Json{{"sample_count", result.sampleCount}});
    out.pendingArtifacts.emplace_back(csvPath, "csv");
    out.pendingArtifacts.emplace_back(jsonPath, "json");
    out.validation = validate(out);
    return out;
}

EngineResult SimulationEngine::executeOneShot(const EngineRequest& request,
                                              const std::string& model) {
    std::string type = "kinematics";
    if (model == "force_mass") {
        type = "basic_dynamics";
    }
    simulation::SimulationProject project = projectFromParams(request.parameters, model);
    project.type = type;
    return runProject(request, project, request.parameters.value("write_artifacts", true));
}

EngineResult SimulationEngine::execute(const EngineRequest& request) {
    if (request.operation == "describe") {
        EngineResult out = successResult(request,
                                         {{"engine", name_},
                                          {"version", version_},
                                          {"capabilities", capabilities_},
                                          {"supported_types", {"kinematics", "basic_dynamics"}},
                                          {"integration_methods",
                                           {"closed_form", "semi_implicit_euler"}},
                                          {"defaults",
                                           {{"dt", simulation::kDefaultDt},
                                            {"duration_s", simulation::kDefaultDurationS}}}});
        out.validation = validate(out);
        return out;
    }
    try {
        if (request.operation == "create_simulation") {
            return executeCreate(request);
        }
        if (request.operation == "run_simulation") {
            return executeRun(request);
        }
        if (request.operation == "validate_simulation") {
            return executeValidate(request);
        }
        if (request.operation == "export_results") {
            return executeExport(request);
        }
        if (request.operation == "simulate_linear_motion") {
            return executeOneShot(request, "linear_motion");
        }
        if (request.operation == "simulate_projectile") {
            return executeOneShot(request, "projectile");
        }
        if (request.operation == "simulate_constant_acceleration") {
            return executeOneShot(request, "constant_acceleration");
        }
        if (request.operation == "simulate_dynamics") {
            return executeOneShot(request, "force_mass");
        }
        return capabilityUnavailable(
            request, "Simulation operation '" + request.operation + "' is not implemented yet");
    } catch (const core::TrinityError& exc) {
        EngineResult out = failureResult(request, exc.what(),
                                         exc.toJson().value("details", core::Json::object()));
        out.errors.clear();
        out.addError(exc.info());
        out.validation = validate(out);
        return out;
    }
}

validation::ValidationResult SimulationEngine::validate(const EngineResult& result) const {
    validation::ValidationResult validation;
    validation.operation = result.operation;
    validation.jobId = result.jobId;
    validation.checks = {{"engine", "simulation"}, {"operation", result.operation}};
    if (!result.success) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "Simulation operation failed";
        validation::ValidationMessage msg;
        msg.rule = "simulation.success";
        msg.severity = validation::Severity::Error;
        msg.passed = false;
        msg.message = "Engine reported failure";
        validation.addMessage(std::move(msg));
        if (!result.errors.empty()) {
            validation.error = result.errors.front();
        }
        return validation;
    }
    if (result.metadata.contains("sim_checks")) {
        const core::Json& checks = result.metadata["sim_checks"];
        const bool ok = checks.value("ok", false);
        validation.status = ok ? validation::ValidationStatus::Verified
                               : validation::ValidationStatus::Invalid;
        validation.message = checks.value("message", "Simulation checks");
        validation::ValidationMessage msg;
        msg.rule = "simulation.checks";
        msg.severity = ok ? validation::Severity::Info : validation::Severity::Error;
        msg.passed = ok;
        msg.message = validation.message;
        msg.details = checks;
        validation.addMessage(std::move(msg));
        if (!ok && !result.errors.empty()) {
            validation.error = result.errors.front();
        }
        return validation;
    }
    if (result.operation == "describe" || result.operation == "create_simulation" ||
        result.operation == "export_results") {
        validation.status = validation::ValidationStatus::Validated;
        validation.message = "Simulation metadata operation completed";
        validation::ValidationMessage msg;
        msg.rule = "simulation.metadata";
        msg.severity = validation::Severity::Info;
        msg.passed = true;
        msg.message = validation.message;
        validation.addMessage(std::move(msg));
        return validation;
    }
    validation.status = validation::ValidationStatus::Validated;
    validation.message = "Simulation completed without independent checks recorded";
    validation::ValidationMessage msg;
    msg.rule = "simulation.completed";
    msg.severity = validation::Severity::Info;
    msg.passed = true;
    msg.message = validation.message;
    validation.addMessage(std::move(msg));
    return validation;
}

}  // namespace trinity::engines
