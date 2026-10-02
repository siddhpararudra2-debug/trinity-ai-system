"""
CAD part registry (PRD §12, §16).

Each supported part type is described by a `PartDefinition` — its name,
title, description, parameter specs, cross-parameter rules, IR class,
builder, validator and artifact-filename stem. The engine looks parts
up here instead of importing part-specific callables directly, so
adding a new part is a registration, not an edit to engine.py.

The `ParameterInfo` specs are the single machine-readable source of
truth for GET /api/cad/catalog (Phase 3): the absolute bounds declared
here are enforced by `check_parameter_bounds`, which the Phase 2 part
modules call from their `from_request` instead of repeating the
numbers. `quadcopter_frame` keeps its original hardcoded checks — its
specs only mirror them, and the catalog drift-guard tests fail if the
two ever diverge.
"""

from __future__ import annotations

import math
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
    """One catalog parameter: display metadata plus enforced bounds.

    `min` is required — every parameter declares a lower bound (0 with
    `min_exclusive=True` for open-ended sizes). `max` is None when
    unbounded. Cross-parameter conditions do not belong here; they live
    in `PartDefinition.rules` and are enforced by the part's
    `from_request`.
    """

    name: str
    label: str
    unit: str
    default: float | int
    min: float | int
    description: str
    min_exclusive: bool = False
    max: float | int | None = None
    integer: bool = False
    allowed_values: tuple[float | int, ...] | None = None


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
    title: str = ""
    rules: tuple[str, ...] = ()


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


def check_parameter_bounds(part_name: str, params: dict[str, float]) -> None:
    """Raise if any value violates its spec's absolute bounds.

    Single enforcement point for `min`, `min_exclusive`, `max`,
    `integer` and `allowed_values` as declared in the catalog specs.
    The Phase 2 part modules call this from `from_request` instead of
    repeating the numbers; cross-parameter rules stay with the part
    module. Error messages reproduce the original Phase 2 wording
    exactly. Every spec declares a `min`, which is also where
    non-finite values are caught.
    """
    for spec in get_part(part_name).parameters:
        value = params[spec.name]
        if spec.min_exclusive:
            if not math.isfinite(value) or value <= spec.min:
                raise RequestValidationError(
                    f"{spec.name} must be a finite positive value"
                    if spec.min == 0
                    else f"{spec.name} must be greater than {spec.min} {spec.unit}"
                )
        elif not math.isfinite(value) or value < spec.min:
            raise RequestValidationError(
                f"{spec.name} must be a finite value of at "
                f"least {spec.min} {spec.unit}"
            )
        if spec.max is not None and value > spec.max:
            raise RequestValidationError(
                f"{spec.name} must be at most {spec.max} {spec.unit}"
            )
        if spec.integer and not float(value).is_integer():
            raise RequestValidationError(f"{spec.name} must be an integer")
        if spec.allowed_values is not None and value not in spec.allowed_values:
            raise RequestValidationError(
                f"{spec.name} must be one of {list(spec.allowed_values)}",
                details={"allowed": list(spec.allowed_values)},
            )


# ------------------------------------------------------- quadcopter_frame ---


def _quadcopter_filename_stem(ir: QuadcopterFrameIR) -> str:
    return f"quadcopter_frame_{int(ir.parameters['overall_size'])}mm"


