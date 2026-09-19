#pragma once

// Trinity error hierarchy.
//
// Mirrors src/core/errors.py: every failure is classified so a future
// caller (eventually an LLM) can tell "bad input" apart from
// "engine tried and failed" apart from "system broke".
// Codes are stable API: GENERATED / VALIDATED / VERIFIED states in
// validation/ and the ToolResponse envelope depend on them.

#include <exception>
#include <string>

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
};

const char* errorCodeName(ErrorCode code) noexcept;

struct ErrorInfo {
    ErrorCode code = ErrorCode::TrinityError;
    std::string message;
    Json details = Json::object();
};

class TrinityError : public std::exception {
public:
    explicit TrinityError(std::string message, Json details = Json::object(),
                          ErrorCode code = ErrorCode::TrinityError);

    const char* what() const noexcept override;
    ErrorCode code() const noexcept;
    const Json& details() const noexcept;
    Json toJson() const;

private:
    ErrorCode code_;
    std::string message_;
    Json details_;
};

class RequestValidationError : public TrinityError {
public:
    explicit RequestValidationError(std::string message, Json details = Json::object());
};

class EngineNotFoundError : public TrinityError {
public:
    explicit EngineNotFoundError(std::string message, Json details = Json::object());
};

class EngineExecutionError : public TrinityError {
public:
    explicit EngineExecutionError(std::string message, Json details = Json::object());
};

class GeometryValidationError : public TrinityError {
public:
    explicit GeometryValidationError(std::string message, Json details = Json::object());
};

class JobNotFoundError : public TrinityError {
public:
    explicit JobNotFoundError(std::string message, Json details = Json::object());
};

class ArtifactNotFoundError : public TrinityError {
public:
    explicit ArtifactNotFoundError(std::string message, Json details = Json::object());
};

class CapabilityUnavailableError : public TrinityError {
public:
    explicit CapabilityUnavailableError(std::string message, Json details = Json::object());
};

}  // namespace trinity::core
