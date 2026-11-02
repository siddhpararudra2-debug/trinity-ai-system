"""M20.4: the C00 engine gate, electrical checks C06-C07, and the
overall VERIFIED/WARNING/FAILED triad.

C00's input is the record's ``engine_checks`` section — per-artifact
engine-validate verdicts stored verbatim (statuses are the
``src/models/schemas.py`` ValidationLevel chain). C06/C07 read the
record-side ``power`` section: ``rails[]`` ({net, class, capacity_ma,
draws[]}) and ``mcu_rail`` ({class}). Both are optional record sections;
absent means WARN cannot_verify — an unannotated design is not a
passing one.

The numbers: C06 wants exactly one declared class per *board power net*
(discovered from power_in/power_out pads); C07 weighs a rail at ≤ 80%
of capacity (PASS), 81-100% (WARN), over 100% (FAIL — over budget).
"""

from __future__ import annotations

import copy

from src.engines.crosscheck.check import (
    FAIL,
    OVERALL_FAILED,
    OVERALL_VERIFIED,
    OVERALL_WARNING,
    PASS,
    WARN,
    CheckResult,
    overall_status,
    run_checks,
    status_counts,
)
from src.engines.crosscheck.record import build_record

# A two-supply board: J1 feeds 5V, U2 (regulator) makes VCC from it,
# R1 hangs on GND. Board power nets: VCC, 5V, GND. Riders:
#   VCC {U1, U2}, 5V {U2, J1}, GND {U1, U2, J1, R1}
_MIN_PCB = {
    "board": {"width_mm": 50.0, "height_mm": 40.0, "thickness_mm": 1.6,
              "origin_mm": {"x": 0.0, "y": 0.0},
              "layers": ["F.Cu", "B.Cu"]},
    "components": [
        {"ref": "U1", "value": "ESP32-WROOM-32",
         "footprint": "ESP32-WROOM-32", "type": "mcu",
         "rotation_deg": 0.0, "layer": "F.Cu",
         "pins": [
             {"number": "1", "name": "3V3", "electrical": "power_in",
              "pos_mm": {"x": 0.0, "y": 0.0}, "net": "VCC"},
             {"number": "2", "name": "GND", "electrical": "power_in",
              "pos_mm": {"x": 1.0, "y": 0.0}, "net": "GND"},
             {"number": "21", "name": "GPIO21", "electrical":
              "bidirectional", "pos_mm": {"x": 2.0, "y": 0.0},
              "net": "SDA"},
         ]},
        {"ref": "U2", "value": "AMS1117-3.3",
         "footprint": "SOT-223", "type": "regulator",
         "rotation_deg": 0.0, "layer": "F.Cu",
         "pins": [
             {"number": "3", "name": "VIN", "electrical": "power_in",
              "pos_mm": {"x": 0.0, "y": 0.0}, "net": "5V"},
             {"number": "2", "name": "VOUT", "electrical": "power_out",
              "pos_mm": {"x": 1.0, "y": 0.0}, "net": "VCC"},
             {"number": "1", "name": "GND", "electrical": "power_in",
              "pos_mm": {"x": 2.0, "y": 0.0}, "net": "GND"},
         ]},
        {"ref": "J1", "value": "USB-C",
         "footprint": "USB_C_Receptacle", "type": "connector",
         "rotation_deg": 0.0, "layer": "F.Cu",
         "pins": [
             {"number": "4", "name": "VBUS", "electrical": "power_out",
              "pos_mm": {"x": 0.0, "y": 0.0}, "net": "5V"},
             {"number": "5", "name": "GND", "electrical": "power_in",
              "pos_mm": {"x": 1.0, "y": 0.0}, "net": "GND"},
         ]},
        {"ref": "R1", "value": "10k", "footprint": "R_0402",
         "type": "passive", "rotation_deg": 0.0, "layer": "F.Cu",
         "pins": [
             {"number": "2", "name": "2", "electrical": "passive",
              "pos_mm": {"x": 0.0, "y": 0.0}, "net": "GND"},
         ]},
    ],
    "nets": [
        {"id": 1, "name": "VCC", "pins": ["U1.1", "U2.2"]},
        {"id": 2, "name": "5V", "pins": ["U2.3", "J1.4"]},
        {"id": 3, "name": "GND", "pins": ["U1.2", "U2.1", "J1.5",
                                          "R1.2"]},
        {"id": 4, "name": "SDA", "pins": ["U1.21"]},
    ],
    "tracks": [],
    "footprints": {},
    "placements": [],
    "rules": [],
    "metadata": {},
}
_POWER_GOOD = {
    "rails": [
        {"net": "VCC", "class": "3.3V", "capacity_ma": 600.0,
         "draws": [
             {"ref": "U1", "draw_ma": 250.0},
             {"ref": "U2", "draw_ma": 0.0},
         ]},
        {"net": "5V", "class": "5.0V", "capacity_ma": 1000.0,
         "draws": [
             {"ref": "U2", "draw_ma": 300.0},
             {"ref": "J1", "draw_ma": 0.0},
         ]},
        {"net": "GND", "class": "0V", "capacity_ma": 1000.0,
         "draws": [
             {"ref": "U1", "draw_ma": 250.0},
             {"ref": "U2", "draw_ma": 0.0},
             {"ref": "J1", "draw_ma": 0.0},
             {"ref": "R1", "draw_ma": 0.0},
         ]},
    ],
    "mcu_rail": {"class": "3.3V"},
}
_HOLES = [{"ref": "H1", "pos_mm": {"x": 2.5, "y": 2.5},
           "diameter_mm": 3.0}]
