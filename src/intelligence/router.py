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
    "research": ("Search the documents for thermal runaway",),
    "simulation": ("Simulate a projectile for 2 s at 5 m/s",),
    "pcb": ("Make a pcb 50 x 40 mm with an esp32 and a regulator",),
    "robotics": (
        "Compute forward kinematics with joints 0.5, -0.3",
        "Plan a trajectory from 0, 0 to 1.0, -1.0 over 1 s",
    ),
    "firmware": ("Create firmware for an esp32 named rover_controller",),
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


# ---------------------------------------------------------------------------
# Bridged engines.
#
# These route to engines that only exist when the native backend is attached.
# Without it the call still parses — and then fails truthfully at the job
# level with capability_unavailable — rather than being rejected at the router
# as if the request were nonsense. A rule that silently hid six working
# engines behind "no parser matched" was the actual bug.
#
# Every extractor below emits the exact parameter contract its engine
# declares. Anything it cannot derive is left at the engine's default; the
# router never invents a value.
# ---------------------------------------------------------------------------

# A search request has to look like a search: a retrieval verb AND a
# document-ish noun. An earlier version also accepted "for"/"about"/"on"
# as keywords, which hijacked half the CAD vocabulary -- "find a bracket
# for a 40 mm build" became a research query because "for" is a
# near-universal preposition. Requiring the noun keeps research from
# eating geometry requests, and a search the router cannot recognise is
# refused honestly rather than misrouted.
_SEARCH_RE = re.compile(
    r"\b(?:search|research|look\s+up|find)\b[^.?!]*?"
    r"\b(?:documents?|docs?|index|papers?|literature|corpus|notes?)\b\s*(.+)$",
    re.IGNORECASE,
)


def _extract_research_search(text: str) -> dict | None:
    match = _SEARCH_RE.search(text.strip())
    if not match:
        return None
    # The document noun can be followed by a preposition -- "find papers
    # about X" -- so one leading preposition comes off before the rest is
    # treated as the query. Safe now that the noun already gated the match.
    query = re.sub(
        r"^\s*(?:for|about|on|regarding|concerning)\s+",
        "",
        match.group(1),
        flags=re.IGNORECASE,
    ).strip().strip("\"'?. ")
    if not query:
        return None
    return {
        "domain": "research",
        "operation": "search",
        "object": "research",
        "parameters": {"query": query},
        "units": "",
    }


_SIM_RE = re.compile(
    r"\b(?:simulate|simulate a|simulation|run a simulation|projectile)\b",
    re.IGNORECASE,
)
_DURATION_RE = re.compile(
    r"(?:for|over|duration(?: of)?)\s*(\d+(?:\.\d+)?)\s*(?:s\b|sec|secs|second|seconds)",
    re.IGNORECASE,
)
_SPEED_RE = re.compile(
    r"(?:at|initial\s+speed(?: of)?|velocity\s+of)\s*(\d+(?:\.\d+)?)\s*m/s",
    re.IGNORECASE,
)
_ACCEL_RE = re.compile(
    r"(?:accelerat\w*(?:ion)?\s*(?:of|at)?|a\s*=)\s*(\d+(?:\.\d+)?)\s*m/s(?:\^?2|2)",
    re.IGNORECASE,
)


def _extract_simulation(text: str) -> dict | None:
    if not _SIM_RE.search(text):
        return None
    parameters: dict[str, Any] = {}
    for regex, key in (
        (_DURATION_RE, "duration_s"),
        (_SPEED_RE, "initial_velocity_m_s"),
        (_ACCEL_RE, "acceleration_m_s2"),
    ):
        match = regex.search(text)
        if match:
            parameters[key] = float(match.group(1))
    if not parameters:
        return None
    return {
        "domain": "simulation",
        "operation": "simulate",
        "object": "simulation",
        "parameters": parameters,
        "units": "s, m/s, m/s^2",
    }


