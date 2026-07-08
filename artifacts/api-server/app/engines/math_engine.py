"""
Trinity Math Engine — Symbolic computation via SymPy.
Handles: solving, integration, differentiation, simplification, expansion, factoring.

Security: all user expressions go through _safe_parse() before any SymPy evaluation.
sympify() / parse_expr() with unrestricted locals can execute arbitrary Python, so we
(a) reject disallowed patterns with a denylist, (b) validate with a strict character
allowlist, and (c) pass a minimal local_dict that contains only whitelisted symbols.
"""
import asyncio
import re
from typing import Any

try:
    import sympy as sp
    from sympy import symbols, solve, integrate, diff, simplify, expand, factor, latex
    from sympy.parsing.sympy_parser import (
        parse_expr,
        standard_transformations,
        implicit_multiplication_application,
    )
    SYMPY_AVAILABLE = True
except ImportError:
    SYMPY_AVAILABLE = False

# ---------------------------------------------------------------------------
# Safe parsing
# ---------------------------------------------------------------------------

# Symbols allowed in expressions
_x, _y, _z, _t, _n, _a, _b, _c = sp.symbols("x y z t n a b c") if SYMPY_AVAILABLE else (None,) * 8

SAFE_LOCALS: dict[str, Any] = {}
if SYMPY_AVAILABLE:
    SAFE_LOCALS = {
        # Variables
        "x": _x, "y": _y, "z": _z, "t": _t, "n": _n, "a": _a, "b": _b, "c": _c,
        # Constants
        "pi": sp.pi, "E": sp.E, "I": sp.I, "oo": sp.oo, "inf": sp.oo,
        # Trig
        "sin": sp.sin, "cos": sp.cos, "tan": sp.tan,
        "asin": sp.asin, "acos": sp.acos, "atan": sp.atan, "atan2": sp.atan2,
        "sinh": sp.sinh, "cosh": sp.cosh, "tanh": sp.tanh,
        # Algebra / calculus
        "sqrt": sp.sqrt, "exp": sp.exp, "log": sp.log, "ln": sp.log,
        "abs": sp.Abs, "Abs": sp.Abs,
        "factorial": sp.factorial, "binomial": sp.binomial,
        "gcd": sp.gcd, "lcm": sp.lcm,
        # Misc
        "floor": sp.floor, "ceiling": sp.ceiling, "sign": sp.sign,
    }

_TRANSFORMATIONS = ()
if SYMPY_AVAILABLE:
    _TRANSFORMATIONS = standard_transformations + (implicit_multiplication_application,)

# Characters allowed anywhere in the raw expression string (after stripping whitespace)
_ALLOWED_CHARS_RE = re.compile(r"^[\w\s\+\-\*\/\^\(\)\=\.\,\!\|<>~]+$")

# Patterns that must never appear (dunder attrs, Python builtins)
_DENY_PATTERNS = [
    "__", "import", "exec", "eval", "open", "compile",
    "os.", "sys.", "subprocess", "getattr", "setattr",
    "globals", "locals", "vars", "builtins",
]


def _safe_parse(expr_str: str) -> "sp.Expr":
    """Parse an expression string into a SymPy Expr with strict sandboxing."""
    if len(expr_str) > 300:
        raise ValueError("Expression too long (max 300 chars)")
    lower = expr_str.lower()
    for pat in _DENY_PATTERNS:
        if pat in lower:
            raise ValueError(f"Disallowed pattern in expression: '{pat}'")
    if not _ALLOWED_CHARS_RE.match(expr_str):
        raise ValueError(
            "Expression contains disallowed characters. "
            "Only standard math notation is allowed."
        )
    return parse_expr(
        expr_str,
        local_dict=SAFE_LOCALS,
        transformations=_TRANSFORMATIONS,
        evaluate=True,
    )


# ---------------------------------------------------------------------------
# Engine
# ---------------------------------------------------------------------------

