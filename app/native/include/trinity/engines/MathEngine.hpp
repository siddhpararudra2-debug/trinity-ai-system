#pragma once

// Math engine: deterministic, LLM-independent numeric evaluation and
// equation solving. `evaluate_expression` handles plain arithmetic;
// `evaluate` adds variables, functions and constants; `solve` finds
// single-variable roots with residual verification. No symbolic
// algebra system ships in C++ — transcendental/multi-variable systems
// beyond numeric roots stay a later phase.

#include <map>
#include <string>
#include <vector>

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

    /// Extended evaluator with variable bindings, functions
    /// (sin/cos/tan/exp/log/sqrt/abs) and constants (pi/e).
    static double evaluateWithVariables(const std::string& expression,
                                        const std::map<std::string, double>& variables);

    /// Variable names occurring in an expression (functions/constants excluded).
    static std::vector<std::string> symbolsIn(const std::string& expression);

private:
    EngineResult executeSolve(const EngineRequest& request);
    validation::ValidationResult validateSolve(
        const EngineResult& result, validation::ValidationResult validation) const;
};

}  // namespace trinity::engines
