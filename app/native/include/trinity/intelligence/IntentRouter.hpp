#pragma once

// IntentRouter: maps a validated Intent onto a registered engine without
// executing anything. Flow: User Request -> RequirementParser -> Intent ->
// IntentValidator -> IntentRouter -> EngineRegistry (lookup only).
// The same Intent/ToolCall structs serve the deterministic parser and a
// future LLM; the LLM must never bypass validation. This component never
// calls EngineRegistry::execute or JobManager::runSync.

#include <string>

#include "../core/Json.hpp"
#include "IntentValidator.hpp"
#include "ToolCall.hpp"

namespace trinity::engines {
class EngineRegistry;
}

namespace trinity::intelligence {

struct RouteResult {
    bool routed = false;
    std::string engine;
    std::string operation;
    std::string capability;
    std::string status;  // "ROUTED" | "REJECTED"
    std::string reason;  // rejection reason when routed == false
    ToolCall toolCall;

    core::Json toJson() const;
    static RouteResult fromJson(const core::Json& json);
};

class IntentRouter {
public:
    /// Route a validated intent. Returns routed=false with a truthful
    /// reason when no registered engine can handle it. Never executes.
    RouteResult route(const Intent& intent, const engines::EngineRegistry& registry) const;

    /// Translate normalized intent parameters (*_mm keys) into the CAD
    /// engine's FrameParams naming (overall_size, arm_width, ...).
    /// Exposed for tests and traceability.
    static core::Json translateCadParams(const Intent& intent);
};

}  // namespace trinity::intelligence
