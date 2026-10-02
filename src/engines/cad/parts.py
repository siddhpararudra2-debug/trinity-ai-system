"""
CAD part registry (PRD §12, §16).

Phase 1 of generalizing the CAD engine beyond a single hardcoded part:
each supported part type is described by a `PartDefinition` — its name,
parameter info, IR class, builder, validator and artifact-filename stem.
The engine looks parts up here instead of importing quadcopter-specific
callables directly, so adding a new part is a registration, not an edit
to engine.py. `quadcopter_frame` was the first registered part;
`enclosure`, `l_bracket`, `mounting_plate` and `standoff` joined it in
Phase 2, each implemented in its own module under src/engines/cad/.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from src.core.errors import RequestValidationError
from src.engines.cad.builder import build_quadcopter_frame
from src.engines.cad.enclosure import (
    DEFAULT_ENCLOSURE_PARAMS,
    EnclosureIR,
    build_enclosure,
    enclosure_filename_stem,
    validate_enclosure,
)
from src.engines.cad.ir import DEFAULT_QUADCOPTER_PARAMS, PartIR, QuadcopterFrameIR
from src.engines.cad.l_bracket import (
    DEFAULT_L_BRACKET_PARAMS,
    LBracketIR,
    build_l_bracket,
    l_bracket_filename_stem,
    validate_l_bracket,
)
from src.engines.cad.mounting_plate import (
    DEFAULT_MOUNTING_PLATE_PARAMS,
    MountingPlateIR,
    build_mounting_plate,
    mounting_plate_filename_stem,
    validate_mounting_plate,
)
from src.engines.cad.primitives import Mesh
from src.engines.cad.standoff import (
    DEFAULT_STANDOFF_PARAMS,
    StandoffIR,
    build_standoff,
    standoff_filename_stem,
    validate_standoff,
)
from src.engines.cad.validators import validate_quadcopter_frame


@dataclass(frozen=True)
class ParameterInfo:
    """Parameter metadata exposed for tool-caller / UI discovery.

    `min` and `max` are optional (empty) for now — Phase 1 only records
    name, unit and default.
    """

    name: str
    unit: str
    default: float
    min: float | None = None
    max: float | None = None


@dataclass(frozen=True)
class PartDefinition:
    """Everything the engine needs to generate one part type."""

    name: str
    description: str
    parameters: tuple[ParameterInfo, ...]
    ir_class: type[PartIR]
    builder: Callable[[Any], Mesh]
    validator: Callable[[Any, Mesh], tuple[bool, dict[str, Any]]]
    filename_stem: Callable[[Any], str]


_REGISTRY: dict[str, PartDefinition] = {}

DEFAULT_PART_TYPE = "quadcopter_frame"


def register_part(definition: PartDefinition) -> None:
    _REGISTRY[definition.name] = definition


def unregister_part(name: str) -> None:
    """Remove a part (used by tests to clean up temporary registrations)."""
    _REGISTRY.pop(name, None)


def list_supported_types() -> list[str]:
    return sorted(_REGISTRY)


def get_part(name: str) -> PartDefinition:
    definition = _REGISTRY.get(name)
    if definition is None:
        raise RequestValidationError(
            f"Unsupported CAD type '{name}'",
            details={"supported": list_supported_types()},
        )
    return definition


# ------------------------------------------------------- quadcopter_frame ---


def _quadcopter_filename_stem(ir: QuadcopterFrameIR) -> str:
    return f"quadcopter_frame_{int(ir.parameters['overall_size'])}mm"


_QUADCOPTER_PARAMETERS = tuple(
    ParameterInfo(
        name=key,
        unit="count" if key == "motor_count" else "mm",
        default=float(value),
    )
    for key, value in DEFAULT_QUADCOPTER_PARAMS.items()
)

register_part(
    PartDefinition(
        name="quadcopter_frame",
        description="X-configuration quadcopter frame with center plate and four arms.",
        parameters=_QUADCOPTER_PARAMETERS,
        ir_class=QuadcopterFrameIR,
        builder=build_quadcopter_frame,
        validator=validate_quadcopter_frame,
        filename_stem=_quadcopter_filename_stem,
    )
)


# ------------------------------------------------- Phase 2 part library ---


def _parameters(defaults: dict[str, Any]) -> tuple[ParameterInfo, ...]:
    """Parameter specs in declaration order — the IR defaults' key order."""
    return tuple(
        ParameterInfo(name=key, unit="mm", default=value)
        for key, value in defaults.items()
    )


register_part(
    PartDefinition(
        name="enclosure",
        description="Open-top rectangular enclosure with a floor and four walls.",
        parameters=_parameters(DEFAULT_ENCLOSURE_PARAMS),
        ir_class=EnclosureIR,
        builder=build_enclosure,
        validator=validate_enclosure,
        filename_stem=enclosure_filename_stem,
    )
)

register_part(
    PartDefinition(
        name="l_bracket",
        description="Right-angle bracket: a flat base with one upright wall.",
        parameters=_parameters(DEFAULT_L_BRACKET_PARAMS),
        ir_class=LBracketIR,
        builder=build_l_bracket,
        validator=validate_l_bracket,
        filename_stem=l_bracket_filename_stem,
    )
)

register_part(
    PartDefinition(
        name="mounting_plate",
        description="Flat plate with four raised cylindrical mounting bosses.",
        parameters=_parameters(DEFAULT_MOUNTING_PLATE_PARAMS),
        ir_class=MountingPlateIR,
        builder=build_mounting_plate,
        validator=validate_mounting_plate,
        filename_stem=mounting_plate_filename_stem,
    )
)

register_part(
    PartDefinition(
        name="standoff",
        description="Cylindrical post on a wider foot for spacing boards apart.",
        parameters=_parameters(DEFAULT_STANDOFF_PARAMS),
        ir_class=StandoffIR,
        builder=build_standoff,
        validator=validate_standoff,
        filename_stem=standoff_filename_stem,
    )
)
