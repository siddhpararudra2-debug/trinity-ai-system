"""
Enclosure part: open-top rectangular box (floor + four walls).

Built only from `box()` — the five slabs overlap/touch at the corners,
there are no boolean cut-outs, so the interior cavity is the space the
five boxes happen to leave open (this is the V1 local-fallback backend,
not a CAD kernel). Units are mm; the footprint is centred on X/Y and
the outside of the floor sits at z=0.
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

DEFAULT_ENCLOSURE_PARAMS: dict[str, float] = {
    "length": 100,  # mm, outer size along X
    "width": 60,  # mm, outer size along Y
    "height": 40,  # mm, outer size along Z
    "wall_thickness": 2,  # mm, floor and wall slab thickness
}


@dataclass
class EnclosureIR:
    units: str
    parameters: dict[str, float] = field(default_factory=dict)

    @classmethod
    def from_request(cls, raw_parameters: dict[str, Any]) -> EnclosureIR:
        params = dict(DEFAULT_ENCLOSURE_PARAMS)
        for key, value in (raw_parameters or {}).items():
            if key not in params:
                raise RequestValidationError(
                    f"Unknown enclosure parameter '{key}'",
                    details={"allowed": sorted(params)},
                )
            params[key] = float(value)

        from src.engines.cad.parts import check_parameter_bounds

        check_parameter_bounds("enclosure", params)

        wall = params["wall_thickness"]
        if 2 * wall >= min(params["length"], params["width"]):
            raise RequestValidationError(
                "2 x wall_thickness must be less than the smaller of length and width",
                details={
                    "wall_thickness": wall,
                    "length": params["length"],
                    "width": params["width"],
                },
            )
        if wall >= params["height"]:
            raise RequestValidationError(
                "wall_thickness must be less than height",
                details={
                    "wall_thickness": wall,
                    "height": params["height"],
                },
            )

        return cls(units="mm", parameters=params)

    def to_dict(self) -> dict[str, Any]:
        return {
            "type": "enclosure",
            "units": self.units,
            "parameters": self.parameters,
        }


def build_enclosure(ir: EnclosureIR) -> Mesh:
    p = ir.parameters
    length = p["length"]
    width = p["width"]
    height = p["height"]
    wall = p["wall_thickness"]

    # Floor spans the full footprint; the two side slabs run the full
    # length, the two end slabs sit between them, all rising to `height`.
    mesh = box(center=(0, 0, wall / 2), size=(length, width, wall))
    for sign in (1, -1):
        mesh.extend(
            box(
                center=(0, sign * (width / 2 - wall / 2), height / 2),
                size=(length, wall, height),
            )
        )
    for sign in (1, -1):
        mesh.extend(
            box(
                center=(sign * (length / 2 - wall / 2), 0, height / 2),
                size=(wall, width - 2 * wall, height),
            )
        )
    return mesh


def validate_enclosure(
    ir: EnclosureIR, mesh: Mesh
) -> tuple[bool, dict[str, Any]]:
    p = ir.parameters
    checks: dict[str, Any] = {}

    dims_ok = envelope_matches(
        mesh,
        (-p["length"] / 2, -p["width"] / 2, 0.0),
        (p["length"] / 2, p["width"] / 2, p["height"]),
    )
    checks["dimensions_match_envelope"] = dims_ok

    finite_ok, degenerate, triangle_count = mesh_integrity(mesh)
    checks["all_vertices_finite"] = finite_ok
    checks["degenerate_triangle_count"] = degenerate

    manufacturable = p["wall_thickness"] >= MIN_PRINTABLE_FEATURE_MM
    checks["manufacturable_min_feature"] = manufacturable

    checks["triangle_count"] = triangle_count
    ok = dims_ok and finite_ok and degenerate == 0 and manufacturable
    return ok, checks


def enclosure_filename_stem(ir: EnclosureIR) -> str:
    p = ir.parameters
    return (
        f"enclosure_{p['length']:g}x{p['width']:g}x{p['height']:g}mm"
    ).replace(".", "p")
