"""M20.3: enclosure cross-checks C08-C10 — record-side geometry.

The enclosure/holes/connectors sections are Trinity-owned record data:
one mm frame, `_mm`-suffixed fields, and `enclosure.cavity_mm` must state
its own `origin_mm` — the checker never guesses where a cavity sits.
Bands per decided.md (convention 6): C08/C10 clearances below 0.5 mm
WARN, negative clearance FAIL; C09 standoff/hole pairs within 0.2 mm
drift PASS, within 1.0 mm WARN, beyond that the standoff simply has no
board hole (FAIL — `HOLE_CAPTURE_MM = 1.0`).

The electrical fragments here stay minimal on purpose: C02-C05 are
M20.2's subject, so this file asserts only C01/C08/C09/C10 and lets the
electrical checks be whatever they will be.
"""

from __future__ import annotations

import copy

from src.engines.crosscheck.check import (
    CHECK_ORDER,
    FAIL,
    OVERALL_WARNING,
    PASS,
    WARN,
    run_checks,
    overall_status,
)
from src.engines.crosscheck.record import build_record

# Good geometry: a 50x40 board at (0,0) inside a 60x50 cavity parked at
# (-3,-4) (tightest edge: left, 3.00 mm); two standoffs dead on their
# board holes; J1 (10x5 at (30,40)) inside a 12x7 cutout at (29,39) with
# 1.00 mm margins all around.
_HOLES = [
    {"ref": "H1", "pos_mm": {"x": 2.5, "y": 2.5}, "diameter_mm": 3.0},
    {"ref": "H2", "pos_mm": {"x": 47.5, "y": 37.5}, "diameter_mm": 3.0},
]
_CONNECTORS = [
    {"ref": "J1", "pos_mm": {"x": 30.0, "y": 40.0},
     "width_mm": 10.0, "height_mm": 5.0, "kind": "usb_c"},
]
_ENCLOSURE = {
    "cavity_mm": {"width_mm": 60.0, "height_mm": 50.0, "depth_mm": 20.0,
                  "origin_mm": {"x": -3.0, "y": -4.0}},
    "standoffs": [
        {"id": "S1", "pos_mm": {"x": 2.5, "y": 2.5}},
        {"id": "S2", "pos_mm": {"x": 47.5, "y": 37.5}},
    ],
    "cutouts": [
        {"id": "C1", "pos_mm": {"x": 29.0, "y": 39.0},
         "width_mm": 12.0, "height_mm": 7.0},
    ],
}
_PCB = {
    "board": {"width_mm": 50.0, "height_mm": 40.0, "thickness_mm": 1.6,
              "origin_mm": {"x": 0.0, "y": 0.0},
              "layers": ["F.Cu", "B.Cu"]},
    "components": [],
    "nets": [],
    "tracks": [],
    "footprints": {},
    "placements": [],
    "rules": [],
    "metadata": {},
}


def _record(**overrides) -> object:
    spec = {
        "pcb": copy.deepcopy(_PCB),
        "firmware": {},
        "holes": copy.deepcopy(_HOLES),
        "connectors": copy.deepcopy(_CONNECTORS),
        "enclosure": copy.deepcopy(_ENCLOSURE),
    }
    for key, value in overrides.items():
        spec[key] = copy.deepcopy(value)
    return build_record("enclosure-tests", **spec)


def _check(checks: list, check_id: str):
    return {result.check_id: result for result in checks}[check_id]


def _enclosure(**replacements) -> dict:
    enclosure = copy.deepcopy(_ENCLOSURE)
    for key, value in replacements.items():
        if value is None:
            enclosure.pop(key, None)
        else:
            enclosure[key] = copy.deepcopy(value)
    return enclosure


