"""The real PCB fallback (Item: placement, routing, design checks).

Every design rule gets a passing and a failing case; the whole engine is
deterministic, so the assertions pin exact outcomes, not vibes.
"""

from __future__ import annotations

import json
import math
from typing import Any

import pytest

from src.engines.pcb import (
    BOTTOM_LAYER,
    DEFAULT_TRACK_WIDTH_MM,
    PcbEngine,
    PcbError,
    TOP_LAYER,
    _check_clearances,
    _parse_components,
)


@pytest.fixture()
def engine() -> PcbEngine:
    return PcbEngine()


def _comp(
    ref: str,
    nets: list[str] | None = None,
    size: tuple[float, float] = (10.0, 10.0),
    x: float | None = None,
    y: float | None = None,
) -> dict[str, Any]:
    entry: dict[str, Any] = {
        "value": ref,
        "footprint": "TEST_BODY",
        "size_mm": list(size),
        "nets": nets or [],
    }
    if x is not None and y is not None:
        entry["parameters"] = {"x_mm": x, "y_mm": y}
    return {ref: entry}


def _rect(
    comp: dict[str, Any], size: tuple[float, float] = (10.0, 10.0)
) -> tuple[float, float, float, float]:
    w, h = size
    return (
        comp["x_mm"] - w / 2,
        comp["y_mm"] - h / 2,
        comp["x_mm"] + w / 2,
        comp["y_mm"] + h / 2,
    )


# ------------------------------------------------------- output shape ---


def test_generate_keeps_the_legacy_output_shape(engine: PcbEngine) -> None:
    netlist: dict[str, Any] = {}
    netlist.update(_comp("U1", ["GND", "VCC"], size=(10.0, 10.0), x=12.0, y=20.0))
    netlist.update(_comp("J1", ["GND", "VCC"], size=(10.0, 10.0), x=40.0, y=20.0))
    result = engine.execute(
        "generate",
        {
            "width_mm": 50.0,
            "height_mm": 40.0,
            "thickness_mm": 1.6,
            "netlist": netlist,
        },
    )
    assert result.success is True
    body = result.result
    # The keys the workflow/eval consumers have always read:
    for key in (
        "outline",
        "board_width_mm",
        "board_height_mm",
        "board_thickness_mm",
        "components",
        "netlist_summary",
    ):
        assert key in body, key
    assert body["outline"]["type"] == "rectangle"
    assert body["board_width_mm"] == 50.0
    assert body["netlist_summary"]["U1"] == {"nets": ["GND", "VCC"]}
    placement = body["components"][0]
    for key in ("ref", "value", "footprint", "x_mm", "y_mm", "rotation_deg"):
        assert key in placement, key


def test_generate_empty_netlist_is_validated(engine: PcbEngine) -> None:
    result = engine.execute("generate", {})
    assert result.success is True
    assert result.validation is not None
    assert result.validation.status == "VALIDATED"
    assert result.result["components"] == []
    assert result.result["tracks"] == []
    assert all(
        check["status"] == "PASS"
        for check in result.result["design_checks"].values()
    )


def test_fixed_positions_are_honored(engine: PcbEngine) -> None:
    netlist: dict[str, Any] = {}
    netlist.update(_comp("U1", x=10.0, y=10.0))
    result = engine.execute("generate", {"netlist": netlist}).result
    assert result["placement_mode"]["fixed"] == ["U1"]
    assert result["components"][0]["x_mm"] == 10.0
    assert result["components"][0]["y_mm"] == 10.0


# ---------------------------------------------------- placement rules ---


def test_auto_placement_has_no_overlaps(engine: PcbEngine) -> None:
    netlist: dict[str, Any] = {}
    for i in range(6):
        netlist.update(_comp(f"U{i}", size=(10.0, 10.0)))
    result = engine.execute(
        "generate",
        {"width_mm": 60.0, "height_mm": 40.0, "netlist": netlist},
    )
    assert result.success is True, result.result["design_checks"]
    assert result.result["design_checks"]["overlap"]["status"] == "PASS"
    rects = [_rect(c) for c in result.result["components"]]
    for i, first in enumerate(rects):
        for second in rects[i + 1 :]:
            separated = (
                first[2] <= second[0] + 1e-9
                or second[2] <= first[0] + 1e-9
                or first[3] <= second[1] + 1e-9
                or second[3] <= first[1] + 1e-9
            )
            assert separated, (first, second)


