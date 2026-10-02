"""Deterministic V1 natural-language router; intentionally no model calls.

A rule list: every rule carries a name, trigger phrases and an extractor.
An extractor returns the parse shape {domain, operation, object, parameters,
units} or None when the rule does not apply; the first rule that matches
wins. Anything no rule matches raises RequestValidationError with the
nearest part names and example sentences instead of guessing — the parser
never invents domains or parameters it cannot derive from the input.

Recognised abilities: verb synonyms (make/build/design/create/generate),
unit conversion (mm/cm/m/inch), dimension patterns ("100 x 60 x 40 mm"),
named values ("3 mm thick", "4 motors"), part-name synonyms ("drone frame",
"box", "bracket") and math routing ("solve ...").
"""

from __future__ import annotations

import re
from collections.abc import Callable
from dataclasses import dataclass
from difflib import get_close_matches
from typing import Any

from src.core.errors import RequestValidationError

_SUPPORTED_EXAMPLE = "Create a 50 mm quadcopter frame"

# Longest-first alternation: mm before m, inches before inch.
_UNIT_ALT = r"mm|cm|inches|inch|m"
_UNIT_TO_MM: dict[str, float] = {
    "mm": 1.0,
    "cm": 10.0,
    "m": 1000.0,
    "inch": 25.4,
    "inches": 25.4,
}

_VERB_RE = re.compile(
    r"\b(?:made|built|(?:make|build|design|create|generate)(?:s|d|ing)?)\b",
    re.IGNORECASE,
)
_NUMBER_RE = re.compile(
    rf"(?<![a-z0-9.])(\d+(?:\.\d+)?)\s*({_UNIT_ALT})?\b",
    re.IGNORECASE,
)

# Dims map positionally onto these parameters, in declaration order
# (L x W x H per part); leftovers are dropped, missing ones stay default.
_DIM_KEYS: dict[str, tuple[str, ...]] = {
    "quadcopter_frame": ("overall_size",),
    "enclosure": ("length", "width", "height"),
    "mounting_plate": ("length", "width"),
    "l_bracket": ("base_length", "height", "width"),
    "standoff": ("height",),
}

_PART_PHRASES: dict[str, tuple[str, ...]] = {
    "quadcopter_frame": (
        "quadcopter frame",
        "quadcopter frames",
        "quadrotor frame",
        "drone frame",
        "drone frames",
        "quadcopter",
        "quadcopters",
        "quadrotor",
        "quadrotors",
        "drone",
        "drones",
    ),
    "enclosure": ("enclosure", "enclosures", "box", "boxes"),
    "mounting_plate": ("mounting plate", "mounting plates", "mount plate"),
    "l_bracket": ("l-bracket", "l-brackets", "l bracket", "bracket", "brackets"),
    "standoff": ("standoff", "standoffs", "stand-off", "stand-offs"),
}

# "<number> [unit] <keyword>" named values, per part. Every keyword maps
# to a parameter the part actually has; motor_count is a count (int),
# everything else is a length in mm.
_KEYWORD_MAP: dict[str, dict[str, str]] = {
    "quadcopter_frame": {
        "motors": "motor_count",
        "motor": "motor_count",
        "thick": "plate_thickness",
        "thickness": "plate_thickness",
    },
    "enclosure": {
        "long": "length",
        "length": "length",
        "wide": "width",
        "width": "width",
        "tall": "height",
        "high": "height",
        "height": "height",
        "thick": "wall_thickness",
        "thickness": "wall_thickness",
        "walls": "wall_thickness",
        "wall": "wall_thickness",
    },
    "mounting_plate": {
        "long": "length",
        "length": "length",
        "wide": "width",
        "width": "width",
        "thick": "thickness",
        "thickness": "thickness",
        "diameter": "boss_diameter",
        "inset": "boss_inset",
    },
    "l_bracket": {
        "long": "base_length",
        "length": "base_length",
        "wide": "width",
        "width": "width",
        "tall": "height",
        "high": "height",
        "height": "height",
        "thick": "thickness",
        "thickness": "thickness",
    },
    "standoff": {
        "tall": "height",
        "high": "height",
        "height": "height",
        "diameter": "diameter",
        "foot": "foot_height",
    },
}

_COUNT_PARAMS = frozenset({"motor_count"})

EXAMPLE_SENTENCES: dict[str, tuple[str, ...]] = {
    "quadcopter_frame": (
        "Create a 50 mm quadcopter frame",
        "Build a drone frame with 4 motors",
    ),
    "enclosure": (
        "Make a box 100 x 60 x 40 mm",
        "Design an enclosure 3 mm thick",
    ),
    "mounting_plate": ("Create a mounting plate 100 x 60 mm",),
    "l_bracket": ("Build a bracket 40 x 40 x 20 mm",),
    "standoff": ("Make a 20 mm standoff",),
    "math": ("Solve x**2 - 4 = 0",),
}


@dataclass(frozen=True)
class Rule:
    """One routing rule: name, trigger phrases and an extractor."""

    name: str
    phrases: tuple[str, ...]
    extractor: Callable[[str], dict | None]


