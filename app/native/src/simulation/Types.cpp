#include "trinity/simulation/Types.hpp"

namespace trinity::simulation {

core::Json Vec2::toJson() const {
    return core::Json{{"x", x}, {"y", y}};
}

Vec2 Vec2::fromJson(const core::Json& json) {
    Vec2 out;
    if (json.is_object()) {
        out.x = json.value("x", 0.0);
        out.y = json.value("y", 0.0);
    }
    return out;
}

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

core::Json Orientation::toJson() const {
    return core::Json{{"roll_deg", rollDeg}, {"pitch_deg", pitchDeg}, {"yaw_deg", yawDeg}};
}

Orientation Orientation::fromJson(const core::Json& json) {
    Orientation out;
    if (json.is_object()) {
        out.rollDeg = json.value("roll_deg", 0.0);
        out.pitchDeg = json.value("pitch_deg", 0.0);
        out.yawDeg = json.value("yaw_deg", 0.0);
    }
    return out;
}

core::Json SimulationState::toJson() const {
    // "time_s" is the spec-facing key; "t" is kept as a compatible
    // alias for existing readers (UI charts, tests).
    return core::Json{{"t", t},
                      {"time_s", t},
                      {"position", position.toJson()},
                      {"velocity", velocity.toJson()},
                      {"acceleration", acceleration.toJson()}};
}

SimulationState SimulationState::fromJson(const core::Json& json) {
    SimulationState out;
    if (json.contains("t") && json["t"].is_number()) {
        out.t = json["t"].get<double>();
    } else {
        out.t = json.value("time_s", 0.0);
    }
    out.position = Vec3::fromJson(json.value("position", core::Json::object()));
    out.velocity = Vec3::fromJson(json.value("velocity", core::Json::object()));
    out.acceleration = Vec3::fromJson(json.value("acceleration", core::Json::object()));
    return out;
}

core::Json SimulationObject::toJson() const {
    return core::Json{{"object_id", objectId},
                      {"name", name},
                      {"mass_kg", massKg},
                      {"initial", initial.toJson()},
                      {"orientation", orientation.toJson()}};
}

SimulationObject SimulationObject::fromJson(const core::Json& json) {
    SimulationObject out;
    if (!json.is_object()) {
        return out;
    }
    out.objectId = json.value("object_id", "");
    out.name = json.value("name", "");
    out.massKg = json.value("mass_kg", 1.0);
    out.initial = SimulationState::fromJson(json.value("initial", core::Json::object()));
    out.orientation = Orientation::fromJson(json.value("orientation", core::Json::object()));
    return out;
}

core::Json SimulationParameter::toJson() const {
    return core::Json{{"key", key}, {"value", value}, {"unit", unit}};
}

SimulationParameter SimulationParameter::fromJson(const core::Json& json) {
    SimulationParameter out;
    if (json.is_object()) {
        out.key = json.value("key", "");
        out.value = json.value("value", 0.0);
        out.unit = json.value("unit", "");
    }
    return out;
}

core::Json SimulationOutput::toJson() const {
    return core::Json{{"key", key}, {"unit", unit}, {"value", value}};
}

SimulationOutput SimulationOutput::fromJson(const core::Json& json) {
    SimulationOutput out;
    if (json.is_object()) {
        out.key = json.value("key", "");
        out.unit = json.value("unit", "");
        out.value = json.value("value", core::Json::object());
    }
    return out;
}

namespace {

bool isDefaultState(const SimulationState& state) {
    const auto zero = [](const Vec3& v) { return v.x == 0.0 && v.y == 0.0 && v.z == 0.0; };
    return state.t == 0.0 && zero(state.position) && zero(state.velocity) &&
           zero(state.acceleration);
}

}  // namespace

void SimulationProject::syncPrimaryObject() {
    if (objects.empty()) {
        return;
    }
    const SimulationObject& primary = objects.front();
    const bool shorthandUntouched = massKg == 1.0 && isDefaultState(initial);
    if (shorthandUntouched && (primary.massKg != 1.0 || !isDefaultState(primary.initial))) {
        // External IR: objects[0] is authoritative — adopt it.
        if (primary.massKg > 0.0) {
            massKg = primary.massKg;
        }
        initial = primary.initial;
    }
    if (name.empty()) {
        name = primary.name;
    }
}

SimulationObject SimulationProject::resolvePrimary() const {
    SimulationObject primary;
    primary.objectId = "object-0";
    primary.name = name;
    primary.massKg = massKg;
    primary.initial = initial;
    if (!objects.empty()) {
        const SimulationObject& source = objects.front();
        if (!source.objectId.empty()) {
            primary.objectId = source.objectId;
        }
        if (primary.name.empty()) {
            primary.name = source.name;
        }
        primary.orientation = source.orientation;
        const bool shorthandUntouched = massKg == 1.0 && isDefaultState(initial);
        if (shorthandUntouched && (source.massKg != 1.0 || !isDefaultState(source.initial))) {
            primary.massKg = source.massKg;
            primary.initial = source.initial;
        }
    }
    if (primary.name.empty()) {
        primary.name = "primary";
    }
    return primary;
}

core::Json SimulationProject::toJson() const {
    const SimulationObject primary = resolvePrimary();
    core::Json objectsJson = core::Json::array();
    if (objects.empty()) {
        objectsJson.push_back(primary.toJson());
    } else {
        for (size_t i = 0; i < objects.size(); ++i) {
            objectsJson.push_back(i == 0 ? primary.toJson() : objects[i].toJson());
        }
    }
    core::Json parametersJson = core::Json::array();
    for (const auto& parameter : parameters) {
        parametersJson.push_back(parameter.toJson());
    }
    core::Json outputsJson = core::Json::array();
    for (const auto& output : outputs) {
        outputsJson.push_back(output.toJson());
    }
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
                      {"original_units", originalUnits},
                      {"objects", objectsJson},
                      {"parameters", parametersJson},
                      {"inputs", inputs},
                      {"boundary_conditions", boundaryConditions},
                      {"outputs", outputsJson},
                      {"metadata", metadata}};
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
    if (json.contains("objects") && json["objects"].is_array()) {
        for (const auto& item : json["objects"]) {
            out.objects.push_back(SimulationObject::fromJson(item));
        }
    }
    if (json.contains("parameters") && json["parameters"].is_array()) {
        for (const auto& item : json["parameters"]) {
            out.parameters.push_back(SimulationParameter::fromJson(item));
        }
    }
    out.inputs = json.value("inputs", core::Json::object());
    out.boundaryConditions = json.value("boundary_conditions", core::Json::object());
    if (json.contains("outputs") && json["outputs"].is_array()) {
        for (const auto& item : json["outputs"]) {
            out.outputs.push_back(SimulationOutput::fromJson(item));
        }
    }
    out.metadata = json.value("metadata", core::Json::object());
    // objects[0] is authoritative when both representations are present.
    if (!out.objects.empty()) {
        out.syncPrimaryObject();
    }
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