def test_overlap_is_reported_and_denies_the_result(engine: PcbEngine) -> None:
    netlist: dict[str, Any] = {}
    netlist.update(_comp("U1", x=10.0, y=10.0))
    netlist.update(_comp("U2", x=15.0, y=10.0))
    result = engine.execute(
        "generate", {"width_mm": 50.0, "height_mm": 40.0, "netlist": netlist}
    )
    assert result.success is False
    assert result.validation is not None
    assert result.validation.status == "FAILED"
    overlap = result.result["design_checks"]["overlap"]
    assert overlap["status"] == "FAIL"
    assert any("U1 and U2 overlap" in f for f in overlap["findings"])


def test_auto_placement_respects_the_keep_out_margin(engine: PcbEngine) -> None:
    netlist: dict[str, Any] = {}
    for i in range(4):
        netlist.update(_comp(f"U{i}", size=(10.0, 10.0)))
    keepout = 2.0
    result = engine.execute(
        "generate",
        {"width_mm": 50.0, "height_mm": 40.0, "keepout_mm": keepout, "netlist": netlist},
    )
    assert result.result["design_checks"]["out_of_bounds"]["status"] == "PASS"
    for comp in result.result["components"]:
        x1, y1, x2, y2 = _rect(comp)
        assert x1 >= keepout - 1e-9
        assert y1 >= keepout - 1e-9
        assert x2 <= 50.0 - keepout + 1e-9
        assert y2 <= 40.0 - keepout + 1e-9


def test_keep_out_violation_is_reported(engine: PcbEngine) -> None:
    netlist: dict[str, Any] = {}
    netlist.update(_comp("U1", x=10.0, y=10.0))
    result = engine.execute(
        "generate",
        {"width_mm": 50.0, "height_mm": 40.0, "keepout_mm": 12.0, "netlist": netlist},
    )
    assert result.success is False
    bounds = result.result["design_checks"]["out_of_bounds"]
    assert bounds["status"] == "FAIL"
    assert any("keep-out margin" in f for f in bounds["findings"])


def test_connected_parts_are_placed_near_each_other(engine: PcbEngine) -> None:
    # UA is fixed at the left edge; UB shares a net with UA, UC shares
    # nothing. The affinity rule must pull UB toward UA and leave UC at
    # the board centre.
    netlist: dict[str, Any] = {}
    netlist.update(_comp("UA", ["N"], x=7.0, y=30.0))
    netlist.update(_comp("UB", nets=["N"]))
    netlist.update(_comp("UC"))
    result = engine.execute(
        "generate",
        {"width_mm": 100.0, "height_mm": 60.0, "netlist": netlist},
    ).result
    by_ref = {c["ref"]: c for c in result["components"]}
    dist_ub = math.dist((by_ref["UB"]["x_mm"], by_ref["UB"]["y_mm"]), (7.0, 30.0))
    dist_uc = math.dist((by_ref["UC"]["x_mm"], by_ref["UC"]["y_mm"]), (7.0, 30.0))
    assert dist_ub < 15.0, by_ref["UB"]
    assert dist_uc > 35.0, by_ref["UC"]


def test_unconnected_parts_are_not_pulled_to_a_neighbour(engine: PcbEngine) -> None:
    # The counterfactual: without the shared net, UB targets the board
    # centre like everything else — the pull in the paired test is the
    # net's doing, not the fixed part's gravity.
    netlist: dict[str, Any] = {}
    netlist.update(_comp("UA", x=7.0, y=30.0))
    netlist.update(_comp("UB"))
    result = engine.execute(
        "generate",
        {"width_mm": 100.0, "height_mm": 60.0, "netlist": netlist},
    ).result
    by_ref = {c["ref"]: c for c in result["components"]}
    dist_ub = math.dist((by_ref["UB"]["x_mm"], by_ref["UB"]["y_mm"]), (7.0, 30.0))
    assert dist_ub > 35.0, by_ref["UB"]


