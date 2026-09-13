"""Deterministic V1 natural-language router; intentionally no model calls."""

from __future__ import annotations

import re

from app.core.errors import RequestValidationError


def parse_requirement(text: str) -> dict:
    match = re.search(
        r"(?:create|generate)\s+(?:a\s+)?(\d+(?:\.\d+)?)\s*mm\s+(?:quad(?:copter|rotor)|drone)\s+frame",
        text,
        re.IGNORECASE,
    )
    if match:
        return {
            "domain": "cad",
            "operation": "generate",
            "object": "quadcopter_frame",
            "parameters": {"overall_size": float(match.group(1))},
            "units": "mm",
        }
    raise RequestValidationError(
        "No deterministic V1 parser matched this requirement",
        details={"supported_example": "Create a 50 mm quadcopter frame"},
    )