class MathEngine:
    """Wrapper around SymPy for symbolic math operations."""

    async def process(self, expression: str, operation: str = "auto") -> dict[str, Any]:
        """Async entry point — runs the sync SymPy work in a thread pool."""
        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(None, self._run, expression, operation)

    # ------------------------------------------------------------------
    # Internal sync implementation
    # ------------------------------------------------------------------

    def _run(self, expression: str, operation: str) -> dict[str, Any]:
        if not SYMPY_AVAILABLE:
            return {
                "expression": expression,
                "result": "SymPy not installed. Run: pip install sympy",
                "latex": expression,
                "steps": ["Install sympy to enable the Math Engine"],
                "engine": "math",
            }
        try:
            clean = self._extract_math(expression)
            op = operation if operation != "auto" else self._detect_op(expression)
            return self._dispatch(clean, op, expression)
        except ValueError as exc:
            return {
                "expression": expression,
                "result": f"Invalid expression: {exc}",
                "latex": expression,
                "steps": [str(exc)],
                "engine": "math",
            }
        except Exception as exc:
            return {
                "expression": expression,
                "result": f"Math error: {exc}",
                "latex": expression,
                "steps": [str(exc)],
                "engine": "math",
            }

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _extract_math(self, text: str) -> str:
        """Strip natural-language words and return the math expression."""
        stopwords = [
            "solve", "calculate", "compute", "integrate", "differentiate",
            "simplify", "expand", "factor", "find", "evaluate", "what is",
            "please", "the", "of",
        ]
        cleaned = text
        for word in stopwords:
            cleaned = re.sub(rf"\b{re.escape(word)}\b", "", cleaned, flags=re.IGNORECASE)
        cleaned = cleaned.strip(" ?.,;:")
        return cleaned if cleaned else text

    def _detect_op(self, text: str) -> str:
        t = text.lower()
        if any(w in t for w in ["integrat", "integral", "antiderivative", "∫"]):
            return "integrate"
        if any(w in t for w in ["differentiat", "derivative", "d/dx", "d/dt", "d/dy"]):
            return "differentiate"
        if "simplif" in t:
            return "simplify"
        if "expand" in t:
            return "expand"
        if "factor" in t:
            return "factor"
        if "=" in text:
            return "solve"
        return "evaluate"

    def _dispatch(self, expr_str: str, op: str, original: str) -> dict[str, Any]:
        handlers = {
            "solve": self._solve,
            "integrate": self._integrate,
            "differentiate": self._differentiate,
            "simplify": self._simplify,
            "expand": self._expand,
            "factor": self._factor,
            "evaluate": self._evaluate,
        }
        return handlers.get(op, self._evaluate)(expr_str, original)

    # ------------------------------------------------------------------
    # Operation handlers — all use _safe_parse() instead of sympify()
    # ------------------------------------------------------------------

    def _solve(self, expr_str: str, original: str) -> dict[str, Any]:
        x = _x
        if "=" in expr_str:
            lhs_str, rhs_str = expr_str.split("=", 1)
            eq = _safe_parse(lhs_str.strip()) - _safe_parse(rhs_str.strip() or "0")
        else:
            eq = _safe_parse(expr_str)
        solutions = solve(eq, x)
        sol_latex = ", ".join(latex(s) for s in solutions) if solutions else r"\text{no solution}"
        return {
            "expression": original,
            "result": str(solutions),
            "latex": rf"x = {sol_latex}",
            "steps": [
                f"Set equation: {expr_str} = 0",
                f"Solve for x: {solutions}",
            ],
            "engine": "math",
        }

    def _integrate(self, expr_str: str, original: str) -> dict[str, Any]:
        x = _x
        expr = _safe_parse(expr_str)
        result = integrate(expr, x)
        return {
            "expression": original,
            "result": f"{result} + C",
            "latex": rf"\int {latex(expr)} \, dx = {latex(result)} + C",
            "steps": [
                f"Compute indefinite integral of: {expr_str}",
                f"Result: {result} + C",
            ],
            "engine": "math",
        }

    def _differentiate(self, expr_str: str, original: str) -> dict[str, Any]:
        x = _x
        expr = _safe_parse(expr_str)
        result = diff(expr, x)
        return {
            "expression": original,
            "result": str(result),
            "latex": rf"\frac{{d}}{{dx}}\left({latex(expr)}\right) = {latex(result)}",
            "steps": [
                f"Differentiate with respect to x: {expr_str}",
                f"Result: {result}",
            ],
            "engine": "math",
        }

    def _simplify(self, expr_str: str, original: str) -> dict[str, Any]:
        expr = _safe_parse(expr_str)
        result = simplify(expr)
        return {
            "expression": original,
            "result": str(result),
            "latex": latex(result),
            "steps": [f"Simplified: {expr_str} → {result}"],
            "engine": "math",
        }

    def _expand(self, expr_str: str, original: str) -> dict[str, Any]:
        expr = _safe_parse(expr_str)
        result = expand(expr)
        return {
            "expression": original,
            "result": str(result),
            "latex": latex(result),
            "steps": [f"Expanded: {expr_str} → {result}"],
            "engine": "math",
        }

    def _factor(self, expr_str: str, original: str) -> dict[str, Any]:
        expr = _safe_parse(expr_str)
        result = factor(expr)
        return {
            "expression": original,
            "result": str(result),
            "latex": latex(result),
            "steps": [f"Factored: {expr_str} → {result}"],
            "engine": "math",
        }

    def _evaluate(self, expr_str: str, original: str) -> dict[str, Any]:
        expr = _safe_parse(expr_str)
        result = simplify(expr)
        return {
            "expression": original,
            "result": str(result),
            "latex": latex(result),
            "steps": [f"Evaluated: {result}"],
            "engine": "math",
        }
