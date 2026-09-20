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
        case ValidationStatus::Invalid:
            return "INVALID";
    }
    return "INVALID";
}

ValidationStatus fromString(const std::string& status) {
    if (status == "GENERATED") return ValidationStatus::Generated;
    if (status == "VALIDATED") return ValidationStatus::Validated;
    if (status == "VERIFIED") return ValidationStatus::Verified;
    if (status == "INVALID" || status == "FAILED") return ValidationStatus::Invalid;
    return ValidationStatus::Invalid;
}

std::string toString(Severity severity) {
    switch (severity) {
        case Severity::Info:
            return "INFO";
        case Severity::Warning:
            return "WARNING";
        case Severity::Error:
            return "ERROR";
    }
    return "INFO";
}

Severity severityFromString(const std::string& severity) {
    if (severity == "WARNING") return Severity::Warning;
    if (severity == "ERROR") return Severity::Error;
    return Severity::Info;
}

core::Json ValidationMessage::toJson() const {
    return core::Json{{"rule", rule},
                      {"severity", toString(severity)},
                      {"passed", passed},
                      {"message", message},
                      {"details", details}};
}

ValidationMessage ValidationMessage::fromJson(const core::Json& json) {
    ValidationMessage msg;
    msg.rule = json.value("rule", "");
    msg.severity = severityFromString(json.value("severity", "INFO"));
    msg.passed = json.value("passed", true);
    msg.message = json.value("message", "");
    msg.details = json.value("details", core::Json::object());
    return msg;
}

bool ValidationResult::passed() const noexcept {
    return status == ValidationStatus::Validated || status == ValidationStatus::Verified;
}

void ValidationResult::addMessage(ValidationMessage msg) {
    messages.push_back(std::move(msg));
}

core::Json ValidationResult::toJson() const {
    core::Json messagesJson = core::Json::array();
    for (const auto& msg : messages) {
        messagesJson.push_back(msg.toJson());
    }
    return core::Json{{"status", toString(status)},
                      {"operation", operation},
                      {"job_id", jobId},
                      {"workflow_id", workflowId},
                      {"message", message},
                      {"checks", checks},
                      {"messages", messagesJson},
                      {"error", error}};
}

ValidationResult ValidationResult::fromJson(const core::Json& json) {
    ValidationResult result;
    result.status = fromString(json.value("status", "INVALID"));
    result.operation = json.value("operation", "");
    result.jobId = json.value("job_id", "");
    result.workflowId = json.value("workflow_id", "");
    result.message = json.value("message", "");
    result.checks = json.value("checks", core::Json::object());
    result.error = json.value("error", core::Json(nullptr));
    if (json.contains("messages") && json["messages"].is_array()) {
        for (const auto& item : json["messages"]) {
            result.messages.push_back(ValidationMessage::fromJson(item));
        }
    }
    return result;
}

}  // namespace trinity::validation
