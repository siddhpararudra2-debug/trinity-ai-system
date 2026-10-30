"""Nesting tests (M14): shelf packing with pairwise proof. Pure-geometry
truth first, then the real registry parts end to end."""

from __future__ import annotations

import pytest

from src.core.errors import GeometryValidationError, RequestValidationError
from src.engines.cad.engine import CADEngine
from src.engines.cad.nesting import NestJob, pack_layout


def _job(index: int, label: str, w: float, h: float) -> NestJob:
    return NestJob(index=index, label=label, width_mm=w, height_mm=h)


def _overlaps(placements) -> list[tuple[str, str]]:
    """Pairwise rectangle intersection, recomputed here on purpose."""
    bad = []
    for i, a in enumerate(placements):
        for b in placements[i + 1 :]:
            if not (
                a["x_mm"] + a["width_mm"] <= b["x_mm"]
                or b["x_mm"] + b["width_mm"] <= a["x_mm"]
                or a["y_mm"] + a["height_mm"] <= b["y_mm"]
                or b["y_mm"] + b["height_mm"] <= a["y_mm"]
            ):
                bad.append((a["label"], b["label"]))
    return bad


def test_pack_two_small_rectangles_no_overlap():
    out = pack_layout(100.0, 100.0, 2.0, True, [_job(0, "a", 30.0, 20.0), _job(1, "b", 20.0, 20.0)])
    placements = out["placements"]
    assert len(placements) == 2 and out["unplaced"] == []
    assert _overlaps(placements) == []
    # shelf order: longest side first (a then b), placed on row y=0
    a = placements[0]
    assert a["x_mm"] == 0.0 and a["y_mm"] == 0.0 and not a["rotated"]
    b = placements[1]
    assert b["x_mm"] == 32.0 and b["y_mm"] == 0.0  # a.x + a.w + spacing


def test_spacing_gap_is_enforced_between_placements():
    out = pack_layout(100.0, 100.0, 5.0, False, [_job(0, "a", 30.0, 20.0), _job(1, "b", 30.0, 20.0)])
    a, b = out["placements"]
    assert b["x_mm"] - (a["x_mm"] + a["width_mm"]) == 5.0


def test_rotation_matches_taller_than_wide_parts():
    # taller than wide: 20x40 -> rotates to 40x20 with allow_rotation
    out = pack_layout(100.0, 100.0, 2.0, True, [_job(0, "tall", 20.0, 40.0)])
    p = out["placements"][0]
    assert p["rotated"] is True and p["width_mm"] == 40.0 and p["height_mm"] == 20.0
    # same request without rotation keeps the original footprint
    out2 = pack_layout(100.0, 100.0, 2.0, False, [_job(0, "tall", 20.0, 40.0)])
    p2 = out2["placements"][0]
    assert p2["rotated"] is False and p2["width_mm"] == 20.0 and p2["height_mm"] == 40.0


def test_oversized_part_is_unplaced_not_dropped():
    out = pack_layout(50.0, 50.0, 2.0, True, [_job(0, "big", 120.0, 30.0)])
    assert out["placements"] == []
    assert out["unplaced"][0]["reason"].startswith("footprint")
    assert "exceeds stock" in out["unplaced"][0]["reason"]


def test_second_shelf_wraps_when_row_is_full():
    # shelf rows fill x, overflow wraps to the next y row with spacing gap
    out = pack_layout(100.0, 100.0, 2.0, False, [_job(0, "a", 60.0, 20.0), _job(1, "b", 60.0, 20.0)])
    a, b = out["placements"]
    assert _overlaps(out["placements"]) == []
    assert a["y_mm"] == 0.0 and b["y_mm"] == 22.0  # shelf wrap adds spacing
    assert b["x_mm"] == 0.0


def test_determinism_same_input_same_layout():
    jobs = [_job(0, "a", 30.0, 25.0), _job(1, "b", 45.0, 15.0), _job(2, "c", 10.0, 10.0)]
    first = pack_layout(100.0, 100.0, 2.0, True, jobs)
    second = pack_layout(100.0, 100.0, 2.0, True, jobs)
    assert first["placements"] == second["placements"]


def test_invalid_stock_refuses():
    with pytest.raises(GeometryValidationError, match="positive"):
        pack_layout(0.0, 100.0, 2.0, True, [_job(0, "a", 10.0, 10.0)])
    with pytest.raises(GeometryValidationError, match=">= 0"):
        pack_layout(100.0, 100.0, -1.0, True, [_job(0, "a", 10.0, 10.0)])


def test_engine_nest_with_real_parts_end_to_end():
    engine = CADEngine()
    result = engine.execute(
        "nest",
        {
            "parts": [
                {"type": "mounting_plate", "quantity": 2},
                {"type": "quadcopter_frame"},
            ],
            "stock_width_mm": 300.0,
            "stock_height_mm": 200.0,
            "spacing_mm": 3.0,
            "allow_rotation": True,
        },
    )
    assert result.success is True
    layout = result.result
    placements = layout["placements"]
    assert len(placements) == 3
    assert _overlaps(placements) == []
    # every placement inside the stock
    for p in placements:
        assert p["x_mm"] >= 0 and p["y_mm"] >= 0
        assert p["x_mm"] + p["width_mm"] <= 300.0 + 1e-9
        assert p["y_mm"] + p["height_mm"] <= 200.0 + 1e-9
    # spacing: any two placements are separated by >= spacing in at
    # least one axis (parts side by side share their y-range, so the
    # other axis decides)
    for i, a in enumerate(placements):
        for b in placements[i + 1 :]:
            gap_x = max(a["x_mm"] - (b["x_mm"] + b["width_mm"]),
                        b["x_mm"] - (a["x_mm"] + a["width_mm"]))
            gap_y = max(a["y_mm"] - (b["y_mm"] + b["height_mm"]),
                        b["y_mm"] - (a["y_mm"] + a["height_mm"]))
            assert max(gap_x, gap_y) >= 3.0 - 1e-9
    assert layout["fill_ratio_mm2"]["ratio"] > 0


def test_engine_nest_bad_part_fails_only_that_entry():
    engine = CADEngine()
    result = engine.execute(
        "nest",
        {
            "parts": [
                {"type": "mounting_plate", "parameters": {"width": 60.0}},
                {"type": "mounting_plate", "parameters": {"width": -5.0}},
            ],
            "stock_width_mm": 200.0,
            "stock_height_mm": 200.0,
        },
    )
    layout = result.result
    assert len(layout["placements"]) == 1
    bad = layout["unplaced"][0]
    assert bad["index"] == 1
    assert bad["reason"].startswith("part rejected:")


def test_engine_nest_requires_stock_dimensions():
    engine = CADEngine()
    with pytest.raises(RequestValidationError, match="stock_width_mm"):
        engine.execute("nest", {"parts": [{"type": "mounting_plate"}]})