_CONNECTORS = [{"ref": "J2", "pos_mm": {"x": 30.0, "y": 40.0},
                "width_mm": 10.0, "height_mm": 5.0, "kind": "usb_c"}]
_ENCLOSURE = {
    "cavity_mm": {"width_mm": 60.0, "height_mm": 50.0, "depth_mm": 20.0,
                  "origin_mm": {"x": -3.0, "y": -4.0}},
    "standoffs": [{"id": "S1", "pos_mm": {"x": 2.5, "y": 2.5}}],
    "cutouts": [{"id": "C1", "pos_mm": {"x": 29.0, "y": 39.0},
                 "width_mm": 12.0, "height_mm": 7.0}],
}


def _record(**overrides) -> object:
    spec = {
        "pcb": copy.deepcopy(_MIN_PCB),
        "firmware": {},
        "holes": copy.deepcopy(_HOLES),
        "connectors": copy.deepcopy(_CONNECTORS),
        "enclosure": copy.deepcopy(_ENCLOSURE),
        "power": copy.deepcopy(_POWER_GOOD),
        "engine_checks": [
            {"artifact": "pcb", "op": "validate",
             "status": "VALIDATED"},
            {"artifact": "firmware", "op": "validate",
             "status": "VERIFIED"},
        ],
    }
    for key, value in overrides.items():
        spec[key] = copy.deepcopy(value)
    return build_record("electrical-tests", **spec)


def _check(checks: list, check_id: str):
    return {result.check_id: result for result in checks}[check_id]


def _power_copy() -> dict:
    return copy.deepcopy(_POWER_GOOD)


# --- C00 ------------------------------------------------------------------


def test_c00_green_gate_passes() -> None:
    c00 = _check(run_checks(_record()), "C00")
    assert c00.status == PASS
    assert c00.reason == "every recorded engine check is VALIDATED " \
        "or VERIFIED"
    rows = c00.evidence["engine_checks"]
    assert len(rows) == 2
    assert all(row["status"] == PASS for row in rows)


def test_c00_engine_failure_fails_with_the_engine_reason_verbatim() \
        -> None:
    gate = [
        {"artifact": "pcb", "op": "validate", "status": "FAILED",
         "detail": "ERC: net GND is shorted to net VCC"},
    ]
    checks = run_checks(_record(engine_checks=gate))
    c00 = _check(checks, "C00")
    assert c00.status == FAIL
    assert c00.reason == (
        "engine check pcb.validate FAILED: ERC: net GND is shorted "
        "to net VCC"
    )
    assert c00.location == "pcb"
    # A gate FAIL makes the whole run FAILED, whatever else WARNs.
    assert overall_status(checks) == OVERALL_FAILED


def test_c00_unfinished_validation_warns_never_passes() -> None:
    gate = [
        {"artifact": "pcb", "op": "generate", "status": "GENERATED"},
        {"artifact": "firmware", "op": "validate",
         "status": "VALIDATED"},
    ]
    c00 = _check(run_checks(_record(engine_checks=gate)), "C00")
    assert c00.status == WARN
    assert "has not completed a validation (status: GENERATED)" \
        in c00.reason
    assert "cannot be passed by default" in c00.evidence[
        "engine_checks"
    ][0]["note"]