def test_generate_is_deterministic(engine: PcbEngine) -> None:
    netlist: dict[str, Any] = {}
    netlist.update(_comp("U1", ["N1", "N2"], x=8.0, y=8.0))
    netlist.update(_comp("U2", ["N1", "N2"]))
    netlist.update(_comp("U3"))
    params = {"width_mm": 40.0, "height_mm": 30.0, "netlist": netlist}
    first = engine.execute("generate", params).result
    second = engine.execute("generate", params).result
    assert json.dumps(first, sort_keys=True) == json.dumps(second, sort_keys=True)


# ------------------------------------------------------- routing rules ---


def test_routes_all_nets_on_two_copper_layers(engine: PcbEngine) -> None:
    netlist: dict[str, Any] = {}
    netlist.update(
        {
            "U1": {
                "value": "ESP32-WROOM-32",
                "footprint": "ESP32-WROOM-32",
                "nets": ["GND", "VCC", "SDA", "SCL"],
                "parameters": {"x_mm": 12.0, "y_mm": 20.0},
            }
        }
    )
    netlist.update(_comp("J1", ["GND", "VCC", "SDA", "SCL"], x=40.0, y=30.0))
    result = engine.execute(
        "generate",
        {"width_mm": 50.0, "height_mm": 40.0, "netlist": netlist},
    )
    assert result.success is True, result.result["design_checks"]
    checks = result.result["design_checks"]
    assert checks["unrouted_nets"]["status"] == "PASS"
    assert checks["clearance"]["status"] == "PASS"
    tracks = result.result["tracks"]
    routed_nets = {t["net"] for t in tracks}
    assert {"GND", "VCC", "SDA", "SCL"} <= routed_nets
    layers = {t["layer"] for t in tracks}
    assert layers <= {TOP_LAYER, BOTTOM_LAYER}
    assert result.result["design_rules"]["layers"] == 2
    for track in tracks:
        assert track["width_mm"] == pytest.approx(DEFAULT_TRACK_WIDTH_MM)
        assert len(track["points"]) >= 2


def test_clearance_parameter_controls_routability(engine: PcbEngine) -> None:
    netlist: dict[str, Any] = {}
    netlist.update(_comp("U1", ["N1", "N2"], size=(6.0, 6.0), x=4.0, y=5.0))
    netlist.update(_comp("U2", ["N1", "N2"], size=(6.0, 6.0), x=16.0, y=5.0))
    params: dict[str, Any] = {
        "width_mm": 20.0,
        "height_mm": 10.0,
        "keepout_mm": 1.0,
        "netlist": netlist,
    }
    loose = engine.execute("generate", {**params, "clearance_mm": 0.2})
    assert loose.success is True, loose.result["design_checks"]
    assert loose.result["design_checks"]["unrouted_nets"]["status"] == "PASS"
    assert loose.result["design_rules"]["clearance_mm"] == 0.2

    # An absurd clearance leaves no room for the second net: the engine
    # must report the net unrouted, not claim success.
    tight = engine.execute("generate", {**params, "clearance_mm": 10.0})
    assert tight.success is False
    unrouted = tight.result["design_checks"]["unrouted_nets"]
    assert unrouted["status"] == "FAIL"
    assert any(
        "'N2' is unrouted between U1 and U2" in f for f in unrouted["findings"]
    )
    assert tight.result["design_rules"]["clearance_mm"] == 10.0


