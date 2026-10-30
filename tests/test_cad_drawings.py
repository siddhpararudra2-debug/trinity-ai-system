"""Drawings tests (M14): projected multi-sheet DXF views with pinned
geometry counts. Pure projection truth first, then the engine op and the
route. Counts below were verified against the wireframe enumeration of
the box primitive: 18 unique mesh edges (4+4 ring edges, 2 face
diagonals, 4 verticals, 4 side-face diagonals); in top/front/side views
verticals project to points (dropped) and side-face diagonals collapse
onto outer edges, leaving 6 strokes each."""

from __future__ import annotations

import math

import pytest

from src.core.errors import GeometryValidationError, RequestValidationError
from src.engines.cad.drawings import (
    MARGIN_MM,
    VIEW_ORDER,
    build_drawing_document,
    projected_unique_edges,
    project_point,
)
from src.engines.cad.engine import CADEngine
from src.engines.cad.primitives import box


def _text_box() -> object:
    return box((0.0, 0.0, 10.0), (40.0, 20.0, 10.0))


def test_plan_view_counts_are_pinned_wireframe_truth():
    m = box((0.0, 0.0, 10.0), (40.0, 20.0, 10.0))
    # verticals drop, side-face diagonals collapse onto outer edges
    for view, expected in (("top", 6), ("front", 6), ("side", 6)):
        edges = projected_unique_edges(m, view)
        assert len(edges) == expected, (view, len(edges))
        # every surviving stroke carries real length (no zero-length lines)
        for (a, b) in edges:
            assert a != b


def test_iso_projection_math_is_true_isometric():
    # unit axes: X and Y at cos(30) above the down-horizontal, Z straight up
    ex = project_point("iso", (1.0, 0.0, 0.0))
    ey = project_point("iso", (0.0, 1.0, 0.0))
    ez = project_point("iso", (0.0, 0.0, 1.0))
    assert ex == pytest.approx((math.sqrt(2) / 2, -math.sqrt(6) / 6))
    assert ey == pytest.approx((-math.sqrt(2) / 2, -math.sqrt(6) / 6))
    assert ez == pytest.approx((0.0, math.sqrt(6) / 3))
    # pairwise 120 degrees (dot of foreshortened unit axes = -1/2)
    def _dot(a, b):
        return (a[0] * b[0] + a[1] * b[1]) / (
            math.hypot(*a) * math.hypot(*b)
        )

    for p, q in ((ex, ey), (ex, ez), (ey, ez)):
        assert _dot(p, q) == pytest.approx(-0.5)
    # +Z stays up
    assert ez[1] > 0


def test_iso_carries_the_full_wireframe():
    m = box((0.0, 0.0, 10.0), (40.0, 20.0, 10.0))
    edges = projected_unique_edges(m, "iso")
    # none of the 18 unique mesh edges degenerate or merge in isometric
    assert len(edges) == 18
    assert len({e for e in edges}) == 18


def test_unknown_view_refuses_with_supported_list():
    with pytest.raises(GeometryValidationError, match="drawing view"):
        projected_unique_edges(_text_box(), "perspective")  # type: ignore[arg-type]
    with pytest.raises(GeometryValidationError, match="view list"):
        build_drawing_document(_text_box(), ["top", "bogus"])


def test_document_report_counts_match_projection_counts():
    m = box((0.0, 0.0, 10.0), (40.0, 20.0, 10.0))
    report, text, counts = build_drawing_document(m, list(VIEW_ORDER))
    assert [r["name"] for r in report] == ["top", "front", "side", "iso"]
    for row in report:
        assert counts["line_counts_by_layer"][row["layer"]] == row["line_count"]
    assert counts["frame_count"] == 4 * len(report)
    assert counts["label_count"] == len(report)
    # canonical order regardless of request order - the document is stable
    report2, text2, _ = build_drawing_document(m, ["iso", "top", "front", "side"])
    assert text2 == text
    assert [r["name"] for r in report2] == [r["name"] for r in report]


def test_independent_recount_of_emitted_dxf():
    """Recount LINE entities per layer with a different parse strategy."""
    m = box((0.0, 0.0, 10.0), (40.0, 20.0, 10.0))
    report, text, _ = build_drawing_document(m, ["top", "iso"])
    lines = text.split("\n")
    # blocks: every entity starts with the "0" code line followed by a name
    per_layer: dict[str, int] = {}
    i = 0
    while i < len(lines) - 1:
        if lines[i] == "0" and lines[i + 1] == "LINE":
            layer = lines[i + 3]  # 8 / <layer>
            assert lines[i + 2] == "8"
            per_layer[layer] = per_layer.get(layer, 0) + 1
            i += 2
            continue
        i += 1
    assert per_layer["TOP"] == report[0]["line_count"]
    assert per_layer["ISO"] == report[1]["line_count"]
    assert per_layer.get("SHEET_FRAMES", 0) == 8
    assert per_layer.get("SHEET_LABELS", 0) == 0
    # structural sanity: one sheet frame per view, sections closed
    assert text.count("\nENDSEC") == 3
    assert text.endswith("EOF\n")


