#include "trinity/validation/ValidationResult.hpp"

namespace trinity::validation {

std::string toString(ValidationStatus status) {
    switch (status) {
        case ValidationStatus::Generated:
            return "GENERATED";
        case ValidationStatus::Validated:
            return "VALIDATED";
        case ValidationStatus::Verified:
            return "VERIFIED";
        case ValidationStatus::Failed:
            return "FAILED";
    }
    return "FAILED";
}

ValidationStatus fromString(const std::string& status) {
    if (status == "GENERATED") return ValidationStatus::Generated;
    if (status == "VALIDATED") return ValidationStatus::Validated;
    if (status == "VERIFIED") return ValidationStatus::Verified;
    return ValidationStatus::Failed;
}

bool ValidationResult::passed() const noexcept {
    return status == ValidationStatus::Validated || status == ValidationStatus::Verified;
}

core::Json ValidationResult::toJson() const {
    return core::Json{{"status", toString(status)},
                      {"operation", operation},
                      {"job_id", jobId},
                      {"workflow_id", workflowId},
                      {"message", message},
                      {"checks", checks},
                      {"error", error}};
}

ValidationResult ValidationResult::fromJson(const core::Json& json) {
    ValidationResult result;
    result.status = fromString(json.value("status", "FAILED"));
    result.operation = json.value("operation", "");
    result.jobId = json.value("job_id", "");
    result.workflowId = json.value("workflow_id", "");
    result.message = json.value("message", "");
    result.checks = json.value("checks", core::Json::object());
    result.error = json.value("error", core::Json(nullptr));
    return result;
}

}  // namespace trinity::validation