def _phrase_re(phrases: tuple[str, ...]) -> re.Pattern[str]:
    alternatives = "|".join(
        re.escape(p) for p in sorted(phrases, key=len, reverse=True)
    )
    return re.compile(rf"(?<![a-z])(?:{alternatives})(?![a-z])", re.IGNORECASE)


def _named_re(part: str) -> re.Pattern[str]:
    keywords = "|".join(sorted(_KEYWORD_MAP[part], key=len, reverse=True))
    return re.compile(
        rf"(?<![a-z0-9.])(\d+(?:\.\d+)?)\s*({_UNIT_ALT})?\s*"
        rf"(?<![a-z])({keywords})(?![a-z])",
        re.IGNORECASE,
    )


def _to_mm(value: float, unit: str | None, fallback: str) -> float:
    return value * _UNIT_TO_MM[(unit or fallback).lower()]


def _make_cad_extractor(
    part: str, phrases: tuple[str, ...]
) -> Callable[[str], dict | None]:
    """Build the extractor for one CAD part.

    Requires a verb and a part phrase, then reads named values
    ("<number> [unit] <keyword>") plus the remaining numbers as
    positional dimensions. Each number uses its own unit; bare numbers
    take the trailing unit of the text (default mm). Named values win
    over positional dims on the same parameter.
    """
    object_re = _phrase_re(phrases)
    named_re = _named_re(part)
    dim_keys = _DIM_KEYS[part]

    def extract(text: str) -> dict | None:
        if not _VERB_RE.search(text) or not object_re.search(text):
            return None

        # Trailing explicit unit is the fallback for every bare number.
        units_in_order = [
            m.group(2).lower() for m in _NUMBER_RE.finditer(text) if m.group(2)
        ]
        fallback = units_in_order[-1] if units_in_order else "mm"

        # Named values: "<number> [unit] <keyword>".
        parameters: dict[str, Any] = {}
        consumed: list[tuple[int, int]] = []
        for match in named_re.finditer(text):
            key = _KEYWORD_MAP[part][match.group(3).lower()]
            value = float(match.group(1))
            consumed.append(match.span(1))
            if key in _COUNT_PARAMS:
                if not value.is_integer():
                    raise RequestValidationError(f"{key} must be an integer")
                parameters[key] = int(value)
            else:
                parameters[key] = _to_mm(value, match.group(2), fallback)

        # Remaining numbers are positional dimensions (L x W x H order).
        tokens = [
            m
            for m in _NUMBER_RE.finditer(text)
            if not any(m.start(1) < hi and m.end(1) > lo for lo, hi in consumed)
        ]
        for key, match in zip(dim_keys, tokens, strict=False):
            parameters.setdefault(
                key, _to_mm(float(match.group(1)), match.group(2), fallback)
            )

        if not parameters:
            return None

        if part == "quadcopter_frame":
            size = parameters.get("overall_size")
            if size is not None and size <= 0:
                raise RequestValidationError(
                    "Quadcopter frame size must be a positive number of millimetres",
                    details={
                        "overall_size": size,
                        "supported_example": _SUPPORTED_EXAMPLE,
                    },
                )

        return {
            "domain": "cad",
            "operation": "generate",
            "object": part,
            "parameters": parameters,
            "units": "mm",
        }

    return extract


_MATH_RE = re.compile(r"^\s*solve\b[,\s]*(.*)$", re.IGNORECASE | re.DOTALL)


def _extract_math(text: str) -> dict | None:
    match = _MATH_RE.match(text.strip())
    if not match:
        return None
    expression = match.group(1).strip().strip("'\"")
    if not expression:
        raise RequestValidationError("'expression' is required for math.solve")
    return {
        "domain": "math",
        "operation": "solve",
        "object": "expression",
        "parameters": {"expression": expression},
        "units": "",
    }


# Order matters: math first (so "solve ..." never falls into CAD), then
# parts from most specific phrase to most generic.
RULES: tuple[Rule, ...] = (
    Rule("math_solve", ("solve",), _extract_math),
    *(
        Rule(part, phrases, _make_cad_extractor(part, phrases))
        for part, phrases in _PART_PHRASES.items()
    ),
)


def _nearest_parts(text: str) -> list[str]:
    aliases = {
        alias: rule.name
        for rule in RULES
        for alias in (rule.name, *(p.lower() for p in rule.phrases))
        if rule.name != "math_solve"
    }
    words = re.findall(r"[a-z0-9]+", text.lower())
    nearest: list[str] = []
    for word in words:
        for alias in get_close_matches(word, sorted(aliases), n=3, cutoff=0.6):
            part = aliases[alias]
            if part not in nearest:
                nearest.append(part)
    return nearest[:3]


def _all_examples() -> list[str]:
    return [s for sentences in EXAMPLE_SENTENCES.values() for s in sentences]


def parse_requirement(text: str) -> dict:
    for rule in RULES:
        parsed = rule.extractor(text)
        if parsed is not None:
            return parsed
    raise RequestValidationError(
        "No deterministic V1 parser matched this requirement",
        details={
            "supported_example": _SUPPORTED_EXAMPLE,
            "parser_scope": (
                "cad part generation and math solve (V1 deterministic "
                "rule-list router)"
            ),
            "nearest_parts": _nearest_parts(text),
            "examples": _all_examples(),
        },
    )
