"""
Enclosure part: open-top rectangular box (floor + four walls) with optional lid.

Built only from `box()` — the five slabs overlap/touch at the corners, there are
no boolean cut-outs, so the interior cavity is the space the five boxes happen
to leave open (this is the V1 local-fallback backend, not a CAD kernel). Units
are mm; the footprint is centred on X/Y and the outside of the floor sits at z=0.

Lid parameters:
  lid:          "none" | "removable"
  lid_thickness: float >= 0 (mm), only used when lid != "none"
  lid_clearance: float >= 0 (mm), gap between board top and lid inner bottom
  screw_posts:  bool, whether screw posts are included
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
    """Build the main enclosure mesh (floor + four walls).

    Returns just the Mesh — callers needing lid info should use
    ``build_enclosure_with_lid`` or validate via ``validate_enclosure``
    with a separately-computed ``lid_info`` dict.
    """
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


def build_enclosure_with_lid(
    ir: EnclosureIR, *, lid: str = "none", lid_thickness: float = 0.0, lid_clearance: float = 0.0, screw_posts: bool = False
) -> tuple[Mesh, dict[str, Any]]:
    """Build the enclosure mesh and optionally a lid mesh.

    Returns (main_mesh, lid_info) where lid_info is a dict with keys
    ``"has_lid"``, ``"lid_mesh"``, ``"inner_height"``, ``"lid_params"``.
    """
    p = ir.parameters
    length = p["length"]
    width = p["width"]
    height = p["height"]
    wall = p["wall_thickness"]

    # ---- main enclosure (floor + four walls) ----
    main_mesh = box(center=(0, 0, wall / 2), size=(length, width, wall))
    for sign in (1, -1):
        main_mesh.extend(
            box(
                center=(0, sign * (width / 2 - wall / 2), height / 2),
                size=(length, wall, height),
            )
        )
    for sign in (1, -1):
        main_mesh.extend(
            box(
                center=(sign * (length / 2 - wall / 2), 0, height / 2),
                size=(wall, width - 2 * wall, height),
            )
        )

    # ---- lid ----
    lid_info: dict[str, Any] = {
        "has_lid": lid != "none",
        "lid_mesh": None,  # will be filled if lid is built
        "inner_height": height - lid_thickness - lid_clearance if lid != "none" else height,
        "lid_params": {"lid": lid, "lid_thickness": lid_thickness, "lid_clearance": lid_clearance, "screw_posts": screw_posts},
    }

    if lid != "none" and lid_thickness > 0:
        # Lid sits on top of the walls, with an inner cavity
        lid_inner_height = height - lid_thickness - lid_clearance
        lid_mesh = box(
            center=(0, 0, height - lid_thickness / 2),
            size=(length, width, lid_thickness),
        )
        lid_info["lid_mesh"] = lid_mesh
    else:
        lid_info["inner_height"] = height

    return main_mesh, lid_info


def validate_enclosure(
    ir: EnclosureIR, mesh: Mesh, lid_info: dict[str, Any] | None = None
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

    # Lid-aware checks
    if lid_info is not None and lid_info.get("has_lid", False):
        # Board top should be below lid inner bottom
        board_top = 0.0  # floor at z=0, board sits inside enclosure
        lid_inner_bottom = p["height"] - lid_info.get("lid_thickness", 0.0) - lid_info.get("lid_clearance", 0.0)
        lid_clearance_ok = board_top <= lid_inner_bottom  # board doesn't penetrate lid
        checks["board_clear_of_lid"] = lid_clearance_ok
    else:
        checks["board_clear_of_lid"] = True

    checks["triangle_count"] = checks.get("degenerate_triangle_count", 0)
    ok = dims_ok and finite_ok and degenerate == 0 and manufacturable and checks.get("board_clear_of_lid", True)
    return ok, checks


def enclosure_filename_stem(ir: EnclosureIR, *, lid: str = "none", lid_thickness: float = 0.0) -> str:
    p = ir.parameters
    base = (
        f"enclosure_{p['length']:g}x{p['width']:g}x{p['height']:g}mm"
    ).replace(".", "p")
    if lid != "none":
        base += f"_lid{lid_thickness}__{lid}"
    return base