_QUADCOPTER_PARAMETERS = (
    ParameterInfo(
        name="overall_size",
        label="Overall size",
        unit="mm",
        default=DEFAULT_QUADCOPTER_PARAMS["overall_size"],
        min=0,
        description="Motor-to-motor diagonal span of the frame.",
        min_exclusive=True,
        max=1000,
    ),
    ParameterInfo(
        name="motor_count",
        label="Motor count",
        unit="count",
        default=DEFAULT_QUADCOPTER_PARAMS["motor_count"],
        min=4,
        description="Number of motors; V1 supports only a four-motor X frame.",
        integer=True,
        allowed_values=(4,),
    ),
    ParameterInfo(
        name="arm_width",
        label="Arm width",
        unit="mm",
        default=DEFAULT_QUADCOPTER_PARAMS["arm_width"],
        min=1,
        description="Width of each arm; at least 1 mm to be printable.",
    ),
    ParameterInfo(
        name="plate_thickness",
        label="Plate thickness",
        unit="mm",
        default=DEFAULT_QUADCOPTER_PARAMS["plate_thickness"],
        min=1,
        description="Thickness of the center plate and arm slabs.",
        max=50,
    ),
    ParameterInfo(
        name="motor_mount_diameter",
        label="Motor mount diameter",
        unit="mm",
        default=DEFAULT_QUADCOPTER_PARAMS["motor_mount_diameter"],
        min=0,
        description="Diameter of the round motor mount at each arm tip.",
        min_exclusive=True,
    ),
    ParameterInfo(
        name="fc_mount_spacing",
        label="Flight controller mount spacing",
        unit="mm",
        default=DEFAULT_QUADCOPTER_PARAMS["fc_mount_spacing"],
        min=0,
        description="Mounting-hole spacing of the flight controller standoffs.",
        min_exclusive=True,
    ),
    ParameterInfo(
        name="center_plate_size",
        label="Center plate size",
        unit="mm",
        default=DEFAULT_QUADCOPTER_PARAMS["center_plate_size"],
        min=0,
        description="Side length of the square center plate.",
        min_exclusive=True,
    ),
)

register_part(
    PartDefinition(
        name="quadcopter_frame",
        title="Quadcopter frame",
        description="X-configuration quadcopter frame with center plate and four arms.",
        parameters=_QUADCOPTER_PARAMETERS,
        ir_class=QuadcopterFrameIR,
        builder=build_quadcopter_frame,
        validator=validate_quadcopter_frame,
        filename_stem=_quadcopter_filename_stem,
        rules=(
            "overall_size must be greater than center_plate_size × 1.4142 "
            "(arms need positive length; with the default plate that means "
            "about 36.8 mm or more)",
            "fc_mount_spacing must be at most center_plate_size",
            "arm_width and motor_mount_diameter must stay small relative to "
            "overall_size or the dimension check fails",
            "motor_count must be 4",
        ),
    )
)


# ------------------------------------------------- Phase 2 part library ---

register_part(
    PartDefinition(
        name="enclosure",
        title="Open-top enclosure",
        description="Open-top rectangular enclosure with a floor and four walls.",
        parameters=(
            ParameterInfo(
                name="length",
                label="Length",
                unit="mm",
                default=DEFAULT_ENCLOSURE_PARAMS["length"],
                min=0,
                description="Outer size along X.",
                min_exclusive=True,
                max=1000,
            ),
            ParameterInfo(
                name="width",
                label="Width",
                unit="mm",
                default=DEFAULT_ENCLOSURE_PARAMS["width"],
                min=0,
                description="Outer size along Y.",
                min_exclusive=True,
                max=1000,
            ),
            ParameterInfo(
                name="height",
                label="Height",
                unit="mm",
                default=DEFAULT_ENCLOSURE_PARAMS["height"],
                min=0,
                description="Outer size along Z.",
                min_exclusive=True,
                max=1000,
            ),
            ParameterInfo(
                name="wall_thickness",
                label="Wall thickness",
                unit="mm",
                default=DEFAULT_ENCLOSURE_PARAMS["wall_thickness"],
                min=1,
                description="Thickness of the floor and all four walls.",
                max=50,
            ),
        ),
        ir_class=EnclosureIR,
        builder=build_enclosure,
        validator=validate_enclosure,
        filename_stem=enclosure_filename_stem,
        rules=(
            "2 × wall_thickness must be less than the smaller of length and width",
            "wall_thickness must be less than height",
        ),
    )
)

register_part(
    PartDefinition(
        name="l_bracket",
        title="L-bracket",
        description="Right-angle bracket: a flat base with one upright wall.",
        parameters=(
            ParameterInfo(
                name="base_length",
                label="Base length",
                unit="mm",
                default=DEFAULT_L_BRACKET_PARAMS["base_length"],
                min=0,
                description="Length of the horizontal base along X.",
                min_exclusive=True,
                max=1000,
            ),
            ParameterInfo(
                name="height",
                label="Height",
                unit="mm",
                default=DEFAULT_L_BRACKET_PARAMS["height"],
                min=0,
                description="Total height of the upright wall along Z.",
                min_exclusive=True,
                max=1000,
            ),
            ParameterInfo(
                name="width",
                label="Width",
                unit="mm",
                default=DEFAULT_L_BRACKET_PARAMS["width"],
                min=0,
                description="Outer size along Y.",
                min_exclusive=True,
                max=1000,
            ),
            ParameterInfo(
                name="thickness",
                label="Thickness",
                unit="mm",
                default=DEFAULT_L_BRACKET_PARAMS["thickness"],
                min=1,
                description="Slab thickness of both the base and the upright.",
                max=50,
            ),
        ),
        ir_class=LBracketIR,
        builder=build_l_bracket,
        validator=validate_l_bracket,
        filename_stem=l_bracket_filename_stem,
        rules=(
            "thickness must be less than base_length",
            "thickness must be less than height",
        ),
    )
)

