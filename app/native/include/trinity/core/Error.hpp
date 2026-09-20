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
    std::string source;  // component that raised the error, e.g. "storage", "jobs"
    Json details = Json::object();
    std::string timestamp;  // UTC ISO-8601, set at creation

    Json toJson() const;
    static ErrorInfo fromJson(const Json& json);
};

ErrorCode errorCodeFromName(const std::string& name) noexcept;
ErrorInfo makeError(ErrorCode code, std::string message, std::string source = "",
                    Json details = Json::object());

class TrinityError : public std::exception {
public:
    explicit TrinityError(std::string message, Json details = Json::object(),
                          ErrorCode code = ErrorCode::TrinityError,
                          std::string source = "");

    const char* what() const noexcept override;
    ErrorCode code() const noexcept;
    const Json& details() const noexcept;
    const std::string& source() const noexcept;
    const std::string& timestamp() const noexcept;
    ErrorInfo info() const;
    Json toJson() const;

private:
    ErrorCode code_;
    std::string message_;
    Json details_;
    std::string source_;
    std::string timestamp_;
};

class RequestValidationError : public TrinityError {
public:
    explicit RequestValidationError(std::string message, Json details = Json::object(),
                                    std::string source = "");
};

class EngineNotFoundError : public TrinityError {
public:
    explicit EngineNotFoundError(std::string message, Json details = Json::object(),
                                 std::string source = "");
};

class EngineExecutionError : public TrinityError {
public:
    explicit EngineExecutionError(std::string message, Json details = Json::object(),
                                  std::string source = "");
};

class GeometryValidationError : public TrinityError {
public:
    explicit GeometryValidationError(std::string message, Json details = Json::object(),
                                     std::string source = "");
};

class JobNotFoundError : public TrinityError {
public:
    explicit JobNotFoundError(std::string message, Json details = Json::object(),
                              std::string source = "");
};

class ArtifactNotFoundError : public TrinityError {
public:
    explicit ArtifactNotFoundError(std::string message, Json details = Json::object(),
                                   std::string source = "");
};

class CapabilityUnavailableError : public TrinityError {
public:
    explicit CapabilityUnavailableError(std::string message, Json details = Json::object(),
                                        std::string source = "");
};

}  // namespace trinity::core
