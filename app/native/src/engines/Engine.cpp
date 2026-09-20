#include "trinity/engines/Engine.hpp"

namespace trinity::engines {

core::Json ArtifactRef::toJson() const {
    return core::Json{{"artifact_id", artifactId},
                      {"type", type},
                      {"path", path},
                      {"size_bytes", sizeBytes},
                      {"checksum", checksum}};
}

ArtifactRef ArtifactRef::fromJson(const core::Json& json) {
    ArtifactRef ref;
    ref.artifactId = json.value("artifact_id", "");
    ref.type = json.value("type", "");
    ref.path = json.value("path", "");
    ref.sizeBytes = json.value("size_bytes", 0LL);
    ref.checksum = json.value("checksum", "");
    return ref;
}

core::Json EngineRequest::toJson() const {
    return core::Json{{"request_id", requestId},
                      {"engine", engine},
                      {"operation", operation},
                      {"parameters", parameters}};
}

EngineRequest EngineRequest::fromJson(const core::Json& json) {
    EngineRequest req;
    req.requestId = json.value("request_id", "");
    req.engine = json.value("engine", "");
    req.operation = json.value("operation", "");
    req.parameters = json.value("parameters", core::Json::object());
    return req;
}

core::Json EngineResult::toJson() const {
    core::Json json{{"success", success},
                    {"engine", engine},
                    {"operation", operation},
                    {"job_id", jobId},
                    {"request_id", requestId},
                    {"result", result},
                    {"errors", errors}};
    core::Json artifactsJson = core::Json::array();
    for (const ArtifactRef& ref : artifacts) {
        artifactsJson.push_back(ref.toJson());
    }
    json["artifacts"] = artifactsJson;
    json["validation"] = validation.has_value() ? validation->toJson() : core::Json(nullptr);
    return json;
}

EngineResult EngineResult::fromJson(const core::Json& json) {
    EngineResult out;
    out.success = json.value("success", false);
    out.engine = json.value("engine", "");
    out.operation = json.value("operation", "");
    out.jobId = json.value("job_id", "");
    out.requestId = json.value("request_id", "");
    out.result = json.value("result", core::Json::object());
    out.errors = json.value("errors", std::vector<core::Json>{});
    if (json.contains("artifacts") && json["artifacts"].is_array()) {
        for (const auto& item : json["artifacts"]) {
            out.artifacts.push_back(ArtifactRef::fromJson(item));
        }
    }
    if (json.contains("validation") && !json["validation"].is_null()) {
        out.validation = validation::ValidationResult::fromJson(json["validation"]);
    }
    return out;
}

core::Json EngineCapability::toJson() const {
    return core::Json{{"name", name}, {"version", version}, {"capabilities", capabilities}};
}

EngineCapability EngineCapability::fromJson(const core::Json& json) {
    EngineCapability capability;
    capability.name = json.value("name", "");
    capability.version = json.value("version", "");
    capability.capabilities = json.value("capabilities", std::vector<std::string>{});
    return capability;
}

EngineCapability EngineBase::describe() const {
    EngineCapability capability;
    capability.name = name_;
    capability.version = version_;
    capability.capabilities = capabilities_;
    return capability;
}

}  // namespace trinity::engines
