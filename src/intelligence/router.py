"""Deterministic V1 natural-language router; intentionally no model calls.

Scope: CAD quadcopter-frame requests only. Anything the pattern does not
match exactly raises RequestValidationError instead of guessing — the parser
never invents domains or parameters it cannot derive from the input.
"""

from __future__ import annotations

import re

from src.core.errors import RequestValidationError

_SUPPORTED_EXAMPLE = "Create a 50 mm quadcopter frame"


def parse_requirement(text: str) -> dict:
    match = re.search(
        r"(?:create|generate)\s+(?:a\s+)?(\d+(?:\.\d+)?)\s*mm\s+(?:quad(?:copter|rotor)|drone)\s+frame",
        text,
        re.IGNORECASE,
    )
    if match:
        size = float(match.group(1))
        if size <= 0:
            raise RequestValidationError(
                "Quadcopter frame size must be a positive number of millimetres",
                details={"overall_size": size, "supported_example": _SUPPORTED_EXAMPLE},
            )
        return {
            "domain": "cad",
            "operation": "generate",
            "object": "quadcopter_frame",
            "parameters": {"overall_size": size},
            "units": "mm",
        }
    raise RequestValidationError(
        "No deterministic V1 parser matched this requirement",
        details={
            "supported_example": _SUPPORTED_EXAMPLE,
            "parser_scope": "cad quadcopter frame generation (V1 deterministic router)",
        },
    )
