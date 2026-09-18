// Trinity — deterministic C++ math engine (no LLM, no SymPy dependency).
//
// Port of the *semantics* of backend/app/engines/math/engine.py:
//   evaluate: compute an expression with known variables; result VERIFIED.
//   solve:    solve equations for one unknown; every root is re-substituted
//             into the original equation and must satisfy it to <1e-9 before
//             the result may claim VALIDATED (brief §Validation: never claim
//             verification without evidence).
//
// C++ V1 supports polynomial forms (linear/quadratic) via exact algebra plus
// the deterministic expression evaluator; transcendental solving stays with
// the Python engine host over IPC.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "../Engine.hpp"

namespace trinity::engines::math {

// Deterministic expression evaluator: + - * / % ^, parentheses, unary minus,
// functions sin cos tan sqrt log ln exp abs, constants pi and e.
// Throws core::TrinityException (request_validation_error) on bad syntax or
// unknown identifiers. Division/modulo by zero is an execution error.
double evaluate_expression(const std::string& expression,
                           const std::map<std::string, double>& variables);

// Extract identifiers that are not functions/constants (mirrors _extract_symbol_names).
std::vector<std::string> extract_symbol_names(const std::string& expression);

struct SolveResult {
    bool verified = false;
    std::vector<double> roots;
    std::string detail;  // human-readable description of the solved form
};

// Solves polynomial equations (after variable substitution) up to degree 2 in
// the requested unknown. nullopt when the form is unsupported.
std::optional<SolveResult> solve_polynomial(const std::string& lhs, const std::string& rhs,
                                            const std::string& unknown,
                                            const std::map<std::string, double>& variables);

class MathEngine : public IEngine {
public:
    EngineDescriptor describe() const override;
    ExecutionOutput execute(const std::string& capability,
                            const core::Json& parameters) override;
    core::Json validate(const core::Json& payload) override;
    EngineHealth health() const override;
};

}  // namespace trinity::engines::math
