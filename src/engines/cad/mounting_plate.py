"""
Mounting plate part: flat rectangular plate with four raised bosses.

Built from `box()` (the plate) plus four `cylinder()` bosses standing on
its top face — overlapping solids, no boolean union, which is the whole
story of the V1 local-fallback backend. Units are mm; the footprint is
centred on X/Y with the plate underside at z=0. `boss_inset` is the
distance from each plate edge to the boss centre, and the cross-field
rule keeps every boss entirely on the plate.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from src.core.errors import RequestValidationError
from src.engines.cad.primitives import Mesh, box, cylinder
from src.engines.cad.validators import (
    MIN_PRINTABLE_FEATURE_MM,
    envelope_matches,
    mesh_integrity,
)

DEFAULT_MOUNTING_PLATE_PARAMS: dict[str, float] = {
    "length": 100,  # mm, extent along X
    "width": 60,  # mm, extent along Y
    "thickness": 3,  # mm, plate slab thickness
    "boss_diameter": 8,  # mm, boss cylinder diameter
    "boss_height": 2,  # mm, boss height above the plate
    "boss_inset": 8,  # mm, plate edge to boss centre
}


@dataclass
class MountingPlateIR:
    units: str
    parameters: dict[str, float] = field(default_factory=dict)

    @classmethod
    def from_request(cls, raw_parameters: dict[str, Any]) -> MountingPlateIR:
        params = dict(DEFAULT_MOUNTING_PLATE_PARAMS)
        for key, value in (raw_parameters or {}).items():
            if key not in params:
                raise RequestValidationError(
                    f"Unknown mounting_plate parameter '{key}'",
                    details={"allowed": sorted(params)},
                )
            params[key] = float(value)

        from src.engines.cad.parts import check_parameter_bounds

        check_parameter_bounds("mounting_plate", params)

        boss_diameter = params["boss_diameter"]
        boss_inset = params["boss_inset"]
        if boss_inset < boss_diameter / 2:
            raise RequestValidationError(
                "boss_inset must be at least boss_diameter / 2 so each boss "
                "stays within its plate edge",
                details={"boss_diameter": boss_diameter, "boss_inset": boss_inset},
            )
        if boss_diameter / 2 + boss_inset > min(params["length"], params["width"]):
            raise RequestValidationError(
                "boss_diameter / 2 + boss_inset must not exceed the smaller "
                "of length and width",
                details={
                    "boss_diameter": boss_diameter,
                    "boss_inset": boss_inset,
                    "length": params["length"],
                    "width": params["width"],
                },
            )

        return cls(units="mm", parameters=params)

    def to_dict(self) -> dict[str, Any]:
        return {
            "type": "mounting_plate",
            "units": self.units,
            "parameters": self.parameters,
        }


def build_mounting_plate(ir: MountingPlateIR) -> Mesh:
    p = ir.parameters
    length = p["length"]
    width = p["width"]
    thickness = p["thickness"]

    mesh = box(center=(0, 0, thickness / 2), size=(length, width, thickness))
    for sign_x in (1, -1):
        for sign_y in (1, -1):
            mesh.extend(
                cylinder(
                    center=(
                        sign_x * (length / 2 - p["boss_inset"]),
                        sign_y * (width / 2 - p["boss_inset"]),
                        thickness + p["boss_height"] / 2,
                    ),
                    radius=p["boss_diameter"] / 2,
                    height=p["boss_height"],
                )
            )
    return mesh


def validate_mounting_plate(
    ir: MountingPlateIR, mesh: Mesh
) -> tuple[bool, dict[str, Any]]:
    p = ir.parameters
    checks: dict[str, Any] = {}

    dims_ok = envelope_matches(
        mesh,
        (-p["length"] / 2, -p["width"] / 2, 0.0),
        (
            p["length"] / 2,
            p["width"] / 2,
            p["thickness"] + p["boss_height"],
        ),
    )
    checks["dimensions_match_envelope"] = dims_ok

    finite_ok, degenerate, triangle_count = mesh_integrity(mesh)
    checks["all_vertices_finite"] = finite_ok
    checks["degenerate_triangle_count"] = degenerate

    manufacturable = (
        p["thickness"] >= MIN_PRINTABLE_FEATURE_MM
        and p["boss_height"] >= MIN_PRINTABLE_FEATURE_MM
    )
    checks["manufacturable_min_feature"] = manufacturable

    checks["triangle_count"] = triangle_count
    ok = dims_ok and finite_ok and degenerate == 0 and manufacturable
    return ok, checks


def mounting_plate_filename_stem(ir: MountingPlateIR) -> str:
    p = ir.parameters
    return f"mounting_plate_{p['length']:g}x{p['width']:g}mm".replace(".", "p")
