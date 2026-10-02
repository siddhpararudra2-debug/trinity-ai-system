"""
L-bracket part: flat base with one upright wall along an end.

Built only from two `box()` slabs that overlap at the corner (no boolean
union, this is the V1 local-fallback backend). Units are mm; the base
is centred on X/Y with its underside at z=0 and the upright rises from
the -X end.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from src.core.errors import RequestValidationError
from src.engines.cad.primitives import Mesh, box
from src.engines.cad.validators import (
    MIN_PRINTABLE_FEATURE_MM,
    envelope_matches,
    mesh_integrity,
)

DEFAULT_L_BRACKET_PARAMS: dict[str, float] = {
    "base_length": 40,  # mm, base extent along X
    "height": 40,  # mm, upright extent along Z
    "width": 20,  # mm, extent along Y
    "thickness": 3,  # mm, slab thickness of base and upright
}


@dataclass
class LBracketIR:
    units: str
    parameters: dict[str, float] = field(default_factory=dict)

    @classmethod
    def from_request(cls, raw_parameters: dict[str, Any]) -> LBracketIR:
        params = dict(DEFAULT_L_BRACKET_PARAMS)
        for key, value in (raw_parameters or {}).items():
            if key not in params:
                raise RequestValidationError(
                    f"Unknown l_bracket parameter '{key}'",
                    details={"allowed": sorted(params)},
                )
            params[key] = float(value)

        from src.engines.cad.parts import check_parameter_bounds

        check_parameter_bounds("l_bracket", params)

        thickness = params["thickness"]
        if thickness >= params["base_length"]:
            raise RequestValidationError(
                "thickness must be less than base_length",
                details={
                    "thickness": thickness,
                    "base_length": params["base_length"],
                },
            )
        if thickness >= params["height"]:
            raise RequestValidationError(
                "thickness must be less than height",
                details={
                    "thickness": thickness,
                    "height": params["height"],
                },
            )

        return cls(units="mm", parameters=params)

    def to_dict(self) -> dict[str, Any]:
        return {
            "type": "l_bracket",
            "units": self.units,
            "parameters": self.parameters,
        }


def build_l_bracket(ir: LBracketIR) -> Mesh:
    p = ir.parameters
    base_length = p["base_length"]
    height = p["height"]
    width = p["width"]
    thickness = p["thickness"]

    mesh = box(center=(0, 0, thickness / 2), size=(base_length, width, thickness))
    mesh.extend(
        box(
            center=(-base_length / 2 + thickness / 2, 0, height / 2),
            size=(thickness, width, height),
        )
    )
    return mesh


def validate_l_bracket(
    ir: LBracketIR, mesh: Mesh
) -> tuple[bool, dict[str, Any]]:
    p = ir.parameters
    checks: dict[str, Any] = {}

    dims_ok = envelope_matches(
        mesh,
        (-p["base_length"] / 2, -p["width"] / 2, 0.0),
        (p["base_length"] / 2, p["width"] / 2, p["height"]),
    )
    checks["dimensions_match_envelope"] = dims_ok

    finite_ok, degenerate, triangle_count = mesh_integrity(mesh)
    checks["all_vertices_finite"] = finite_ok
    checks["degenerate_triangle_count"] = degenerate

    manufacturable = p["thickness"] >= MIN_PRINTABLE_FEATURE_MM
    checks["manufacturable_min_feature"] = manufacturable

    checks["triangle_count"] = triangle_count
    ok = dims_ok and finite_ok and degenerate == 0 and manufacturable
    return ok, checks


def l_bracket_filename_stem(ir: LBracketIR) -> str:
    p = ir.parameters
    return (
        f"l_bracket_{p['base_length']:g}x{p['height']:g}x{p['width']:g}mm"
    ).replace(".", "p")
