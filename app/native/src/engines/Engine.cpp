#include "trinity/engines/Engine.hpp"

#include <algorithm>
#include <chrono>

#include "trinity/core/Logger.hpp"
#include "trinity/core/Time.hpp"
#include "trinity/core/Uuid.hpp"

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
                    {"errors", errors},
                    {"metadata", metadata}};
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
    out.metadata = json.value("metadata", core::Json::object());
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

EngineCapability IEngine::describe() const {
    EngineCapability capability;
    capability.name = name();
    capability.version = version();
    capability.capabilities = capabilities();
    return capability;
}

EngineResult IEngine::execute(const std::string& operation,
                              const core::Json& parameters) {
    EngineRequest request;
    request.requestId = core::newUuid();
    request.engine = name();
    request.operation = operation;
    request.parameters = parameters;
    return execute(request);
}

EngineResult EngineBase::execute(const std::string& operation,
                                 const core::Json& parameters) {
    EngineRequest request;
    request.requestId = core::newUuid();
    request.engine = name_;
    request.operation = operation;
    request.parameters = parameters;
    return execute(request);
}

validation::ValidationResult EngineBase::validate(const EngineResult& result) const {
    validation::ValidationResult validation;
    validation.operation = result.operation;
    validation.jobId = result.jobId;
    if (result.success) {
        validation.status = validation::ValidationStatus::Generated;
        validation.message = "Engine completed; awaiting independent checks";
    } else {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "Engine reported failure";
        if (!result.errors.empty()) {
            validation.error = result.errors.front();
        }
    }
    return validation;
}

bool EngineBase::hasCapability(const std::string& operation) const noexcept {
    return std::find(capabilities_.begin(), capabilities_.end(), operation) !=
           capabilities_.end();
}

void EngineBase::requireCapability(const EngineRequest& request) const {
    if (!hasCapability(request.operation)) {
        core::Json available = core::Json::array();
        for (const auto& cap : capabilities_) {
            available.push_back(cap);
        }
        throw core::CapabilityUnavailableError(
            "Operation '" + request.operation + "' is not supported by engine '" + name_ +
                "'",
            {{"engine", name_}, {"operation", request.operation}, {"available", available}},
            "engines");
    }
}

void EngineBase::requireParams(const EngineRequest& request,
                               const std::vector<std::string>& keys) const {
    for (const auto& key : keys) {
        if (!request.parameters.contains(key)) {
            throw core::RequestValidationError(
                "Missing required parameter '" + key + "' for operation '" +
                    request.operation + "'",
                {{"engine", name_}, {"operation", request.operation}, {"param", key}},
                "engines");
        }
    }
}

EngineResult EngineBase::capabilityUnavailable(const EngineRequest& request,
                                               const std::string& detail) const {
    EngineResult out;
    out.success = false;
    out.engine = name_;
    out.operation = request.operation;
    out.requestId = request.requestId;
    out.result = core::Json::object();
    out.metadata = {{"engine_version", version_}, {"attempted_at", core::utcNowIso()}};
    core::Json details{{"engine", name_}, {"operation", request.operation}};
    if (!detail.empty()) {
        details["detail"] = detail;
    }
    out.addError(core::makeError(core::ErrorCode::CapabilityUnavailableError,
                                 "Operation '" + request.operation +
                                     "' is not available on engine '" + name_ + "'",
                                 "engines", details));
    out.validation = validate(out);
    return out;
}

EngineResult EngineBase::invalidRequest(const std::string& message,
                                        const core::Json& details) const {
    EngineRequest fake;
    fake.engine = name_;
    return failureResult(fake, message, details);
}

EngineResult EngineBase::failureResult(const EngineRequest& request,
                                       const std::string& message,
                                       const core::Json& details) const {
    EngineResult out;
    out.success = false;
    out.engine = name_.empty() ? request.engine : name_;
    out.operation = request.operation;
    out.requestId = request.requestId;
    out.result = core::Json::object();
    out.metadata = {{"engine_version", version_}, {"attempted_at", core::utcNowIso()}};
    out.addError(core::makeError(core::ErrorCode::EngineExecutionError, message, "engines",
                                 details));
    out.validation = validate(out);
    return out;
}

EngineResult EngineBase::successResult(const EngineRequest& request,
                                       const core::Json& data) const {
    EngineResult out;
    out.success = true;
    out.engine = name_.empty() ? request.engine : name_;
    out.operation = request.operation;
    out.requestId = request.requestId;
    out.result = data;
    out.metadata = {{"engine_version", version_}, {"completed_at", core::utcNowIso()}};
    out.validation = validate(out);
    return out;
}

}  // namespace trinity::engines
