#include "trinity/core/Error.hpp"

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

TrinityError::TrinityError(std::string message, Json details, ErrorCode code)
    : code_(code), message_(std::move(message)), details_(std::move(details)) {}

const char* TrinityError::what() const noexcept {
    return message_.c_str();
}

ErrorCode TrinityError::code() const noexcept {
    return code_;
}

const Json& TrinityError::details() const noexcept {
    return details_;
}

Json TrinityError::toJson() const {
    return Json{{"code", errorCodeName(code_)},
                {"message", message_},
                {"details", details_}};
}

RequestValidationError::RequestValidationError(std::string message, Json details)
    : TrinityError(std::move(message), std::move(details),
                   ErrorCode::RequestValidationError) {}

EngineNotFoundError::EngineNotFoundError(std::string message, Json details)
    : TrinityError(std::move(message), std::move(details), ErrorCode::EngineNotFoundError) {}

EngineExecutionError::EngineExecutionError(std::string message, Json details)
    : TrinityError(std::move(message), std::move(details),
                   ErrorCode::EngineExecutionError) {}

GeometryValidationError::GeometryValidationError(std::string message, Json details)
    : TrinityError(std::move(message), std::move(details),
                   ErrorCode::GeometryValidationError) {}

JobNotFoundError::JobNotFoundError(std::string message, Json details)
    : TrinityError(std::move(message), std::move(details), ErrorCode::JobNotFoundError) {}

ArtifactNotFoundError::ArtifactNotFoundError(std::string message, Json details)
    : TrinityError(std::move(message), std::move(details),
                   ErrorCode::ArtifactNotFoundError) {}

CapabilityUnavailableError::CapabilityUnavailableError(std::string message, Json details)
    : TrinityError(std::move(message), std::move(details),
                   ErrorCode::CapabilityUnavailableError) {}

}  // namespace trinity::core
