#include "Error.hpp"

#include <stdexcept>

namespace trinity::core {

const char* error_code_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::TrinityError: return "trinity_error";
        case ErrorCode::RequestValidationError: return "request_validation_error";
        case ErrorCode::EngineNotFoundError: return "engine_not_found";
        case ErrorCode::EngineExecutionError: return "engine_execution_error";
        case ErrorCode::GeometryValidationError: return "geometry_validation_error";
        case ErrorCode::JobNotFoundError: return "job_not_found";
        case ErrorCode::ArtifactNotFoundError: return "artifact_not_found";
        case ErrorCode::CapabilityUnavailableError: return "capability_unavailable";
        case ErrorCode::PathValidationError: return "path_validation_error";
        case ErrorCode::DatabaseError: return "database_error";
        case ErrorCode::IpcProtocolError: return "ipc_protocol_error";
        case ErrorCode::ProcessError: return "process_error";
    }
    return "trinity_error";
}

ErrorCode error_code_from_string(std::string_view code) {
    if (code == "request_validation_error") return ErrorCode::RequestValidationError;
    if (code == "engine_not_found") return ErrorCode::EngineNotFoundError;
    if (code == "engine_execution_error") return ErrorCode::EngineExecutionError;
    if (code == "geometry_validation_error") return ErrorCode::GeometryValidationError;
    if (code == "job_not_found") return ErrorCode::JobNotFoundError;
    if (code == "artifact_not_found") return ErrorCode::ArtifactNotFoundError;
    if (code == "capability_unavailable") return ErrorCode::CapabilityUnavailableError;
    if (code == "path_validation_error") return ErrorCode::PathValidationError;
    if (code == "database_error") return ErrorCode::DatabaseError;
    if (code == "ipc_protocol_error") return ErrorCode::IpcProtocolError;
    if (code == "process_error") return ErrorCode::ProcessError;
    return ErrorCode::TrinityError;
}

}  // namespace trinity::core
