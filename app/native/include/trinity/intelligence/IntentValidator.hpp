#pragma once

// IntentValidator: enforces that a parsed (deterministic or LLM-produced)
// Intent is well-formed before it may reach the IntentRouter or any engine.
// An invalid intent must never reach an engine. Uses the existing
// validation::ValidationResult envelope so results serialize like every
// other Trinity validation.

#include "Intent.hpp"
#include "../validation/ValidationResult.hpp"

namespace trinity::engines {
class EngineRegistry;
}

namespace trinity::intelligence {

class IntentValidator {
public:
    /// Validate without registry access (skips the capability check with
    /// a warning message). Prefer the registry overload whenever possible.
    validation::ValidationResult validate(const Intent& intent) const;

    /// Full validation including engine capability compatibility against
    /// the live registry (read-only: has/listCapabilities only).
    validation::ValidationResult validate(const Intent& intent,
                                          const engines::EngineRegistry& registry) const;
};

}  // namespace trinity::intelligence