# Part presets the native PCB engine actually has footprints for. A word that
# is not here is ignored rather than routed: the engine would reject it, and
# a board with a silently dropped component is worse than a clear one.
_PCB_PARTS: dict[str, tuple[str, ...]] = {
    "esp32": ("esp32", "esp-32"),
    "imu": ("imu", "mpu6050", "mpu-6050", "mpu9250", "mpu-9250", "accelerometer", "gyroscope"),
    "regulator": ("regulator", "ams1117", "ldo"),
    "resistor": ("resistor",),
    "capacitor": ("capacitor", "cap"),
    "led": ("led",),
}
_PCB_RE = re.compile(r"\b(?:pcb|printed circuit board|circuit board|board layout)\b", re.IGNORECASE)


def _extract_pcb_generate(text: str) -> dict | None:
    if not _PCB_RE.search(text):
        return None
    lowered = text.lower()
    components = [
        {"part": part}
        for part, aliases in _PCB_PARTS.items()
        if any(re.search(rf"(?<![a-z0-9]){re.escape(a)}(?![a-z0-9])", lowered) for a in aliases)
    ]
    if not components:
        return None
    parameters: dict[str, Any] = {"components": components}
    dims = re.findall(
        r"(?<![a-z0-9.])(\d+(?:\.\d+)?)\s*(mm|cm|inch|inches)?", text, re.IGNORECASE
    )
    values = [_to_mm(float(v), u, "mm") for v, u in dims]
    for key, value in zip(("width_mm", "height_mm"), values, strict=False):
        parameters.setdefault(key, value)
    return {
        "domain": "pcb",
        "operation": "generate",
        "object": "pcb",
        "parameters": parameters,
        "units": "mm",
    }


_JOINT_LIST_RE = re.compile(r"(?:joints?|angles?)\s*[:=]?\s*([-\d.,\s]+)", re.IGNORECASE)


def _joints(text: str) -> list[float]:
    match = _JOINT_LIST_RE.search(text)
    if not match:
        return []
    values = []
    for token in re.split(r"[,\s]+", match.group(1).strip()):
        if not token:
            continue
        try:
            values.append(float(token))
        except ValueError:
            break
    return values


_KINEMATICS_RE = re.compile(
    r"\b(?:forward kinematics|kinematics|end effector|end-effector)\b", re.IGNORECASE
)


def _extract_robotics_kinematics(text: str) -> dict | None:
    if not _KINEMATICS_RE.search(text):
        return None
    angles = _joints(text)
    if not angles:
        return None
    return {
        "domain": "robotics",
        "operation": "kinematics",
        "object": "robotics",
        "parameters": {"joint_angles": angles},
        "units": "rad",
    }


_TRAJECTORY_RE = re.compile(
    r"\b(?:trajectory|joint[- ]space path|plan a path|motion plan)\b", re.IGNORECASE
)
# A joint list is numbers, optionally comma separated. Capturing the list as
# a unit (rather than lazily up to a delimiter) is what stops "to 1.0, -1.0"
# from being cut to "1" by the "." it contains.
_JOINT_LIST_PATTERN = r"-?\d+(?:\.\d+)?(?:\s*,\s*-?\d+(?:\.\d+)?)*"
_FROM_TO_RE = re.compile(
    rf"\bfrom\s*({_JOINT_LIST_PATTERN})\s*(?:to|->|→)\s*({_JOINT_LIST_PATTERN})",
    re.IGNORECASE,
)


def _numbers(raw: str) -> list[float]:
    values = []
    for token in re.split(r"[,\s]+", raw.strip()):
        if not token:
            continue
        try:
            values.append(float(token))
        except ValueError:
            return []
    return values


