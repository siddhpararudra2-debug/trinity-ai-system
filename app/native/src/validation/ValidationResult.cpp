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
    return core::Json{{"status", toString(status)}, {"checks", checks}};
}

ValidationResult ValidationResult::fromJson(const core::Json& json) {
    ValidationResult result;
    result.status = fromString(json.value("status", "FAILED"));
    result.checks = json.value("checks", core::Json::object());
    return result;
}

}  // namespace trinity::validation
