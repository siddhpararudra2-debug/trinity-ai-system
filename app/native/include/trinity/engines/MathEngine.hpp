#pragma once

// Math engine: deterministic, LLM-independent numeric evaluation and
// equation solving. `evaluate_expression` handles plain arithmetic;
// `evaluate` adds variables, functions and constants; `solve` finds
// single-variable roots with residual verification; `solve_linear` solves
// a scalar coefficient equation and, in matrix form (A/b arrays), any
// linear system up to 16 unknowns via Gaussian elimination with partial
// pivoting (singular systems are a defined honest outcome);
// `solve_quadratic` solves coefficient-form quadratics in closed form;
// `differentiate` and `simplify` are hand-rolled symbolic operations over
// an AST (no CAS dependency) with an honest refusal set (abs(), constant^
// variable with base <= 0) and numeric derivative verification;
// `solve_system` runs Newton with a numeric Jacobian and residual
// verification; `convert` converts compatible dimensional units; `formula`
// evaluates registered engineering formulas (ohm/power/force). Symbolic
// integration, eigendecomposition and statistics remain later phases and
// plug in as new capabilities with the same request/validate contract.

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

    /// Convert a value between compatible dimensional units.
    /// Throws RequestValidationError on unknown units or dimensional
    /// mismatch (e.g. kg -> mm). Supported: length (mm/cm/m/in),
    /// mass (g/kg), angle (deg/rad), force (N), pressure (Pa/kPa/MPa).
    static double convertUnits(double value, const std::string& from,
                               const std::string& to);

    /// Canonical dimension of a unit ("length"|"mass"|"angle"|"force"|
    /// "pressure"); empty when the unit is unknown.
    static std::string dimensionOf(const std::string& unit);

    /// Hand-rolled symbolic derivative of a plain expression (no '=')
    /// w.r.t. `variable`, simplified to a stable form. Throws
    /// RequestValidationError on syntax errors, on an unknown `variable`,
    /// for abs() (piecewise — genuinely not covered) and for
    /// constant^variable bases <= 0 (undefined over the reals).
    static std::string differentiateSimplified(const std::string& expression,
                                               const std::string& variable);

    /// Symbolic simplification: constant folding plus identity rules
    /// (x+0, 0-x, x*0, x*1, x/1, x^0, x^1, x-x), iterated to a fixed
    /// point. Divisions by zero, negative-base fractional powers and 0^0
    /// stay unfolded — they are refusals at evaluation, not silent fudge.
    static std::string simplifyExpression(const std::string& expression);

    /// Names of the registered engineering formulas ("ohm", "power", "force").
    static std::vector<std::string> formulaNames();
    /// Evaluate a registered formula from numeric inputs. Returns
    /// {formula, inputs, outputs, units}. Throws RequestValidationError
    /// on unknown formula / missing inputs / division by zero and
    /// EngineExecutionError on non-finite results.
    static core::Json formulaResult(const std::string& name, const core::Json& inputs);

private:
    EngineResult executeSolve(const EngineRequest& request);
    EngineResult executeSolveLinear(const EngineRequest& request);
    EngineResult executeSolveLinearMatrix(const EngineRequest& request);
    EngineResult executeSolveQuadratic(const EngineRequest& request);
    EngineResult executeDifferentiate(const EngineRequest& request);
    EngineResult executeSimplify(const EngineRequest& request);
    EngineResult executeSolveSystem(const EngineRequest& request);
    EngineResult executeConvert(const EngineRequest& request);
    EngineResult executeFormula(const EngineRequest& request);
    validation::ValidationResult validateSolve(
        const EngineResult& result, validation::ValidationResult validation) const;
};

}  // namespace trinity::engines
