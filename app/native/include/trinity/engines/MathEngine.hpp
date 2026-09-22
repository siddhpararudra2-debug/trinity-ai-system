#pragma once

// Math engine: deterministic, LLM-independent numeric evaluation and
// equation solving. `evaluate_expression` handles plain arithmetic;
// `evaluate` adds variables, functions and constants; `solve` finds
// single-variable roots with residual verification; `solve_linear` and
// `solve_quadratic` solve coefficient-form equations in closed form;
// `convert` converts compatible dimensional units; `formula` evaluates
// registered engineering formulas (ohm/power/force). No symbolic
// algebra system ships in C++ — transcendental/multi-variable systems
// beyond numeric roots stay a later phase. Future operations
// (derivative, integral, matrix_operations, unit-aware engineering
// calculations, symbolic_solve) plug in as new capabilities with the
// same request/validate/result contract.

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
    EngineResult executeSolveQuadratic(const EngineRequest& request);
    EngineResult executeConvert(const EngineRequest& request);
    EngineResult executeFormula(const EngineRequest& request);
    validation::ValidationResult validateSolve(
        const EngineResult& result, validation::ValidationResult validation) const;
};

}  // namespace trinity::engines
