#pragma once

// Validation primitives. GENERATED | VALIDATED | VERIFIED | INVALID are
// distinct states: generation and checking are separate steps so a
// future LLM can never self-certify output. FAILED is kept as a
// backward-compatible alias of INVALID.

#include <string>
#include <vector>

#include "../core/Json.hpp"

namespace trinity::validation {

enum class ValidationStatus {
    Generated,
    Validated,
    Verified,
    Invalid,
    Failed = Invalid,
};

enum class Severity {
    Info,
    Warning,
    Error,
};

std::string toString(ValidationStatus status);
ValidationStatus fromString(const std::string& status);
std::string toString(Severity severity);
Severity severityFromString(const std::string& severity);

struct ValidationMessage {
    std::string rule;
    Severity severity = Severity::Info;
    bool passed = true;
    std::string message;
    core::Json details = core::Json::object();

    core::Json toJson() const;
    static ValidationMessage fromJson(const core::Json& json);
};

struct ValidationResult {
    ValidationStatus status = ValidationStatus::Generated;
    std::string operation;   // engine operation that was checked
    std::string jobId;       // owning job, when known
    std::string workflowId;  // owning workflow, when known
    std::string message;     // human-readable summary
    core::Json checks = core::Json::object();
    std::vector<ValidationMessage> messages;
    core::Json error = nullptr;  // structured ErrorInfo JSON on failure

    bool passed() const noexcept;
    bool success() const noexcept { return passed(); }
    void addMessage(ValidationMessage msg);

    core::Json toJson() const;
    static ValidationResult fromJson(const core::Json& json);
};

}  // namespace trinity::validation