def test_via_jumps_to_the_bottom_layer_when_the_top_is_walled(engine: PcbEngine) -> None:
    netlist: dict[str, Any] = {}
    netlist.update(_comp("U1", ["N"], x=10.0, y=10.0))
    netlist.update(_comp("U2", ["N"], x=10.0, y=30.0))
    # A body spanning nearly the full width seals the top layer between
    # the two parts; the only sane route crosses on B.Cu through vias.
    netlist.update(_comp("W1", size=(44.0, 2.0), x=25.0, y=20.0))
    result = engine.execute(
        "generate",
        {"width_mm": 50.0, "height_mm": 40.0, "netlist": netlist},
    )
    assert result.success is True, result.result["design_checks"]
    assert result.result["design_checks"]["unrouted_nets"]["status"] == "PASS"
    vias = result.result["vias"]
    assert len(vias) == 2, vias
    assert {v["net"] for v in vias} == {"N"}
    layers = {t["layer"] for t in result.result["tracks"]}
    assert BOTTOM_LAYER in layers
    assert layers <= {TOP_LAYER, BOTTOM_LAYER}


# ------------------------------------------------------ clearance rule ---


def test_clearance_check_flags_violations_directly() -> None:
    components = _parse_components(
        {
            **_comp("U1", ["NA"], size=(10.0, 10.0), x=20.0, y=20.0),
            **_comp("U2", ["NB"], size=(10.0, 10.0), x=40.0, y=20.0),
        }
    )
    routing = {
        "tracks": [
            {
                "net": "NA",
                "layer": TOP_LAYER,
                "width_mm": 0.2,
                "points": [
                    {"x_mm": 0.0, "y_mm": 0.0},
                    {"x_mm": 5.0, "y_mm": 0.0},
                ],
            },
            {
                # 0.2 mm from NA's centreline: needs 0.4 with defaults.
                "net": "NB",
                "layer": TOP_LAYER,
                "width_mm": 0.2,
                "points": [
                    {"x_mm": 0.0, "y_mm": 0.2},
                    {"x_mm": 5.0, "y_mm": 0.2},
                ],
            },
            {
                # Same net as NA: proximity here is legal, not a finding.
                "net": "NA",
                "layer": BOTTOM_LAYER,
                "width_mm": 0.2,
                "points": [
                    {"x_mm": 0.0, "y_mm": 0.0},
                    {"x_mm": 5.0, "y_mm": 0.0},
                ],
            },
        ],
        "vias": [],
        "unrouted": [],
    }
    findings = _check_clearances(components, routing, 0.2, 0.2)
    assert any("'NA' and 'NB'" in f for f in findings)
    assert not any("0.2 mm apart on B.Cu" in f for f in findings)


# --------------------------------------------------------- refusals -----


def test_impossible_parameters_are_refused(engine: PcbEngine) -> None:
    with pytest.raises(PcbError, match="must be positive"):
        engine.execute("generate", {"width_mm": 0.0})
    # A grid finer than the guard allows, on a board with real nets to
    # route, is refused instead of grinding to a halt.
    netlist: dict[str, Any] = {}
    netlist.update(_comp("U1", ["N"], x=10.0, y=10.0))
    netlist.update(_comp("U2", ["N"], x=30.0, y=10.0))
    with pytest.raises(PcbError, match="too fine"):
        engine.execute(
            "generate",
            {
                "width_mm": 1000.0,
                "height_mm": 1000.0,
                "grid_mm": 0.1,
                "netlist": netlist,
            },
        )


def test_validate_op_still_grades_record_shaped_input(engine: PcbEngine) -> None:
    result = engine.execute(
        "validate",
        {
            "components": [{"ref": "U1", "value": "X", "nets": ["ONLY_ONE"]}],
            "nets": ["ONLY_ONE"],
        },
    )
    assert result.success is False
    assert result.validation is not None
    assert result.validation.status == "FAILED"


# ------------------------------------------------------------- export ---


def test_export_writes_layers_and_labels_the_placeholder(engine: PcbEngine) -> None:
    netlist: dict[str, Any] = {}
    netlist.update(_comp("U1", ["N"], x=10.0, y=10.0))
    netlist.update(_comp("U2", ["N"], x=30.0, y=10.0))
    result = engine.execute(
        "export",
        {"width_mm": 50.0, "height_mm": 20.0, "netlist": netlist},
    )
    assert result.success is True
    assert result.pending_artifacts, "export produced no layer files"
    assert result.result["gerber_status"].startswith("placeholder")
    assert result.result["tracks"], "export should carry the routed tracks"
