#pragma once

// Structured requirement intent: the single validated data contract between
// natural-language understanding (deterministic parser today, LLM later)
// and engine routing/execution. Models and parsers emit data only; the
// IntentValidator must approve an Intent before IntentRouter maps it to
// a ToolCall. Units in `parameters` are normalized to canonical form
// (length->mm, angle->deg, mass->g, force->N, pressure->Pa); the original
// value/unit strings are preserved in `rawMetadata` for traceability.

#include <string>
#include <vector>

#include "../core/Json.hpp"

namespace trinity::intelligence {

enum class IntentSource {
    Deterministic,
    Llm,
    Unknown,
};

std::string toString(IntentSource source);
IntentSource intentSourceFromString(const std::string& value);

enum class ParseStatus {
    Valid,
    Incomplete,
    Ambiguous,
    Invalid,
};

std::string toString(ParseStatus status);
ParseStatus parseStatusFromString(const std::string& value);

struct Intent {
    std::string intentId;
    std::string domain;      // e.g. "cad", "math"
    std::string operation;   // e.g. "generate", "evaluate_expression", "solve"
    std::string object;      // e.g. "quadcopter_frame", "plate", "expression"
    core::Json parameters = core::Json::object();    // normalized values
    core::Json constraints = core::Json::object();
    std::string units;       // canonical default, e.g. "mm"
    core::Json outputs = core::Json::array();        // requested outputs
    std::string priority = "normal";                 // "low" | "normal" | "high"
    std::vector<std::string> assumptions;
    std::vector<std::string> missing;                // missing requirements
    double confidence = 0.0;
    std::string source = "deterministic";            // "deterministic" | "llm"
    std::string timestamp;                           // UTC ISO-8601
    std::string rawRequest;                          // original user text
    core::Json rawMetadata = core::Json::object();   // original units/values

    ParseStatus status = ParseStatus::Invalid;

    core::Json toJson() const;
    static Intent fromJson(const core::Json& json);
};

}  // namespace trinity::intelligence