def test_c00_absent_or_empty_or_malformed_cannot_verify() -> None:
    c00 = _check(run_checks(_record(engine_checks=None)), "C00")
    assert c00.status == WARN and c00.reason.startswith("cannot_verify")
    assert "engine_checks absent" in c00.reason

    c00 = _check(run_checks(_record(engine_checks=[])), "C00")
    assert c00.status == WARN
    assert "engine_checks holds no entries" in c00.reason

    c00 = _check(
        run_checks(_record(engine_checks=["not an object"])), "C00"
    )
    assert c00.status == WARN
    assert "not an object" in c00.reason


# --- C06 ------------------------------------------------------------------


def test_c06_good_power_classification_passes() -> None:
    c06 = _check(run_checks(_record()), "C06")
    assert c06.status == PASS
    assert c06.reason == (
        "every board power net carries exactly one rail class and the "
        "MCU's rail exists on the board"
    )
    carrier = [row for row in c06.evidence["nets"]
               if "MCU's rail" in row["reason"]]
    assert len(carrier) == 1
    assert carrier[0]["status"] == PASS
    assert "VCC" in carrier[0]["reason"]


def test_c06_unclassified_board_power_net_warns() -> None:
    power = _power_copy()
    power["rails"] = [
        rail for rail in power["rails"] if rail["net"] != "5V"
    ]
    c06 = _check(run_checks(_record(power=power)), "C06")
    assert c06.status == WARN
    assert c06.reason == (
        "power net '5V' is present on the board but no rail "
        "classifies it"
    )
    assert c06.location == "5V"


def test_c06_two_classes_on_one_net_fail() -> None:
    power = _power_copy()
    power["rails"].append(
        {"net": "VCC", "class": "5.0V", "capacity_ma": 700.0,
         "draws": []}
    )
    c06 = _check(run_checks(_record(power=power)), "C06")
    assert c06.status == FAIL
    assert "carries 2 classes (3.3V, 5.0V)" in c06.reason
    assert c06.location == "VCC"


def test_c06_rail_naming_an_undeclared_net_fails() -> None:
    power = _power_copy()
    power["rails"].append(
        {"net": "12V", "class": "12V", "capacity_ma": 1000.0,
         "draws": []}
    )
    c06 = _check(run_checks(_record(power=power)), "C06")
    assert c06.status == FAIL
    assert "the board netlist does not declare" in c06.reason
    assert c06.location == "12V"


def test_c06_rail_on_a_non_power_net_warns() -> None:
    # SDA is declared, but only a bidirectional GPIO pad rides it.
    power = _power_copy()
    power["rails"].append(
        {"net": "SDA", "class": "3.3V", "capacity_ma": 100.0,
         "draws": []}
    )
    c06 = _check(run_checks(_record(power=power)), "C06")
    assert c06.status == WARN
    assert "no power-typed pad rides" in c06.reason


def test_c06_mcu_rail_verdicts() -> None:
    # Absent: cannot_verify, never a PASS.
    power = _power_copy()
    power.pop("mcu_rail")
    c06 = _check(run_checks(_record(power=power)), "C06")
    assert c06.status == WARN
    assert "power.mcu_rail is absent" in c06.reason

    # Present but class-less: same WARN family, field named.
    power = _power_copy()
    power["mcu_rail"] = {"net": "VCC"}
    c06 = _check(run_checks(_record(power=power)), "C06")
    assert c06.status == WARN
    assert "states no rail class" in c06.reason

    # A rail class no board power net carries: FAIL.
    power = _power_copy()
    power["mcu_rail"] = {"class": "12V"}
    c06 = _check(run_checks(_record(power=power)), "C06")
    assert c06.status == FAIL
    assert "the MCU's rail '12V' is missing from the board" \
        in c06.reason


def test_missing_sections_cannot_verify_the_electrical_checks() -> None:
    checks = run_checks(_record(power=None, pcb=None))
    c06 = _check(checks, "C06")
    assert c06.status == WARN and c06.reason.startswith("cannot_verify")
    assert "lacks a pcb section" in c06.reason
    c07 = _check(checks, "C07")
    assert c07.status == WARN
    assert "lacks a pcb section" in c07.reason

    checks = run_checks(_record(power=None))
    for check_id in ("C06", "C07"):
        result = _check(checks, check_id)
        assert result.status == WARN
        assert "lacks a power section" in result.reason
        assert result.location == "power"


