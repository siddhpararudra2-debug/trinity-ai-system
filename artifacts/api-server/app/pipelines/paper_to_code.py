"""Paper-to-Code — extract equations from paper text and emit runnable SymPy blocks."""
from __future__ import annotations

import re
from typing import Any

from app.engines.math_engine import MathEngine


_EQUATION_PATTERNS = [
    re.compile(r"\$\$(.+?)\$\$", re.DOTALL),
    re.compile(r"\$(.+?)\$"),
    re.compile(r"\\begin\{equation\*?\}(.+?)\\end\{equation\*?\}", re.DOTALL),
    re.compile(r"([A-Za-z][A-Za-z0-9_]*\s*=\s*[^.\n]{3,120})"),
]


class PaperToCodePipeline:
    def __init__(self) -> None:
        self._math = MathEngine()

    async def run(self, paper_text: str, equation_hint: str | None = None) -> dict[str, Any]:
        text = (equation_hint or paper_text or "").strip()
        equations = self._extract_equations(text)
        if not equations and equation_hint:
            equations = [equation_hint.strip()]
        blocks: list[dict[str, Any]] = []
        for index, expr in enumerate(equations[:5], start=1):
            sympy_code = self._to_sympy_block(expr, index)
            validation = await self._math.process(expr, "auto")
            blocks.append(
                {
                    "index": index,
                    "source_expression": expr,
                    "sympy_code": sympy_code,
                    "latex": validation.get("latex"),
                    "result": validation.get("result"),
                    "validation_status": "ok" if validation.get("result") else "partial",
                }
            )
        return {
            "engine": "paper_to_code",
            "equations_found": len(equations),
            "blocks": blocks,
            "traceability": {
                "source": "paper_text",
                "characters_analyzed": len(text),
            },
            "status": "completed" if blocks else "no_equations_found",
        }

    def _extract_equations(self, text: str) -> list[str]:
        found: list[str] = []
        for pattern in _EQUATION_PATTERNS:
            for match in pattern.finditer(text):
                candidate = match.group(1).strip()
                if len(candidate) >= 3 and candidate not in found:
                    found.append(candidate)
        return found

    @staticmethod
    def _to_sympy_block(expression: str, index: int) -> str:
        safe = expression.replace('"', '\\"')
        return (
            f"# Paper-to-Code block {index}\n"
            f"from sympy import symbols, Eq, simplify, latex\n\n"
            f"expr_{index} = \"{safe}\"\n"
            f"# Use Trinity Math Engine or sympify with safe parsing in production\n"
            f"print('Source:', expr_{index})\n"
        )
