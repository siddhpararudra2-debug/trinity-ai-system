#include "trinity/core/Error.hpp"

#include "trinity/core/Time.hpp"

namespace trinity::core {

namespace {

const char* kNames[] = {
    "trinity_error",
    "request_validation_error",
    "engine_not_found",
    "engine_execution_error",
    "geometry_validation_error",
    "job_not_found",
    "artifact_not_found",
    "capability_unavailable",
};

}  // namespace

const char* errorCodeName(ErrorCode code) noexcept {
    return kNames[static_cast<int>(code)];
}

ErrorCode errorCodeFromName(const std::string& name) noexcept {
    for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i) {
        if (name == kNames[i]) {
            return static_cast<ErrorCode>(i);
        }
    }
    return ErrorCode::TrinityError;
}

Json ErrorInfo::toJson() const {
    return Json{{"code", errorCodeName(code)},
                {"message", message},
                {"source", source},
                {"details", details},
                {"timestamp", timestamp}};
}

ErrorInfo ErrorInfo::fromJson(const Json& json) {
    ErrorInfo info;
    info.code = errorCodeFromName(json.value("code", "trinity_error"));
    info.message = json.value("message", "");
    info.source = json.value("source", "");
    info.details = json.value("details", Json::object());
    info.timestamp = json.value("timestamp", "");
    return info;
}

ErrorInfo makeError(ErrorCode code, std::string message, std::string source,
                    Json details) {
    ErrorInfo info;
    info.code = code;
    info.message = std::move(message);
    info.source = std::move(source);
    info.details = std::move(details);
    info.timestamp = utcNowIso();
    return info;
}

TrinityError::TrinityError(std::string message, Json details, ErrorCode code,
                           std::string source)
    : code_(code),
      message_(std::move(message)),
      details_(std::move(details)),
      source_(std::move(source)),
      timestamp_(utcNowIso()) {}

const char* TrinityError::what() const noexcept {
    return message_.c_str();
}

ErrorCode TrinityError::code() const noexcept {
    return code_;
}

const Json& TrinityError::details() const noexcept {
    return details_;
}

const std::string& TrinityError::source() const noexcept {
    return source_;
}

const std::string& TrinityError::timestamp() const noexcept {
    return timestamp_;
}

ErrorInfo TrinityError::info() const {
    ErrorInfo info;
    info.code = code_;
    info.message = message_;
    info.source = source_;
    info.details = details_;
    info.timestamp = timestamp_;
    return info;
}

Json TrinityError::toJson() const {
    return info().toJson();
}

RequestValidationError::RequestValidationError(std::string message, Json details,
                                               std::string source)
    : TrinityError(std::move(message), std::move(details),
                   ErrorCode::RequestValidationError, std::move(source)) {}

EngineNotFoundError::EngineNotFoundError(std::string message, Json details,
                                         std::string source)
    : TrinityError(std::move(message), std::move(details), ErrorCode::EngineNotFoundError,
                   std::move(source)) {}

EngineExecutionError::EngineExecutionError(std::string message, Json details,
                                           std::string source)
    : TrinityError(std::move(message), std::move(details),
                   ErrorCode::EngineExecutionError, std::move(source)) {}

GeometryValidationError::GeometryValidationError(std::string message, Json details,
                                                 std::string source)
    : TrinityError(std::move(message), std::move(details),
                   ErrorCode::GeometryValidationError, std::move(source)) {}

JobNotFoundError::JobNotFoundError(std::string message, Json details, std::string source)
    : TrinityError(std::move(message), std::move(details), ErrorCode::JobNotFoundError,
                   std::move(source)) {}

ArtifactNotFoundError::ArtifactNotFoundError(std::string message, Json details,
                                             std::string source)
    : TrinityError(std::move(message), std::move(details),
                   ErrorCode::ArtifactNotFoundError, std::move(source)) {}

CapabilityUnavailableError::CapabilityUnavailableError(std::string message, Json details,
                                                       std::string source)
    : TrinityError(std::move(message), std::move(details),
                   ErrorCode::CapabilityUnavailableError, std::move(source)) {}

}  // namespace trinity::core
