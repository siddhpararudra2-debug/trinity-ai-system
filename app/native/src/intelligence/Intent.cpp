#include "trinity/intelligence/Intent.hpp"

namespace trinity::intelligence {

std::string toString(IntentSource source) {
    switch (source) {
        case IntentSource::Deterministic:
            return "deterministic";
        case IntentSource::Llm:
            return "llm";
        case IntentSource::Unknown:
            return "unknown";
    }
    return "unknown";
}

IntentSource intentSourceFromString(const std::string& value) {
    if (value == "deterministic") return IntentSource::Deterministic;
    if (value == "llm") return IntentSource::Llm;
    return IntentSource::Unknown;
}

std::string toString(ParseStatus status) {
    switch (status) {
        case ParseStatus::Valid:
            return "VALID";
        case ParseStatus::Incomplete:
            return "INCOMPLETE";
        case ParseStatus::Ambiguous:
            return "AMBIGUOUS";
        case ParseStatus::Invalid:
            return "INVALID";
    }
    return "INVALID";
}

ParseStatus parseStatusFromString(const std::string& value) {
    if (value == "VALID") return ParseStatus::Valid;
    if (value == "INCOMPLETE") return ParseStatus::Incomplete;
    if (value == "AMBIGUOUS") return ParseStatus::Ambiguous;
    return ParseStatus::Invalid;
}

core::Json Intent::toJson() const {
    core::Json assumptionsJson = core::Json::array();
    for (const auto& item : assumptions) {
        assumptionsJson.push_back(item);
    }
    core::Json missingJson = core::Json::array();
    for (const auto& item : missing) {
        missingJson.push_back(item);
    }
    return core::Json{{"intent_id", intentId},
                      {"domain", domain},
                      {"operation", operation},
                      {"object", object},
                      {"parameters", parameters},
                      {"constraints", constraints},
                      {"units", units},
                      {"outputs", outputs},
                      {"priority", priority},
                      {"assumptions", assumptionsJson},
                      {"missing_requirements", missingJson},
                      {"confidence", confidence},
                      {"source", source},
                      {"timestamp", timestamp},
                      {"raw_request", rawRequest},
                      {"raw_metadata", rawMetadata},
                      {"status", toString(status)}};
}

Intent Intent::fromJson(const core::Json& json) {
    Intent intent;
    intent.intentId = json.value("intent_id", "");
    intent.domain = json.value("domain", "");
    intent.operation = json.value("operation", "");
    intent.object = json.value("object", "");
    intent.parameters = json.value("parameters", core::Json::object());
    intent.constraints = json.value("constraints", core::Json::object());
    intent.units = json.value("units", "");
    intent.outputs = json.value("outputs", core::Json::array());
    intent.priority = json.value("priority", "normal");
    intent.confidence = json.value("confidence", 0.0);
    intent.source = json.value("source", "deterministic");
    intent.timestamp = json.value("timestamp", "");
    intent.rawRequest = json.value("raw_request", "");
    intent.rawMetadata = json.value("raw_metadata", core::Json::object());
    intent.status = parseStatusFromString(json.value("status", "INVALID"));
    if (json.contains("assumptions") && json["assumptions"].is_array()) {
        for (const auto& item : json["assumptions"]) {
            if (item.is_string()) {
                intent.assumptions.push_back(item.get<std::string>());
            }
        }
    }
    if (json.contains("missing_requirements") && json["missing_requirements"].is_array()) {
        for (const auto& item : json["missing_requirements"]) {
            if (item.is_string()) {
                intent.missing.push_back(item.get<std::string>());
            }
        }
    } else if (json.contains("missing") && json["missing"].is_array()) {
        for (const auto& item : json["missing"]) {
            if (item.is_string()) {
                intent.missing.push_back(item.get<std::string>());
            }
        }
    }
    return intent;
}

}  // namespace trinity::intelligence
