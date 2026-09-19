"""
CAD validation (PRD §29):

    dimensions | intersections | clearances | topology | manufacturability

V1's local-fallback backend does box geometry only, so "intersections/
topology" reduce to sanity checks (finite coordinates, non-degenerate
triangles) rather than a full boolean-mesh analysis — that level of
rigor is exactly what the CadQuery/FreeCAD/Onshape adapters are for.
"""

from __future__ import annotations

import math
from typing import Any

from src.engines.cad.ir import QuadcopterFrameIR
from src.engines.cad.primitives import Mesh

MIN_PRINTABLE_FEATURE_MM = 1.0  # conservative FDM-printing floor


def validate_quadcopter_frame(
    ir: QuadcopterFrameIR, mesh: Mesh
) -> tuple[bool, dict[str, Any]]:
    p = ir.parameters
    checks: dict[str, Any] = {}
    ok = True

    # --- dimensions: mesh bounding box roughly matches requested envelope ---
    # Motors sit on the X pattern at 45/135/225/315 degrees, so the
    # axis-aligned bounding box span is the diagonal footprint
    # projected onto an axis: sqrt(2) * (radius + boss half-width),
    # not `overall_size` directly (that's the motor-to-motor diagonal).
    (min_c, max_c) = mesh.bounding_box()
    span_x = max_c[0] - min_c[0]
    span_y = max_c[1] - min_c[1]
    boss_side = p["motor_mount_diameter"] * 1.4
    expected = math.sqrt(2) * (p["overall_size"] / 2 + boss_side)
    tolerance = expected * 0.15
    dims_ok = (
        abs(span_x - expected) <= tolerance and abs(span_y - expected) <= tolerance
    )
    checks["dimensions_within_tolerance"] = dims_ok
    checks["expected_span_mm"] = round(expected, 3)
    checks["actual_span_mm"] = (round(span_x, 3), round(span_y, 3))
    ok = ok and dims_ok

    # --- topology / finiteness: no NaN/Inf vertices, no degenerate triangles ---
    finite_ok = True
    degenerate = 0
    for tri in mesh.triangles:
        for v in tri:
            if any(math.isnan(c) or math.isinf(c) for c in v):
                finite_ok = False
        if _triangle_area(tri) < 1e-9:
            degenerate += 1
    checks["all_vertices_finite"] = finite_ok
    checks["degenerate_triangle_count"] = degenerate
    ok = ok and finite_ok and degenerate == 0

    # --- clearances: arm must actually span a positive length ---
    start_r = (p["center_plate_size"] / 2) * math.sqrt(2)
    end_r = p["overall_size"] / 2
    arm_length = end_r - start_r
    clearance_ok = arm_length > 0
    checks["arm_length_mm"] = round(arm_length, 3)
    checks["arm_length_positive"] = clearance_ok
    ok = ok and clearance_ok

    # --- manufacturability: minimum feature size for FDM printing ---
    manufacturable = (
        p["arm_width"] >= MIN_PRINTABLE_FEATURE_MM
        and p["plate_thickness"] >= MIN_PRINTABLE_FEATURE_MM
    )
    checks["manufacturable_min_feature"] = manufacturable
    ok = ok and manufacturable

    checks["triangle_count"] = len(mesh.triangles)
    return ok, checks


def _triangle_area(tri) -> float:
    (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = tri
    ux, uy, uz = x1 - x0, y1 - y0, z1 - z0
    vx, vy, vz = x2 - x0, y2 - y0, z2 - z0
    cx = uy * vz - uz * vy
    cy = uz * vx - ux * vz
    cz = ux * vy - uy * vx
    return 0.5 * math.sqrt(cx * cx + cy * cy + cz * cz)
