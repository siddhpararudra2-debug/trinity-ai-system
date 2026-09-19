#include "trinity/engines/Engine.hpp"

namespace trinity::engines {

core::Json ArtifactRef::toJson() const {
    return core::Json{{"artifact_id", artifactId},
                      {"type", type},
                      {"path", path},
                      {"size_bytes", sizeBytes},
                      {"checksum", checksum}};
}

core::Json EngineResult::toJson() const {
    core::Json json{{"success", success},
                    {"engine", engine},
                    {"operation", operation},
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

core::Json EngineCapability::toJson() const {
    return core::Json{{"name", name}, {"version", version}, {"capabilities", capabilities}};
}

EngineCapability EngineBase::describe() const {
    EngineCapability capability;
    capability.name = name_;
    capability.version = version_;
    capability.capabilities = capabilities_;
    return capability;
}

}  // namespace trinity::engines