# --- C07 ------------------------------------------------------------------


def test_c07_good_budget_passes() -> None:
    c07 = _check(run_checks(_record()), "C07")
    assert c07.status == PASS
    assert c07.reason == "every weighed rail holds at 80% of its " \
        "capacity"
    rows = {row["location"]: row for row in c07.evidence["rails"]}
    assert rows["VCC"]["status"] == PASS
    assert "draws 250.0 mA of 600.0 mA (42%)" in rows["VCC"]["reason"]
    assert "5V" in rows and "GND" in rows


def test_c07_over_budget_fails() -> None:
    power = _power_copy()
    power["rails"][0]["draws"][0]["draw_ma"] = 620.0  # U1
    c07 = _check(run_checks(_record(power=power)), "C07")
    assert c07.status == FAIL
    assert c07.reason == (
        "rail 'VCC' is over budget: 620.0 mA drawn of 600.0 mA capacity"
    )
    assert c07.location == "VCC"


def test_c07_thin_margin_warns() -> None:
    power = _power_copy()
    power["rails"][0]["draws"][0]["draw_ma"] = 520.0  # 87% of 600
    c07 = _check(run_checks(_record(power=power)), "C07")
    assert c07.status == WARN
    assert "runs at 87% of capacity (520.0 of 600.0 mA)" in c07.reason


def test_c07_missing_draw_data_warns() -> None:
    power = _power_copy()
    power["rails"][0]["draws"].pop(1)  # U2 rides VCC but is unmetered
    c07 = _check(run_checks(_record(power=power)), "C07")
    assert c07.status == WARN
    assert c07.reason == (
        "'U2' rides rail 'VCC' on the board but declares no draw data"
    )
    assert c07.location == "U2"


def test_c07_draw_for_a_non_rider_warns() -> None:
    power = _power_copy()
    power["rails"][0]["draws"].append(
        {"ref": "R9", "draw_ma": 50.0}  # R9 has no pad on VCC
    )
    c07 = _check(run_checks(_record(power=power)), "C07")
    assert c07.status == WARN
    assert "draw data for 'R9' on rail 'VCC'" in c07.reason
    assert "no pad of it on this net" in c07.reason


def test_c07_rail_without_capacity_is_not_weighed() -> None:
    power = _power_copy()
    power["rails"][2].pop("capacity_ma")  # GND unweighed
    c07 = _check(run_checks(_record(power=power)), "C07")
    assert c07.status == WARN
    assert (
        "rail 'GND' declares no usable capacity_ma; its draw is not "
        "weighed"
    ) in c07.reason
    assert c07.location == "GND"


def test_c07_negative_draw_cannot_be_summed() -> None:
    power = _power_copy()
    power["rails"][0]["draws"][0]["draw_ma"] = -250.0
    c07 = _check(run_checks(_record(power=power)), "C07")
    assert c07.status == WARN
    assert "negative and cannot be summed" in c07.reason


def test_c07_empty_rails_cannot_verify() -> None:
    power = _power_copy()
    power["rails"] = []
    c07 = _check(run_checks(_record(power=power)), "C07")
    assert c07.status == WARN and c07.reason.startswith("cannot_verify")
    assert "declares no rails" in c07.reason
    assert c07.location == "power.rails"


# --- the overall triad -----------------------------------------------------


def _result(check_id: str, status: str) -> CheckResult:
    return CheckResult(check_id=check_id, name=check_id, status=status,
                       reason="reason")


def test_overall_triad_semantics() -> None:
    assert overall_status([]) == OVERALL_VERIFIED
    assert overall_status([_result("C01", PASS)]) == OVERALL_VERIFIED
    assert overall_status(
        [_result("C01", PASS), _result("C08", WARN)]
    ) == OVERALL_WARNING
    assert overall_status(
        [_result("C01", WARN), _result("C08", FAIL)]
    ) == OVERALL_FAILED
    # FAIL dominates no matter how many WARNs precede it.
    assert overall_status(
        [_result("C0" + str(i), WARN) for i in range(9)]
        + [_result("C10", FAIL)]
    ) == OVERALL_FAILED

    assert status_counts([]) == {PASS: 0, WARN: 0, FAIL: 0}
    assert status_counts(
        [_result("C01", PASS), _result("C08", WARN),
         _result("C09", WARN), _result("C10", FAIL)]
    ) == {PASS: 1, WARN: 2, FAIL: 1}
