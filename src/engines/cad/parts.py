"""
CAD part registry (PRD §12, §16).

Phase 1 of generalizing the CAD engine beyond a single hardcoded part:
each supported part type is described by a `PartDefinition` — its name,
parameter info, IR class, builder, validator and artifact-filename stem.
The engine looks parts up here instead of importing quadcopter-specific
callables directly, so adding a new part is a registration, not an edit
to engine.py. `quadcopter_frame` is the first registered part.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from src.core.errors import RequestValidationError
from src.engines.cad.builder import build_quadcopter_frame
from src.engines.cad.ir import DEFAULT_QUADCOPTER_PARAMS, PartIR, QuadcopterFrameIR
from src.engines.cad.primitives import Mesh
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
