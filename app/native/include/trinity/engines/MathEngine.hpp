#pragma once

// Math engine: deterministic, LLM-independent arithmetic evaluation.
// This phase implements only `evaluate_expression` (e.g. "2 + 3 * 4"
// -> 14). Full symbolic/numeric functionality is a later phase.

#include <string>

#include "Engine.hpp"

namespace trinity::engines {

class MathEngine : public EngineBase {
public:
    MathEngine();

    EngineResult execute(const EngineRequest& request) override;
    validation::ValidationResult validate(const EngineResult& result) const override;

    /// Deterministic expression evaluator. Throws RequestValidationError
    /// on empty input, invalid characters, or malformed syntax.
    static double evaluateExpression(const std::string& expression);
};

}  // namespace trinity::engines
