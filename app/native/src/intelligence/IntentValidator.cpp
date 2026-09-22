#include "trinity/intelligence/IntentValidator.hpp"

#include <algorithm>
#include <cmath>

#include "trinity/core/Logger.hpp"
#include "trinity/engines/EngineRegistry.hpp"

namespace trinity::intelligence {
namespace {

void addCheck(validation::ValidationResult& out, const std::string& rule,
              validation::Severity severity, bool passed, const std::string& message,
              const core::Json& details = core::Json::object()) {
    validation::ValidationMessage msg;
    msg.rule = rule;
    msg.severity = severity;
    msg.passed = passed;
    msg.message = message;
    msg.details = details;
    out.addMessage(std::move(msg));
}

bool isValidDomain(const std::string& domain) {
    return domain == "cad" || domain == "math";
}

bool isValidOperation(const std::string& domain, const std::string& operation) {
    if (domain == "cad") {
        return operation == "generate" || operation == "describe";
    }
    if (domain == "math") {
        return operation == "evaluate" || operation == "evaluate_expression" ||
                operation == "solve" || operation == "solve_linear" ||
                operation == "solve_quadratic" || operation == "convert" ||
                operation == "formula";
    }
    return false;
}

bool isFiniteNumber(const core::Json& value) {
    if (!value.is_number()) {
        return false;
    }
    return std::isfinite(value.get<double>());
}

}  // namespace

validation::ValidationResult IntentValidator::validate(const Intent& intent) const {
    validation::ValidationResult out;
    out.operation = "intent.validate";
    out.checks = core::Json::object();
    out.checks["intent_id"] = intent.intentId;
    out.checks["domain"] = intent.domain;
    out.checks["operation"] = intent.operation;
    bool ok = true;

    // Required fields.
    if (intent.domain.empty() || intent.operation.empty() || intent.object.empty()) {
        ok = false;
        addCheck(out, "intent.required_fields", validation::Severity::Error, false,
                 "Intent is missing required fields",
                 core::Json{{"domain", intent.domain},
                            {"operation", intent.operation},
                            {"object", intent.object}});
    } else {
        addCheck(out, "intent.required_fields", validation::Severity::Info, true,
                 "Required fields present");
    }

    // Valid domain.
    if (!isValidDomain(intent.domain)) {
        ok = false;
        addCheck(out, "intent.domain", validation::Severity::Error, false,
                 "Unsupported domain '" + intent.domain + "'",
                 core::Json{{"domain", intent.domain},
                            {"supported", core::Json::array({"cad", "math"})}});
    } else {
        addCheck(out, "intent.domain", validation::Severity::Info, true,
                 "Domain '" + intent.domain + "' is supported");
    }

    // Valid operation.
    if (!isValidOperation(intent.domain, intent.operation)) {
        ok = false;
        addCheck(out, "intent.operation", validation::Severity::Error, false,
                 "Unsupported operation '" + intent.operation + "' for domain '" +
                     intent.domain + "'",
                 core::Json{{"domain", intent.domain}, {"operation", intent.operation}});
    } else {
        addCheck(out, "intent.operation", validation::Severity::Info, true,
                 "Operation '" + intent.operation + "' is valid for '" + intent.domain + "'");
    }

    // Incomplete intents (missing requirements) fail validation by design:
    // the parser must not invent values, and the validator must not pass
    // them on to routing.
    if (intent.status == ParseStatus::Incomplete || intent.status == ParseStatus::Ambiguous ||
        !intent.missing.empty()) {
        ok = false;
        core::Json missingJson = core::Json::array();
        for (const auto& item : intent.missing) {
            missingJson.push_back(item);
        }
        addCheck(out, "intent.completeness", validation::Severity::Error, false,
                 "Intent is incomplete or ambiguous; missing requirements must be resolved",
                 core::Json{{"status", toString(intent.status)}, {"missing", missingJson}});
    } else {
        addCheck(out, "intent.completeness", validation::Severity::Info, true,
                 "Intent is complete");
    }

    // Parameter types.
    if (!intent.parameters.is_object()) {
        ok = false;
        addCheck(out, "intent.param_type", validation::Severity::Error, false,
                 "Intent parameters must be an object");
    } else if (intent.domain == "math") {
        const std::string op = intent.operation;
        if (op == "evaluate" || op == "evaluate_expression" || op == "solve") {
            if (!intent.parameters.contains("expression") ||
                !intent.parameters["expression"].is_string() ||
                intent.parameters["expression"].get<std::string>().empty()) {
                ok = false;
                addCheck(out, "intent.param_type", validation::Severity::Error, false,
                         "Math intent requires a non-empty string 'expression'",
                         core::Json{{"parameters", intent.parameters}});
            } else {
                addCheck(out, "intent.param_type", validation::Severity::Info, true,
                         "Math expression parameter present");
            }
        } else if (op == "solve_linear") {
            if (!isFiniteNumber(intent.parameters.value("a", core::Json(nullptr))) ||
                !isFiniteNumber(intent.parameters.value("b", core::Json(nullptr)))) {
                ok = false;
                addCheck(out, "intent.param_type", validation::Severity::Error, false,
                         "solve_linear requires finite numeric 'a' and 'b'",
                         core::Json{{"parameters", intent.parameters}});
            } else {
                addCheck(out, "intent.param_type", validation::Severity::Info, true,
                         "Linear coefficients present");
            }
        } else if (op == "solve_quadratic") {
            if (!isFiniteNumber(intent.parameters.value("a", core::Json(nullptr))) ||
                !isFiniteNumber(intent.parameters.value("b", core::Json(nullptr))) ||
                !isFiniteNumber(intent.parameters.value("c", core::Json(nullptr)))) {
                ok = false;
                addCheck(out, "intent.param_type", validation::Severity::Error, false,
                         "solve_quadratic requires finite numeric 'a', 'b' and 'c'",
                         core::Json{{"parameters", intent.parameters}});
            } else {
                addCheck(out, "intent.param_type", validation::Severity::Info, true,
                         "Quadratic coefficients present");
            }
        } else if (op == "convert") {
            const bool valueOk =
                isFiniteNumber(intent.parameters.value("value", core::Json(nullptr)));
            const bool fromOk = intent.parameters.contains("from") &&
                                intent.parameters["from"].is_string() &&
                                !intent.parameters["from"].get<std::string>().empty();
            const bool toOk = intent.parameters.contains("to") &&
                              intent.parameters["to"].is_string() &&
                              !intent.parameters["to"].get<std::string>().empty();
            if (!valueOk || !fromOk || !toOk) {
                ok = false;
                addCheck(out, "intent.param_type", validation::Severity::Error, false,
                         "convert requires finite 'value' plus 'from'/'to' unit strings",
                         core::Json{{"parameters", intent.parameters}});
            } else {
                addCheck(out, "intent.param_type", validation::Severity::Info, true,
                         "Conversion parameters present");
            }
        } else if (op == "formula") {
            const bool nameOk = intent.parameters.contains("name") &&
                                intent.parameters["name"].is_string() &&
                                !intent.parameters["name"].get<std::string>().empty();
            const bool inputsOk = intent.parameters.contains("inputs") &&
                                  intent.parameters["inputs"].is_object() &&
                                  !intent.parameters["inputs"].empty();
            if (!nameOk || !inputsOk) {
                ok = false;
                addCheck(out, "intent.param_type", validation::Severity::Error, false,
                         "formula requires a 'name' plus non-empty object 'inputs'",
                         core::Json{{"parameters", intent.parameters}});
            } else {
                addCheck(out, "intent.param_type", validation::Severity::Info, true,
                         "Formula parameters present");
            }
        } else {
            // Validated operation set already gates unknown ops; structural
            // checks above cover every known math operation.
            addCheck(out, "intent.param_type", validation::Severity::Info, true,
                     "Math parameters present");
        }
    } else if (intent.domain == "cad") {
        bool typesOk = true;
        for (auto it = intent.parameters.begin(); it != intent.parameters.end(); ++it) {
            if (!it.value().is_number()) {
                typesOk = false;
                addCheck(out, "intent.param_type", validation::Severity::Error, false,
                         "CAD parameter '" + it.key() + "' must be numeric",
                         core::Json{{"parameter", it.key()}});
            }
        }
        if (typesOk) {
            addCheck(out, "intent.param_type", validation::Severity::Info, true,
                     "CAD parameter types are numeric");
        } else {
            ok = false;
        }
    } else {
        addCheck(out, "intent.param_type", validation::Severity::Warning, true,
                 "Parameter type check skipped for unknown domain");
    }

    // Numeric ranges (V1 engineering rules, mirrors FrameParams limits).
    if (intent.domain == "cad" && intent.parameters.is_object()) {
        auto checkRange = [&](const std::string& key, double lo, double hi) {
            if (!intent.parameters.contains(key)) {
                return;
            }
            const core::Json& value = intent.parameters[key];
            if (!isFiniteNumber(value)) {
                ok = false;
                addCheck(out, "intent.range", validation::Severity::Error, false,
                         "Parameter '" + key + "' must be finite",
                         core::Json{{key, value}});
                return;
            }
            const double v = value.get<double>();
            if (v <= lo || v > hi) {
                ok = false;
                addCheck(out, "intent.range", validation::Severity::Error, false,
                         "Parameter '" + key + "' is outside the supported range",
                         core::Json{{key, v}, {"exclusive_min", lo}, {"inclusive_max", hi}});
            } else {
                addCheck(out, "intent.range", validation::Severity::Info, true,
                         "Parameter '" + key + "' is within range",
                         core::Json{{key, v}});
            }
        };
        checkRange("overall_size_mm", 0.0, 1000.0);
        checkRange("arm_thickness_mm", 0.0, 50.0);
        checkRange("thickness_mm", 0.0, 50.0);
        checkRange("diameter_mm", 0.0, 500.0);
        checkRange("spacing_mm", 0.0, 1000.0);
        checkRange("width_mm", 0.0, 1000.0);
        checkRange("extra_length_mm", 0.0, 1000.0);
        checkRange("angle_deg", -3600.0, 3600.0);
        // Cross-field rule: overall must exceed a plausible center plate.
        if (intent.parameters.contains("overall_size_mm") &&
            isFiniteNumber(intent.parameters["overall_size_mm"])) {
            const double overall = intent.parameters["overall_size_mm"].get<double>();
            if (overall <= 26.0 && intent.object == "quadcopter_frame") {
                ok = false;
                addCheck(out, "intent.range", validation::Severity::Error, false,
                         "overall_size_mm must exceed the 26 mm center plate",
                         core::Json{{"overall_size_mm", overall}});
            }
        }
    }

    // Unit consistency: normalized keys must carry canonical suffixes and
    // raw metadata (when present) must agree on the canonical unit.
    {
        bool unitsOk = true;
        if (intent.domain == "cad") {
            for (auto it = intent.parameters.begin(); it != intent.parameters.end(); ++it) {
                const std::string& key = it.key();
                if (key.size() >= 3 && key.substr(key.size() - 3) == "_mm") {
                    continue;
                }
                if (key.size() >= 4 && key.substr(key.size() - 4) == "_deg") {
                    continue;
                }
                if (key.size() >= 2 && key.substr(key.size() - 2) == "_g") {
                    continue;
                }
                if (key.size() >= 2 && key.substr(key.size() - 2) == "_N") {
                    continue;
                }
                if (key.size() >= 3 && key.substr(key.size() - 3) == "_Pa") {
                    continue;
                }
                if (key == "expression" || key == "motor_count") {
                    continue;
                }
                unitsOk = false;
                addCheck(out, "intent.units", validation::Severity::Error, false,
                         "Parameter '" + key + "' lacks a canonical unit suffix",
                         core::Json{{"parameter", key}});
            }
        }
        if (intent.rawMetadata.contains("original_quantities") &&
            intent.rawMetadata["original_quantities"].is_array()) {
            for (const auto& entry : intent.rawMetadata["original_quantities"]) {
                if (entry.contains("error")) {
                    unitsOk = false;
                    ok = false;
                    addCheck(out, "intent.units", validation::Severity::Error, false,
                             "Unsupported unit in request", entry);
                }
            }
        }
        if (unitsOk) {
            addCheck(out, "intent.units", validation::Severity::Info, true,
                     "Units are consistent with the canonical representation");
        } else {
            ok = false;
        }
    }

    // Capability check is registry-dependent; without a registry we warn.
    addCheck(out, "intent.capability", validation::Severity::Warning, true,
             "Capability check deferred: no registry provided");

    out.status = ok ? validation::ValidationStatus::Validated
                    : validation::ValidationStatus::Invalid;
    out.message = ok ? "Intent is valid" : "Intent validation failed";
    core::Logger::instance().info(
        "intelligence", "intent validated",
        core::Json{{"intent_id", intent.intentId}, {"valid", ok}});
    return out;
}

validation::ValidationResult IntentValidator::validate(
    const Intent& intent, const engines::EngineRegistry& registry) const {
    validation::ValidationResult out = validate(intent);
    // Replace the deferred capability message with a live check.
    out.messages.erase(
        std::remove_if(out.messages.begin(), out.messages.end(),
                       [](const validation::ValidationMessage& m) {
                           return m.rule == "intent.capability";
                       }),
        out.messages.end());

    bool capable = false;
    std::string engine;
    std::string detail;
    if (intent.domain == "cad") {
        engine = "cad";
    } else if (intent.domain == "math") {
        engine = "math";
    }
    if (!engine.empty() && registry.has(engine)) {
        const std::vector<std::string> caps = registry.listCapabilities(engine);
        // Intent operations map onto engine capabilities: math evaluate*,
        // solve, solve_linear, solve_quadratic, convert and formula are
        // direct; cad generate/describe are direct.
        if (std::find(caps.begin(), caps.end(), intent.operation) != caps.end()) {
            // CAD object gate: only quadcopter_frame is implemented.
            if (intent.domain == "cad" && intent.object != "quadcopter_frame" &&
                intent.operation == "generate") {
                detail = "CadEngine does not support object '" + intent.object + "'";
            } else {
                capable = true;
            }
        } else {
            detail = "Engine '" + engine + "' has no capability '" + intent.operation + "'";
        }
    } else if (engine.empty()) {
        detail = "No engine mapping for domain '" + intent.domain + "'";
    } else {
        detail = "Engine '" + engine + "' is not registered";
    }

    if (capable) {
        addCheck(out, "intent.capability", validation::Severity::Info, true,
                 "Engine '" + engine + "' supports '" + intent.operation + "'",
                 core::Json{{"engine", engine}, {"operation", intent.operation}});
    } else {
        addCheck(out, "intent.capability", validation::Severity::Error, false,
                 detail.empty() ? "Engine capability check failed" : detail,
                 core::Json{{"engine", engine}, {"operation", intent.operation}});
        out.status = validation::ValidationStatus::Invalid;
        out.message = "Intent validation failed";
    }
    return out;
}

}  // namespace trinity::intelligence