def test_good_geometry_record_completes_with_c01_c08_c09_c10() -> None:
    checks = run_checks(_record())
    assert [result.check_id for result in checks] == list(CHECK_ORDER)
    for check_id in ("C01", "C08", "C09", "C10"):
        result = _check(checks, check_id)
        assert result.status == PASS, (
            f"{check_id} did not pass: {result.reason}"
        )
    assert "left" in _check(checks, "C08").reason
    assert "3.00 mm" in _check(checks, "C08").reason
    # C09 evidence carries one row per standoff, both exact.
    c09 = _check(checks, "C09")
    assert all(row["status"] == PASS for row in c09.evidence["standoffs"])
    assert c09.evidence["counts"] == {
        "standoffs": 2,
        "paired_holes": 2,
        "malformed_holes": 0,
        "posless_standoffs": 0,
    }
    # C10 names the exposing cutout in the row's evidence note.
    c10 = _check(checks, "C10")
    assert c10.evidence["connectors"][0]["status"] == PASS
    assert c10.evidence["connectors"][0]["note"] == "cutout 'C1'"
    # No FAIL here, but the electrical sections are absent — the run
    # is WARNING, never VERIFIED (WARN never grants VERIFIED).
    assert overall_status(checks) == OVERALL_WARNING


def test_c08_board_sticking_out_fails_naming_edge_and_mm() -> None:
    enclosure = _enclosure(
        cavity_mm={"width_mm": 40.0, "height_mm": 30.0,
                   "depth_mm": 20.0, "origin_mm": {"x": 5.0, "y": 5.0}},
    )
    c08 = _check(run_checks(_record(enclosure=enclosure)), "C08")
    assert c08.status == FAIL
    assert "sticks out past the cavity's left edge by 5.00 mm" in c08.reason
    assert c08.location == "board.left"
    assert c08.evidence["edge_clearances_mm"]["left"] == -5.0


def test_c08_thin_clearance_warns() -> None:
    enclosure = _enclosure(
        cavity_mm={"width_mm": 60.0, "height_mm": 50.0, "depth_mm": 20.0,
                   "origin_mm": {"x": -0.3, "y": -5.0}},
    )
    c08 = _check(run_checks(_record(enclosure=enclosure)), "C08")
    assert c08.status == WARN
    assert "edge 'left' holds only 0.30 mm clearance" in c08.reason
    assert c08.location == "board.left"


def test_c08_without_cavity_origin_cannot_verify() -> None:
    # The decided rule: the cavity states where it sits; a missing
    # origin is a WARN cannot_verify, never a guessed (0, 0).
    enclosure = _enclosure(
        cavity_mm={"width_mm": 60.0, "height_mm": 50.0, "depth_mm": 20.0},
    )
    c08 = _check(run_checks(_record(enclosure=enclosure)), "C08")
    assert c08.status == WARN
    assert c08.reason.startswith("cannot_verify")
    assert "enclosure.cavity_mm.origin_mm is absent" in c08.reason
    assert c08.location == "enclosure.cavity_mm.origin_mm"


def test_c09_standoff_hole_drift_warns() -> None:
    holes = copy.deepcopy(_HOLES)
    holes[0]["pos_mm"] = {"x": 2.65, "y": 2.65}  # 0.21 mm off H1's standoff
    c09 = _check(run_checks(_record(holes=holes)), "C09")
    assert c09.status == WARN
    assert "only at 0.21 mm drift (> 0.2 mm)" in c09.reason
    assert c09.location == "S1"


def test_c09_standoff_without_board_hole_fails() -> None:
    # One standoff, one distant hole: beyond HOLE_CAPTURE_MM the
    # standoff simply has no hole.
    holes = [{"ref": "H1", "pos_mm": {"x": 10.0, "y": 10.0},
              "diameter_mm": 3.0}]
    enclosure = _enclosure(
        standoffs=[{"id": "S1", "pos_mm": {"x": 2.5, "y": 2.5}}],
    )
    c09 = _check(run_checks(_record(holes=holes, enclosure=enclosure)),
                 "C09")
    assert c09.status == FAIL
    assert "standoff 'S1' has no board hole within 1.0 mm" in c09.reason
    assert "nearest 'H1' at 10.61 mm" in c09.reason
    assert c09.location == "S1"


def test_c09_without_usable_hole_positions_fails() -> None:
    holes = [{"ref": "H9"}]  # no pos_mm anywhere
    c09 = _check(run_checks(_record(holes=holes)), "C09")
    assert c09.status == FAIL
    assert "the holes section holds no usable position" in c09.reason


