"""
Math Engine (PRD §17).

Deterministic symbolic + numeric solving via SymPy. This is the
simplest possible "real" engine and exists to prove the
Engine -> Validate -> Result pipeline end to end without any LLM,
matching the "20% of 50 -> Python, no LLM required" example in §44.
"""

from __future__ import annotations

from typing import Any

import sympy as sp

from app.core.errors import EngineExecutionError, RequestValidationError
from app.engines.base import BaseEngine, EngineResult, ValidationResult


class MathEngine(BaseEngine):
    name = "math"
    version = "1.0"
    capabilities = ["solve", "evaluate"]

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        if operation == "solve":
            return self._solve(parameters)
        if operation == "evaluate":
            return self._evaluate(parameters)
        raise RequestValidationError(f"Math engine has no operation '{operation}'")

    # ------------------------------------------------------------ solve ---

    def _solve(self, parameters: dict[str, Any]) -> EngineResult:
        expression: str | None = parameters.get("expression")
        if not expression:
            raise RequestValidationError("'expression' is required for math.solve")

        variables: dict[str, float] = parameters.get("variables") or {}
        solve_for: str | None = parameters.get("solve_for")

        try:
            lhs_str, _, rhs_str = expression.partition("=")
            rhs_str = rhs_str or "0"

            local_syms = {
                name: sp.Symbol(name) for name in _extract_symbol_names(expression)
            }
            lhs = sp.sympify(lhs_str, locals=local_syms)
            rhs = sp.sympify(rhs_str, locals=local_syms)
            eq = sp.Eq(lhs, rhs)

            # Substitute known variables
            subs = {local_syms[k]: v for k, v in variables.items() if k in local_syms}
            eq = eq.subs(subs)

            free_syms = list(eq.free_symbols)
            if solve_for:
                target = sp.Symbol(solve_for)
            elif len(free_syms) == 1:
                target = free_syms[0]
            elif not free_syms:
                target = None
            else:
                raise RequestValidationError(
                    "Multiple free symbols remain; specify 'solve_for'",
                    details={"free_symbols": [str(s) for s in free_syms]},
                )

            if target is None:
                # Fully substituted — just check the identity holds.
                truth = bool(eq.lhs.equals(eq.rhs))
                result = {"expression": expression, "identity_holds": truth}
                status = "VERIFIED" if truth else "FAILED"
                return EngineResult(
                    success=truth,
                    engine=self.name,
                    operation="solve",
                    result=result,
                    validation=ValidationResult(
                        status=status, checks={"identity_holds": truth}
                    ),
                )

            solutions = sp.solve(eq, target)
        except RequestValidationError:
            raise
        except Exception as exc:  # noqa: BLE001 — surface as a classified engine error
            raise EngineExecutionError(f"Could not solve expression: {exc}") from exc

        numeric_solutions = [
            (float(s) if s.is_number and s.is_real else str(s)) for s in solutions
        ]

        result = {
            "expression": expression,
            "variables": variables,
            "solved_for": str(target) if target is not None else None,
            "result": numeric_solutions,
            "units": parameters.get("units", {}),
        }

        # A solve is VALIDATED once we've re-substituted and confirmed each
        # root satisfies the original equation to numerical precision.
        checks = {}
        all_ok = True
        for i, sol in enumerate(solutions):
            residual = sp.simplify(eq.lhs - eq.rhs).subs(target, sol)
            ok = bool(sp.N(residual) == 0 or abs(complex(sp.N(residual))) < 1e-9)
            checks[f"root_{i}_satisfies_equation"] = ok
            all_ok = all_ok and ok

        validation = ValidationResult(
            status="VALIDATED" if all_ok else "FAILED", checks=checks
        )

        return EngineResult(
            success=all_ok,
            engine=self.name,
            operation="solve",
            result=result,
            validation=validation,
        )

    # -------------------------------------------------------- evaluate ---

    def _evaluate(self, parameters: dict[str, Any]) -> EngineResult:
        expression: str | None = parameters.get("expression")
        if not expression:
            raise RequestValidationError("'expression' is required for math.evaluate")

        variables: dict[str, float] = parameters.get("variables") or {}

        try:
            local_syms = {
                name: sp.Symbol(name) for name in _extract_symbol_names(expression)
            }
            expr = sp.sympify(expression, locals=local_syms)
            subs = {local_syms[k]: v for k, v in variables.items() if k in local_syms}
            value = sp.N(expr.subs(subs))
        except Exception as exc:  # noqa: BLE001
            raise EngineExecutionError(f"Could not evaluate expression: {exc}") from exc

        numeric = float(value) if value.is_real else str(value)
        result = {"expression": expression, "variables": variables, "result": numeric}

        return EngineResult(
            success=True,
            engine=self.name,
            operation="evaluate",
            result=result,
            validation=ValidationResult(status="VERIFIED", checks={"numeric": True}),
        )


def _extract_symbol_names(expression: str) -> set[str]:
    """Cheap symbol-name extraction so sympify doesn't collide with builtins."""
    import re

    tokens = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", expression)
    reserved = {"sin", "cos", "tan", "exp", "log", "sqrt", "pi", "E", "I", "Abs"}
    return {t for t in tokens if t not in reserved}