register_part(
    PartDefinition(
        name="mounting_plate",
        title="Mounting plate",
        description="Flat plate with four raised cylindrical mounting bosses.",
        parameters=(
            ParameterInfo(
                name="length",
                label="Length",
                unit="mm",
                default=DEFAULT_MOUNTING_PLATE_PARAMS["length"],
                min=0,
                description="Outer size along X.",
                min_exclusive=True,
                max=1000,
            ),
            ParameterInfo(
                name="width",
                label="Width",
                unit="mm",
                default=DEFAULT_MOUNTING_PLATE_PARAMS["width"],
                min=0,
                description="Outer size along Y.",
                min_exclusive=True,
                max=1000,
            ),
            ParameterInfo(
                name="thickness",
                label="Thickness",
                unit="mm",
                default=DEFAULT_MOUNTING_PLATE_PARAMS["thickness"],
                min=1,
                description="Thickness of the base plate.",
                max=50,
            ),
            ParameterInfo(
                name="boss_diameter",
                label="Boss diameter",
                unit="mm",
                default=DEFAULT_MOUNTING_PLATE_PARAMS["boss_diameter"],
                min=1,
                description="Diameter of each raised cylindrical boss.",
                max=1000,
            ),
            ParameterInfo(
                name="boss_height",
                label="Boss height",
                unit="mm",
                default=DEFAULT_MOUNTING_PLATE_PARAMS["boss_height"],
                min=1,
                description="Height of each boss above the plate surface.",
                max=50,
            ),
            ParameterInfo(
                name="boss_inset",
                label="Boss inset",
                unit="mm",
                default=DEFAULT_MOUNTING_PLATE_PARAMS["boss_inset"],
                min=0,
                description="Distance from each plate edge to the boss centre.",
                min_exclusive=True,
                max=1000,
            ),
        ),
        ir_class=MountingPlateIR,
        builder=build_mounting_plate,
        validator=validate_mounting_plate,
        filename_stem=mounting_plate_filename_stem,
        rules=(
            "boss_inset must be at least boss_diameter / 2 so each boss stays "
            "within its plate edge",
            "boss_diameter / 2 + boss_inset must not exceed the smaller of "
            "length and width",
        ),
    )
)

register_part(
    PartDefinition(
        name="standoff",
        title="Standoff",
        description="Cylindrical post on a wider foot for spacing boards apart.",
        parameters=(
            ParameterInfo(
                name="height",
                label="Height",
                unit="mm",
                default=DEFAULT_STANDOFF_PARAMS["height"],
                min=0,
                description="Overall height of the standoff.",
                min_exclusive=True,
                max=1000,
            ),
            ParameterInfo(
                name="diameter",
                label="Diameter",
                unit="mm",
                default=DEFAULT_STANDOFF_PARAMS["diameter"],
                min=1,
                description="Diameter of the main cylindrical shaft.",
                max=1000,
            ),
            ParameterInfo(
                name="foot_diameter",
                label="Foot diameter",
                unit="mm",
                default=DEFAULT_STANDOFF_PARAMS["foot_diameter"],
                min=1,
                description="Diameter of the wider foot at the base.",
                max=1000,
            ),
            ParameterInfo(
                name="foot_height",
                label="Foot height",
                unit="mm",
                default=DEFAULT_STANDOFF_PARAMS["foot_height"],
                min=1,
                description="Height of the foot above the base.",
                max=1000,
            ),
        ),
        ir_class=StandoffIR,
        builder=build_standoff,
        validator=validate_standoff,
        filename_stem=standoff_filename_stem,
        rules=("foot_height must not exceed height",),
    )
)
