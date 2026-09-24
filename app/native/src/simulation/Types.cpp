#include "trinity/simulation/Types.hpp"

namespace trinity::simulation {

core::Json Vec3::toJson() const {
    return core::Json{{"x", x}, {"y", y}, {"z", z}};
}

Vec3 Vec3::fromJson(const core::Json& json) {
    Vec3 out;
    if (json.is_object()) {
        out.x = json.value("x", 0.0);
        out.y = json.value("y", 0.0);
        out.z = json.value("z", 0.0);
    }
    return out;
}

core::Json SimulationState::toJson() const {
    return core::Json{{"t", t},
                      {"position", position.toJson()},
                      {"velocity", velocity.toJson()},
                      {"acceleration", acceleration.toJson()}};
}

SimulationState SimulationState::fromJson(const core::Json& json) {
    SimulationState out;
    out.t = json.value("t", 0.0);
    out.position = Vec3::fromJson(json.value("position", core::Json::object()));
    out.velocity = Vec3::fromJson(json.value("velocity", core::Json::object()));
    out.acceleration = Vec3::fromJson(json.value("acceleration", core::Json::object()));
    return out;
}

core::Json SimulationProject::toJson() const {
    return core::Json{{"project_id", projectId},
                      {"name", name},
                      {"type", type},
                      {"model", model},
                      {"dt", dt},
                      {"duration_s", durationS},
                      {"initial", initial.toJson()},
                      {"mass_kg", massKg},
                      {"force_N", forceN.toJson()},
                      {"gravity", gravity},
                      {"original_units", originalUnits}};
}

SimulationProject SimulationProject::fromJson(const core::Json& json) {
    SimulationProject out;
    out.projectId = json.value("project_id", "");
    out.name = json.value("name", "");
    out.type = json.value("type", "");
    out.model = json.value("model", "");
    out.dt = json.value("dt", kDefaultDt);
    out.durationS = json.value("duration_s", kDefaultDurationS);
    out.initial = SimulationState::fromJson(json.value("initial", core::Json::object()));
    out.massKg = json.value("mass_kg", 1.0);
    out.forceN = Vec3::fromJson(json.value("force_N", core::Json::object()));
    out.gravity = json.value("gravity", 9.80665);
    out.originalUnits = json.value("original_units", core::Json::object());
    return out;
}

core::Json SimulationResult::toJson() const {
    core::Json samplesJson = core::Json::array();
    for (const auto& sample : samples) {
        samplesJson.push_back(sample.toJson());
    }
    return core::Json{{"project_id", projectId},
                      {"type", type},
                      {"model", model},
                      {"method", method},
                      {"dt", dt},
                      {"duration_s", durationS},
                      {"step_count", stepCount},
                      {"sample_count", sampleCount},
                      {"downsampled", downsampled},
                      {"samples", samplesJson},
                      {"final_state", finalState.toJson()},
                      {"summary", summary},
                      {"original_units", originalUnits}};
}

SimulationResult SimulationResult::fromJson(const core::Json& json) {
    SimulationResult out;
    out.projectId = json.value("project_id", "");
    out.type = json.value("type", "");
    out.model = json.value("model", "");
    out.method = json.value("method", "");
    out.dt = json.value("dt", 0.0);
    out.durationS = json.value("duration_s", 0.0);
    out.stepCount = json.value("step_count", 0LL);
    out.sampleCount = json.value("sample_count", 0LL);
    out.downsampled = json.value("downsampled", false);
    out.finalState = SimulationState::fromJson(json.value("final_state", core::Json::object()));
    out.summary = json.value("summary", core::Json::object());
    out.originalUnits = json.value("original_units", core::Json::object());
    if (json.contains("samples") && json["samples"].is_array()) {
        for (const auto& item : json["samples"]) {
            out.samples.push_back(SimulationState::fromJson(item));
        }
    }
    return out;
}

}  // namespace trinity::simulation
