"""Engineering drawings (M14) - projected views of the tessellated solid.

V1 truth model, stated plainly: a drawing here is NOT a hidden-line-removed
manufacturing print. Each view projects the mesh's full unique wireframe
(every triangle edge, deduplicated, zero-length edges dropped) onto a named
plane:

    top   -> XY plane (u=x, v=y)
    front -> XZ plane (u=x, v=v... u=x, v=z)
    side  -> YZ plane (u=y, v=z)
    iso   -> u=(x-y)/sqrt(2), v=(2z-x-y)/sqrt(6)  (true isometric: the
             three axes separate by 120 degrees on screen, +Z up)

Views are laid out as separate sheets (framed + labeled tiles, 1:1 scale)
in one DXF, on per-view LAYERs. The document self-verifies: after
rendering, the emitted text is parsed back and per-layer entity counts
must match the projection counts exactly - a serialization drop raises
instead of shipping a drawing that lies.
"""

from __future__ import annotations

import math
from collections.abc import Iterable
from typing import Any

from src.core.errors import GeometryValidationError
from src.engines.cad.primitives import Mesh, Vec3

# Canonical view order (request order never leaks into the document).
VIEW_ORDER: tuple[str, ...] = ("top", "front", "side", "iso")

# Plane mapping is data, not magic, so the document can name what it drew.
VIEW_PLANES: dict[str, tuple[str, str]] = {
    "top": ("XY", "u=x_mm, v=y_mm"),
    "front": ("XZ", "u=x_mm, v=z_mm"),
    "side": ("YZ", "u=y_mm, v=z_mm"),
    "iso": ("isometric", "u=(x-y)/sqrt(2), v=(2z-x-y)/sqrt(6)"),
}

# Sheet dressing (mm): frames, labels, gaps.
MARGIN_MM = 10.0
LABEL_GAP_MM = 4.0
LABEL_HEIGHT_MM = 6.0
SHEET_GAP_MM = 30.0

LAYER_FRAMES = "SHEET_FRAMES"
LAYER_LABELS = "SHEET_LABELS"

# DXF layer color codes (7 = white/black, others distinct).
_LAYER_COLORS: dict[str, int] = {
    "TOP": 1,
    "FRONT": 3,
    "SIDE": 4,
    "ISO": 5,
    LAYER_FRAMES: 8,
    LAYER_LABELS: 2,
}


def project_point(view: str, p: Vec3) -> tuple[float, float]:
    """Project one 3D vertex to its 2D view coordinates. Public for tests."""
    x, y, z = p
    if view == "top":
        return (x, y)
    if view == "front":
        return (x, z)
    if view == "side":
        return (y, z)
    if view == "iso":
        return ((x - y) / math.sqrt(2.0), (2.0 * z - x - y) / math.sqrt(6.0))
    raise GeometryValidationError(
        f"unknown drawing view '{view}'",
        details={"supported_views": list(VIEW_ORDER)},
    )


def _mesh_edges(mesh: Mesh) -> Iterable[tuple[Vec3, Vec3]]:
    """Every unique triangle edge, canonicalized so shared edges yield once."""
    seen: set[tuple[tuple[float, ...], tuple[float, ...]]] = set()
    for triangle in mesh.triangles:
        for i in range(3):
            a, b = triangle[i], triangle[(i + 1) % 3]
            key = (tuple(a), tuple(b)) if a <= b else (tuple(b), tuple(a))
            if key not in seen:
                seen.add(key)
                yield a, b


def projected_unique_edges(
    mesh: Mesh, view: str
) -> list[tuple[tuple[float, float], tuple[float, float]]]:
    """Project every mesh edge onto `view`'s plane, deduplicated.

    Zero-length projections (edges that run exactly ALONG the view axis)
    are dropped - in a projection they are points, not strokes. Counts are
    deterministic: rounding to 1e-6 mm means the same mesh always yields
    the same edge set.
    """
    unique: set[tuple[tuple[float, float], tuple[float, float]]] = set()
    for a, b in _mesh_edges(mesh):
        pa, pb = project_point(view, a), project_point(view, b)
        ka = (round(pa[0], 6), round(pa[1], 6))
        kb = (round(pb[0], 6), round(pb[1], 6))
        if ka == kb:
            continue
        unique.add((ka, kb) if ka <= kb else (kb, ka))
    return sorted(unique)


