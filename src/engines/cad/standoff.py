"""
Standoff part: cylindrical post standing on a wider foot.

Built from two overlapping `cylinder()` solids (no boolean union — V1
local-fallback backend). Units are mm; both cylinders are centred on
X/Y, the foot sits on z=0 and the post rises to `height`. The cross-
field rule keeps the foot no taller than the post so the overall part
height is exactly `height`.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any

from src.core.errors import RequestValidationError
from src.engines.cad.primitives import Mesh, cylinder
from src.engines.cad.validators import (
    MIN_PRINTABLE_FEATURE_MM,
    envelope_matches,
    mesh_integrity,
)

DEFAULT_STANDOFF_PARAMS: dict[str, float] = {
    "height": 10,  # mm, overall height (post reaches z=height)
    "diameter": 6,  # mm, post diameter
    "foot_diameter": 10,  # mm, foot diameter
    "foot_height": 1.5,  # mm, foot height above z=0
}


@dataclass
class StandoffIR:
    units: str
    parameters: dict[str, float] = field(default_factory=dict)

    @classmethod
    def from_request(cls, raw_parameters: dict[str, Any]) -> StandoffIR:
        params = dict(DEFAULT_STANDOFF_PARAMS)
        for key, value in (raw_parameters or {}).items():
            if key not in params:
                raise RequestValidationError(
                    f"Unknown standoff parameter '{key}'",
                    details={"allowed": sorted(params)},
                )
            params[key] = float(value)

        height = params["height"]
        if not math.isfinite(height) or height <= 0:
            raise RequestValidationError("height must be a finite positive value")
        if height > 1000:
            raise RequestValidationError("height must be at most 1000 mm")

        for key in ("diameter", "foot_diameter", "foot_height"):
            value = params[key]
            if not math.isfinite(value) or value < MIN_PRINTABLE_FEATURE_MM:
                raise RequestValidationError(
                    f"{key} must be a finite value of at least 1 mm"
                )
            if value > 1000:
                raise RequestValidationError(f"{key} must be at most 1000 mm")

        if params["foot_height"] > height:
            raise RequestValidationError(
                "foot_height must not exceed height",
                details={"foot_height": params["foot_height"], "height": height},
            )

        return cls(units="mm", parameters=params)

    def to_dict(self) -> dict[str, Any]:
        return {
            "type": "standoff",
            "units": self.units,
            "parameters": self.parameters,
        }


def build_standoff(ir: StandoffIR) -> Mesh:
    p = ir.parameters
    mesh = cylinder(
        center=(0, 0, p["height"] / 2),
        radius=p["diameter"] / 2,
        height=p["height"],
    )
    mesh.extend(
        cylinder(
            center=(0, 0, p["foot_height"] / 2),
            radius=p["foot_diameter"] / 2,
            height=p["foot_height"],
        )
    )
    return mesh


def validate_standoff(ir: StandoffIR, mesh: Mesh) -> tuple[bool, dict[str, Any]]:
    p = ir.parameters
    checks: dict[str, Any] = {}

    half_span = max(p["diameter"], p["foot_diameter"]) / 2
    dims_ok = envelope_matches(
        mesh,
        (-half_span, -half_span, 0.0),
        (half_span, half_span, p["height"]),
    )
    checks["dimensions_match_envelope"] = dims_ok

    finite_ok, degenerate, triangle_count = mesh_integrity(mesh)
    checks["all_vertices_finite"] = finite_ok
    checks["degenerate_triangle_count"] = degenerate

    manufacturable = (
        p["diameter"] >= MIN_PRINTABLE_FEATURE_MM
        and p["foot_diameter"] >= MIN_PRINTABLE_FEATURE_MM
        and p["foot_height"] >= MIN_PRINTABLE_FEATURE_MM
    )
    checks["manufacturable_min_feature"] = manufacturable

    checks["triangle_count"] = triangle_count
    ok = dims_ok and finite_ok and degenerate == 0 and manufacturable
    return ok, checks


def standoff_filename_stem(ir: StandoffIR) -> str:
    return f"standoff_{ir.parameters['height']:g}mm".replace(".", "p")