def test_c09_with_no_standoffs_is_a_vacuous_pass() -> None:
    enclosure = _enclosure(standoffs=[])
    c09 = _check(run_checks(_record(enclosure=enclosure)), "C09")
    assert c09.status == PASS
    assert "no standoff/hole pairs to check" in c09.reason


def test_c09_missing_inputs_cannot_verify() -> None:
    checks = run_checks(_record(holes=None))
    c09 = _check(checks, "C09")
    assert c09.status == WARN and c09.reason.startswith("cannot_verify")
    assert "lacks a holes section" in c09.reason

    # An enclosure without a standoffs list cannot verify either.
    c09 = _check(run_checks(_record(enclosure=_enclosure(standoffs=None))),
                 "C09")
    assert c09.status == WARN
    assert "enclosure.standoffs is absent or malformed" in c09.reason
    assert c09.location == "enclosure.standoffs"


def test_c10_missing_inputs_cannot_verify() -> None:
    checks = run_checks(_record(connectors=None, enclosure=None))
    c10 = _check(checks, "C10")
    assert c10.status == WARN and c10.reason.startswith("cannot_verify")
    assert "lacks a connectors section" in c10.reason

    # An enclosure without a cutouts list cannot verify either.
    c10 = _check(run_checks(_record(enclosure=_enclosure(cutouts=None))),
                 "C10")
    assert c10.status == WARN
    assert "enclosure.cutouts is absent or malformed" in c10.reason
    assert c10.location == "enclosure.cutouts"


def test_c10_connector_tight_in_cutout_warns() -> None:
    enclosure = _enclosure(
        cutouts=[{"id": "C1", "pos_mm": {"x": 29.8, "y": 39.8},
                  "width_mm": 10.4, "height_mm": 5.4}],
    )
    c10 = _check(run_checks(_record(enclosure=enclosure)), "C10")
    assert c10.status == WARN
    assert "inside cutout 'C1' but its tightest edge holds only 0.20 mm" \
        in c10.reason


def test_c10_uncovered_connector_fails_naming_closest_window() -> None:
    # J1 moved onto the board body, same row as the cutout: 19.00 mm
    # short of C1's left edge.
    connectors = [
        {"ref": "J1", "pos_mm": {"x": 10.0, "y": 40.0},
         "width_mm": 6.0, "height_mm": 4.0, "kind": "usb_c"},
    ]
    c10 = _check(run_checks(_record(connectors=connectors)), "C10")
    assert c10.status == FAIL
    assert "covered by no cutout" in c10.reason
    assert "closest window 'C1' leaves it 19.00 mm past its left edge" \
        in c10.reason
    assert c10.location == "J1"


def test_c10_without_any_cutout_fails() -> None:
    enclosure = _enclosure(cutouts=[])
    c10 = _check(run_checks(_record(enclosure=enclosure)), "C10")
    assert c10.status == FAIL
    assert "no cutout window: the enclosure declares no usable cutout" \
        in c10.reason


def test_c10_degenerate_connectors_cannot_be_checked() -> None:
    # One connector without position/size, one non-object entry: both
    # are honest WARNs naming what cannot be checked.
    connectors = [{"ref": "J2"}, "oops"]
    c10 = _check(run_checks(_record(connectors=connectors)), "C10")
    assert c10.status == WARN
    assert "'J2' has no usable pos_mm/width_mm/height_mm" in c10.reason
    non_object = [row for row in c10.evidence["connectors"]
                  if "non-object entry" in row["reason"]]
    assert len(non_object) == 1
    assert c10.evidence["counts"] == {
        "connectors": 2,
        "usable_cutouts": 1,
        "malformed_cutouts": 0,
    }


def test_c10_with_no_connectors_is_a_vacuous_pass() -> None:
    connectors = []
    c10 = _check(run_checks(_record(connectors=connectors)), "C10")
    assert c10.status == PASS
    assert "no external connectors to expose" in c10.reason
    assert c10.evidence["counts"] == {
        "connectors": 0,
        "usable_cutouts": 1,
        "malformed_cutouts": 0,
    }
