// Trinity — typed error hierarchy mirroring backend/app/core/errors.py exactly.
// The machine codes are part of the cross-boundary contract (IPC + UI + future
// model layer must be able to classify failures identically to the V1 API).
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>

#include "Json.hpp"

namespace trinity::core {

enum class ErrorCode {
    TrinityError,
    RequestValidationError,
    EngineNotFoundError,
    EngineExecutionError,
    GeometryValidationError,
    JobNotFoundError,
    ArtifactNotFoundError,
    CapabilityUnavailableError,
    PathValidationError,
    DatabaseError,
    IpcProtocolError,
    ProcessError,
};

const char* error_code_string(ErrorCode code);
ErrorCode error_code_from_string(std::string_view code);

// Structured, serialisable application error. Never loses its classification.
class Error {
public:
    Error() = default;
    Error(ErrorCode code, std::string message, Json details = Json::object())
        : code_(code), message_(std::move(message)), details_(std::move(details)) {}

    ErrorCode code() const { return code_; }
    const std::string& message() const { return message_; }
    const Json& details() const { return details_; }

    std::string code_string() const { return error_code_string(code_); }

    Json to_json() const {
        Json out = Json::object();
        out["code"] = code_string();
        out["message"] = message_;
        out["details"] = details_;
        return out;
    }

    static Error from_json(const Json& json) {
        const Json* code = json.find("code");
        const Json* message = json.find("message");
        const Json* details = json.find("details");
        return Error(error_code_from_string(code ? code->as_string() : "trinity_error"),
                     message ? message->as_string() : std::string(),
                     details ? *details : Json::object());
    }

private:
    ErrorCode code_ = ErrorCode::TrinityError;
    std::string message_;
    Json details_ = Json::object();
};

// Exception type carrying a Trinity Error. Thrown only inside a module; every
// public C++ API returns Result-style values instead of throwing.
class TrinityException : public std::exception {
public:
    explicit TrinityException(Error error) : error_(std::move(error)) {}
    const Error& error() const { return error_; }
    const char* what() const noexcept override { return error_.message().c_str(); }

private:
    Error error_;
};

}  // namespace trinity::core