def test_dxf_is_deterministic_for_the_same_mesh():
    m1 = box((0.0, 0.0, 5.0), (30.0, 30.0, 5.0))
    m2 = box((0.0, 0.0, 5.0), (30.0, 30.0, 5.0))
    _, text1, _ = build_drawing_document(m1, ["top"])
    _, text2, _ = build_drawing_document(m2, ["top"])
    assert text1 == text2


def test_empty_mesh_cannot_be_drawn():
    with pytest.raises(GeometryValidationError, match="empty mesh"):
        build_drawing_document(_empty_mesh(), ["top"])


def _empty_mesh():
    from src.engines.cad.primitives import Mesh

    return Mesh(triangles=[])


def test_sheet_layout_keeps_views_in_separate_tiles():
    m = box((0.0, 0.0, 10.0), (40.0, 20.0, 10.0))
    report, _, _ = build_drawing_document(m, list(VIEW_ORDER))
    tops = [r["sheet_origin_mm"][0] for r in report]
    assert tops == sorted(tops)  # tiled left to right in canonical order
    heights = [r["sheet_size_mm"][1] for r in report]
    # MARGIN wraps every view; a 10mm tall box keeps frame height > 2*MARGIN
    assert all(h >= 2 * MARGIN_MM for h in heights)


# ------------------------------------------------------------- engine ---


def test_engine_drawings_end_to_end_mounting_plate():
    engine = CADEngine()
    result = engine.execute(
        "drawings",
        {"type": "mounting_plate", "parameters": {}, "views": ["top", "front"]},
    )
    assert result.success is True and result.operation == "drawings"
    rows = result.result["views"]
    assert [r["name"] for r in rows] == ["top", "front"]  # canonical order wins
    assert result.result["sheet_count"] == 2
    assert result.result["line_count_total"] == sum(r["line_count"] for r in rows)
    # the document was self-verified before being returned
    assert result.result["dxf_verified_counts"]["frame_count"] == 8
    assert len(result.pending_artifacts) == 1
    path, artifact_type = result.pending_artifacts[0]
    assert artifact_type == "dxf"
    text = open(path, encoding="utf-8").read()
    assert "SECTION" in text and text.endswith("EOF\n")
    # the artifact path is a real file with the promised layer count
    assert text.count("\n0\nLINE\n") == 8 + sum(r["line_count"] for r in rows)


def test_engine_drawings_all_views_by_default():
    engine = CADEngine()
    result = engine.execute("drawings", {"type": "mounting_plate", "parameters": {}})
    rows = result.result["views"]
    assert [r["name"] for r in rows] == ["top", "front", "side", "iso"]
    for row in rows:
        assert row["plane"] and row["line_count"] > 0


def test_engine_drawings_unknown_part_refuses():
    engine = CADEngine()
    with pytest.raises(RequestValidationError, match="Unsupported CAD type"):
        engine.execute("drawings", {"type": "gridfinity_bin", "parameters": {}})


def test_engine_drawings_unknown_view_refuses():
    engine = CADEngine()
    with pytest.raises(GeometryValidationError, match="view list"):
        engine.execute(
            "drawings", {"type": "mounting_plate", "parameters": {}, "views": ["top", "plan"]}
        )


def test_engine_drawings_gate_refuses_invalid_geometry():
    engine = CADEngine()
    # the part's own engineering-range rule fires at from_request time,
    # exactly like families - assert on the verbatim reason, not a guess
    with pytest.raises(RequestValidationError, match="engineering range"):
        engine.execute(
            "drawings",
            {"type": "quadcopter_frame", "parameters": {"plate_thickness": 60.0}},
        )


def test_drawings_engine_capabilities_advertise_the_op():
    assert "drawings" in CADEngine().capabilities


# -------------------------------------------------------------- route ---


def test_drawings_route_happy_path_and_422s():
    from fastapi.testclient import TestClient

    from src.main import app

    with TestClient(app) as client:
        resp = client.post(
            "/api/cad/drawings",
            json={"type": "mounting_plate", "parameters": {}, "views": ["top"]},
        )
        assert resp.status_code == 200
        body = resp.json()
        assert body["success"] is True
        assert body["operation"] == "drawings"
        assert len(body["result"]["views"]) == 1
        assert body["artifacts"][0]["type"] == "dxf"

        resp2 = client.post("/api/cad/drawings", json={"type": "nope"})
        assert resp2.status_code == 422

        resp3 = client.post("/api/cad/drawings", json={"type": "mounting_plate", "views": ["oblique"]})
        assert resp3.status_code == 422  # Literal edge check, not a 200 envelope