def _bbox(points: Iterable[tuple[float, float]]) -> tuple[float, float, float, float]:
    us = []
    vs = []
    for u, v in points:
        us.append(u)
        vs.append(v)
    return (min(us), min(vs), max(us), max(vs))


def _sheet_frames(ox: float, oy: float, w: float, h: float) -> list[tuple[tuple[float, float], tuple[float, float]]]:
    x1, y1 = ox + w, oy + h
    return [
        ((ox, oy), (x1, oy)),
        ((x1, oy), (x1, y1)),
        ((x1, y1), (ox, y1)),
        ((ox, y1), (ox, oy)),
    ]


def build_sheets(mesh: Mesh, views: list[str]) -> tuple[list[dict[str, Any]], list[str]]:
    """Lay out the requested views as tiled sheets; return report + strokes.

    Returns (per-view report rows, flat stroke list) where each stroke is
    (layer, (u1, v1), (u2, v2)) in final sheet coordinates. Deterministic:
    canonical view order, stable sheet x-order.
    """
    known = set(VIEW_ORDER)
    unknown = [v for v in views if v not in known]
    if unknown or not views:
        raise GeometryValidationError(
            "unknown or empty drawing view list",
            details={"unknown": sorted(set(unknown)), "supported_views": list(VIEW_ORDER)},
        )

    report: list[dict[str, Any]] = []
    strokes: list[tuple[str, tuple[float, float], tuple[float, float]]] = []
    x_cursor = 0.0

    for view in VIEW_ORDER:
        if view not in views:
            continue
        edges = projected_unique_edges(mesh, view)
        if not edges:
            raise GeometryValidationError(
                f"view '{view}' projected no geometry - refusing an empty sheet"
            )
        (umin, vmin, umax, vmax) = _bbox([p for e in edges for p in e])
        tile_w, tile_h = umax - umin, vmax - vmin

        ox = x_cursor
        oy = 0.0
        dx, dy = ox + MARGIN_MM - umin, oy + MARGIN_MM - vmin

        layer = view.upper()
        for (a, b) in edges:
            strokes.append((layer, (a[0] + dx, a[1] + dy), (b[0] + dx, b[1] + dy)))
        for (a, b) in _sheet_frames(ox, oy, tile_w + 2 * MARGIN_MM, tile_h + 2 * MARGIN_MM):
            strokes.append((LAYER_FRAMES, a, b))

        plane, definition = VIEW_PLANES[view]
        report.append(
            {
                "name": view,
                "layer": layer,
                "plane": plane,
                "projection": definition,
                "line_count": len(edges),
                "view_bbox_mm": [round(v, 6) for v in (umin, vmin, umax, vmax)],
                "sheet_origin_mm": [round(ox, 6), round(oy, 6)],
                "sheet_size_mm": [
                    round(tile_w + 2 * MARGIN_MM, 6),
                    round(tile_h + 2 * MARGIN_MM, 6),
                ],
            }
        )
        x_cursor += tile_w + 2 * MARGIN_MM + SHEET_GAP_MM
    return report, strokes


def _labels(views: list[str]) -> list[str]:
    return [f"{v.upper()} ({VIEW_PLANES[v][0]})" for v in VIEW_ORDER if v in views]