def _extract_robotics_trajectory(text: str) -> dict | None:
    if not _TRAJECTORY_RE.search(text):
        return None
    match = _FROM_TO_RE.search(text)
    parameters: dict[str, Any] = {}
    if match:
        start, goal = _numbers(match.group(1)), _numbers(match.group(2))
        if not start or len(start) != len(goal):
            raise RequestValidationError(
                "A trajectory needs the same number of joints in start and goal",
                details={"start": start, "goal": goal},
            )
        parameters["joint_start"] = start
        parameters["joint_goal"] = goal
    duration = _DURATION_RE.search(text)
    if duration:
        parameters["duration_s"] = float(duration.group(1))
    if not parameters:
        return None
    return {
        "domain": "robotics",
        "operation": "trajectory_plan",
        "object": "robotics",
        "parameters": parameters,
        "units": "rad, s",
    }


_MCU_RE = re.compile(r"\b(?:esp32|stm32f401re|rp2040)\b", re.IGNORECASE)
_FIRMWARE_RE = re.compile(
    r"\b(?:firmware|microcontroller project|mcu project|embedded project)\b", re.IGNORECASE
)


def _extract_firmware_create(text: str) -> dict | None:
    if not _FIRMWARE_RE.search(text):
        return None
    mcu = _MCU_RE.search(text)
    named = re.search(r"\bnamed\s+([\w.-]+)", text, re.IGNORECASE)
    project = named.group(1) if named else (text.strip()[:40] or "firmware_project")
    parameters: dict[str, Any] = {"name": re.sub(r"\W+", "_", project)}
    if mcu:
        parameters["mcu"] = mcu.group(0).upper()
    return {
        "domain": "firmware",
        "operation": "create",
        "object": "firmware",
        "parameters": parameters,
        "units": "",
    }


# Order matters: math first (so "solve ..." never falls into anything else),
# then the bridged engines whose triggers are unambiguous, then the CAD
# parts from most specific phrase to most generic.
#
# The public rule list keeps research after the CAD entries so introspection
# stays stable. parse_requirement() checks explicit document retrieval intent
# before CAD, except when a CAD part and dimensions make it a geometry request.
RULES: tuple[Rule, ...] = (
    Rule("math_solve", ("solve",), _extract_math),
    Rule("simulation_simulate", ("simulate", "simulation", "projectile"), _extract_simulation),
    Rule("pcb_generate", ("pcb", "circuit board", "board layout"), _extract_pcb_generate),
    Rule(
        "robotics_trajectory",
        ("trajectory", "plan a path", "motion plan"),
        _extract_robotics_trajectory,
    ),
    Rule(
        "robotics_kinematics",
        ("forward kinematics", "kinematics", "end effector"),
        _extract_robotics_kinematics,
    ),
    Rule(
        "firmware_create",
        ("firmware", "microcontroller project", "mcu project"),
        _extract_firmware_create,
    ),
    *(
        Rule(part, phrases, _make_cad_extractor(part, phrases))
        for part, phrases in _PART_PHRASES.items()
    ),
    Rule("research_search", ("search", "research", "look up"), _extract_research_search),
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
        if rule.name == "math_solve":
            if parsed is not None:
                return parsed
            # Require both retrieval language and a document noun before
            # choosing research. If a CAD part and dimensions are also
            # present, leave it to the CAD rules (or refuse if no CAD verb
            # was given) instead of silently searching for a geometry job.
            research = _extract_research_search(text)
            if research is not None:
                has_part = any(
                    _phrase_re(phrases).search(text)
                    for phrases in _PART_PHRASES.values()
                )
                has_dimensions = bool(_NUMBER_RE.search(text))
                if not (has_part and has_dimensions):
                    return research
        elif rule.name != "research_search" and parsed is not None:
            return parsed
    raise RequestValidationError(
        "No deterministic V1 parser matched this requirement",
        details={
            "supported_example": _SUPPORTED_EXAMPLE,
            "parser_scope": (
                "cad part generation, math solve, and the bridged engines "
                "research/simulation/pcb/robotics/firmware (V1 deterministic "
                "rule-list router)"
            ),
            "nearest_parts": _nearest_parts(text),
            "examples": _all_examples(),
        },
    )
