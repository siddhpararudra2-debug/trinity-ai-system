#pragma once

// Validation primitives. Mirrors the GENERATED | VALIDATED | VERIFIED |
// FAILED distinction in src/engines/base.py: generation and checking
// are separate steps so a future LLM can never self-certify output.

#include <string>

#include "../core/Json.hpp"

namespace trinity::validation {

enum class ValidationStatus {
    Generated,
    Validated,
    Verified,
    Failed,
};

std::string toString(ValidationStatus status);
ValidationStatus fromString(const std::string& status);

struct ValidationResult {
    ValidationStatus status = ValidationStatus::Generated;
    std::string operation;   // engine operation that was checked
    std::string jobId;       // owning job, when known
    std::string workflowId;  // owning workflow, when known
    std::string message;     // human-readable summary
    core::Json checks = core::Json::object();
    core::Json error = nullptr;  // structured ErrorInfo JSON on failure

    bool passed() const noexcept;
    bool success() const noexcept { return passed(); }

    core::Json toJson() const;
    static ValidationResult fromJson(const core::Json& json);
};

}  // namespace trinity::validation