def render_dxf(views: list[str], report: list[dict[str, Any]], strokes) -> str:
    """Emit the sheet set as one deterministic minimal DXF (R12-style)."""
    layer_names = [r["layer"] for r in report] + [LAYER_FRAMES, LAYER_LABELS]

    out: list[str] = []
    # HEADER: mm units for readers that honor it.
    out += ["0", "SECTION", "2", "HEADER", "9", "$INSUNITS", "70", "4", "0", "ENDSEC"]
    # TABLES: one LAYER per sheet plus the dressing layers.
    out += ["0", "SECTION", "2", "TABLES", "0", "TABLE", "2", "LAYER", "70", str(len(layer_names))]
    for name in layer_names:
        out += ["0", "LAYER", "2", name, "70", "0", "62", str(_LAYER_COLORS[name]), "6", "CONTINUOUS"]
    out += ["0", "ENDTAB", "0", "ENDSEC"]
    # ENTITIES. Text coordinates always carry 6 decimals.
    out += ["0", "SECTION", "2", "ENTITIES"]
    for layer, a, b in strokes:
        out += [
            "0", "LINE", "8", layer,
            "10", f"{a[0]:.6f}", "20", f"{a[1]:.6f}", "30", "0.000000",
            "11", f"{b[0]:.6f}", "21", f"{b[1]:.6f}", "31", "0.000000",
        ]
    x_cursor = 0.0
    for label, row in zip(_labels(views), report):
        x_cursor = row["sheet_origin_mm"][0]
        label_y = row["sheet_origin_mm"][1] + row["sheet_size_mm"][1] + LABEL_GAP_MM
        out += [
            "0", "TEXT", "8", LAYER_LABELS,
            "10", f"{x_cursor:.6f}", "20", f"{label_y:.6f}", "30", "0.000000",
            "40", f"{LABEL_HEIGHT_MM:.6f}", "1", label,
        ]
    out += ["0", "ENDSEC", "0", "EOF"]
    return "\n".join(out) + "\n"


def _count_entities(text: str, kind: str, layer: str) -> int:
    """Count `kind` entities on `layer` by parsing the DXF back.

    Independent of the writer's loop: tokens are walked pairwise so a
    half-written entity cannot count itself.
    """
    lines = text.split("\n")
    count = 0
    i = 0
    while i < len(lines):
        if lines[i] == kind:
            # scan forward for this entity's layer code (8)
            j = i + 1
            while j < len(lines) and lines[j] not in ("0", "EOF"):
                if lines[j] == "8" and j + 1 < len(lines) and lines[j + 1] == layer:
                    count += 1
                    break
                j += 1
        i += 1
    return count


def _verify_dxf(text: str, report: list[dict[str, Any]]) -> dict[str, Any]:
    """Recount emitted entities and refuse a document that lost strokes."""
    counts: dict[str, int] = {}
    for row in report:
        got = _count_entities(text, "LINE", row["layer"])
        if got != row["line_count"]:
            raise GeometryValidationError(
                "drawing self-check failed: projected strokes do not all appear in the DXF",
                details={"view": row["name"], "expected": row["line_count"], "found": got},
            )
        counts[row["layer"]] = got
    frames = _count_entities(text, "LINE", LAYER_FRAMES)
    if frames != 4 * len(report):
        raise GeometryValidationError(
            "drawing self-check failed: sheet frames incomplete",
            details={"expected": 4 * len(report), "found": frames},
        )
    labels = _count_entities(text, "TEXT", LAYER_LABELS)
    if labels != len(report):
        raise GeometryValidationError(
            "drawing self-check failed: sheet labels incomplete",
            details={"expected": len(report), "found": labels},
        )
    return {
        "line_counts_by_layer": counts,
        "frame_count": frames,
        "label_count": labels,
    }


def build_drawing_document(
    mesh: Mesh, views: list[str] | None = None
) -> tuple[list[dict[str, Any]], str, dict[str, int]]:
    """Full pipeline: report, DXF text, and the verified entity counts.

    Raises GeometryValidationError for an empty mesh (a solid with no
    edges cannot produce a truthful drawing) or when the rendered text
    fails the recount gate.
    """
    if not mesh.triangles:
        raise GeometryValidationError("cannot draw an empty mesh")
    selected = list(views or list(VIEW_ORDER))
    report, strokes = build_sheets(mesh, selected)
    text = render_dxf(selected, report, strokes)  # type: ignore[arg-type]
    counts = _verify_dxf(text, report)
    return report, text, counts
