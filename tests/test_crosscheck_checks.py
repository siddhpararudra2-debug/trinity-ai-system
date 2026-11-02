"""M20.2: cross-checks C01-C05 — framework, verdicts, and the phase's
done-when: a wrong pin gives FAILED with the correct reason.

The records here are the same honestly-incomplete things M20.1 built;
the checker's contract lives in `src/engines/crosscheck/check.py`
decided.md's rule table rows C01-C05 (C00 gate and C06-C11 come in later
milestones). Fixture fragments use the native serializers' real wire
names; the wrong-pin cases are the acceptance fixtures the eval seeds
will reuse.
"""

from __future__ import annotations

import copy

from src.engines.crosscheck.bom import bom_from_pcb
from src.engines.crosscheck.check import (
    CHECK_ORDER,
    FAIL,
    OVERALL_FAILED,
    OVERALL_VERIFIED,
    PASS,
    WARN,
    CheckResult,
    overall_status,
    run_checks,
    status_counts,
)
from src.engines.crosscheck.record import build_record

# A good, complete design: I2C0 wired in full, GPIO out on a resistor,
# everything attachment-counted. Reused (deep-copied) by the wrong-pin
# variants below.

_HOLES = [{"ref": "H1", "pos_mm": {"x": 2.5, "y": 2.5},
           "diameter_mm": 3.0}]
_CONNECTORS = [{"ref": "J1", "pos_mm": {"x": 30.0, "y": 40.0},
                "width_mm": 10.0, "height_mm": 5.0, "kind": "usb_c"}]
_ENCLOSURE = {
    "cavity_mm": {"width_mm": 60.0, "height_mm": 50.0, "depth_mm": 20.0,
                  "origin_mm": {"x": -3.0, "y": -4.0}},
    "standoffs": [{"id": "S1", "pos_mm": {"x": 2.5, "y": 2.5}}],
    "cutouts": [
        {"id": "C1", "pos_mm": {"x": 29.0, "y": 39.0},
         "width_mm": 12.0, "height_mm": 7.0}
    ],
}


def _mcu_pin(number: str, name: str, net: str, electrical: str
             ) -> dict:
    return {
        "number": number,
        "name": name,
        "electrical": electrical,
        "pos_mm": {"x": 0.0, "y": 0.0},
        "net": net,
    }


def _good_pcb() -> dict:
    return {
        "board": {
            "width_mm": 50.0, "height_mm": 40.0, "thickness_mm": 1.6,
            "origin_mm": {"x": 0.0, "y": 0.0},
            "layers": ["F.Cu", "B.Cu"],
        },
        "components": [
            {
                "ref": "U1",
                "value": "ESP32-WROOM-32",
                "footprint": "ESP32-WROOM-32",
                "type": "mcu",
                "rotation_deg": 0.0,
                "layer": "F.Cu",
                "pins": [
                    _mcu_pin("1", "3V3", "VCC", "power_in"),
                    _mcu_pin("2", "GND", "GND", "power_in"),
                    _mcu_pin("21", "GPIO21", "SDA", "bidirectional"),
                    _mcu_pin("22", "GPIO22", "SCL", "output"),
                    _mcu_pin("23", "GPIO23", "LED_D1", "output"),
                ],
            },
            {
                "ref": "J1",
                "value": "USB-C",
                "footprint": "USB_C_Receptacle",
                "type": "connector",
                "rotation_deg": 0.0,
                "layer": "F.Cu",
                "pins": [
                    _mcu_pin("1", "DP", "SDA", "bidirectional"),
                    _mcu_pin("2", "DN", "SCL", "input"),
                    _mcu_pin("4", "VBUS", "VCC", "power_out"),
                    _mcu_pin("5", "GND", "GND", "power_in"),
                ],
            },
            {
                "ref": "R1",
                "value": "1k",
                "footprint": "R_0402",
                "type": "passive",
                "rotation_deg": 0.0,
                "layer": "F.Cu",
                "pins": [
                    _mcu_pin("1", "1", "LED_D1", "passive"),
                    _mcu_pin("2", "2", "GND", "passive"),
                ],
            },
        ],
        "nets": [
            {"id": 1, "name": "VCC", "pins": ["U1.1", "J1.4"]},
            {"id": 2, "name": "GND", "pins": ["U1.2", "J1.5", "R1.2"]},
            {"id": 3, "name": "SDA", "pins": ["U1.21", "J1.1"]},
            {"id": 4, "name": "SCL", "pins": ["U1.22", "J1.2"]},
            {"id": 5, "name": "LED_D1", "pins": ["U1.23", "R1.1"]},
        ],
        "tracks": [],
        "footprints": {},
        "placements": [
            {"ref": "U1", "x_mm": 25.0, "y_mm": 20.0, "rotation_deg": 0.0,
             "layer": "F.Cu"},
            {"ref": "J1", "x_mm": 30.0, "y_mm": 40.0, "rotation_deg": 0.0,
             "layer": "F.Cu"},
            {"ref": "R1", "x_mm": 12.0, "y_mm": 12.0, "rotation_deg": 0.0,
             "layer": "F.Cu"},
        ],
        "rules": [{"name": "default", "minClearanceMm": 0.2,
                   "minTrackMm": 0.2}],
        "metadata": {},
    }


def _good_firmware() -> dict:
    return {
        "name": "esp32-i2c-demo",
        "mcu": {
            "family": "ESP32",
            "manufacturer": "Espressif",
            "model": "ESP32-WROOM-32",
            "architecture": "xtensa-lx6",
            "clock_hz_max": 240000000,
            "pins": [
                {"name": "GPIO21", "digital": True, "adc": False,
                 "pwm": False},
                {"name": "GPIO22", "digital": True, "adc": False,
                 "pwm": False},
                {"name": "GPIO23", "digital": True, "adc": False,
                 "pwm": False},
            ],
            "peripherals": [{"name": "I2C0", "kind": "i2c",
                             "instances": 1}],
            "toolchain": "xtensa-esp32-elf-g++",
        },
        "has_mcu": True,
        "clock_hz": 240000000,
        "pin_mappings": [
            {
                "mcu_pin": "GPIO21",
                "function": "i2c_sda",
                "peripheral": "I2C0",
                "direction": "inout",
                "electrical_mode": "digital",
                "pull": "up",
                "alt_function": "",
            },
            {
                "mcu_pin": "GPIO22",
                "function": "i2c_scl",
                "peripheral": "I2C0",
                "direction": "out",
                "electrical_mode": "digital",
                "pull": "up",
                "alt_function": "",
            },
            {
                "mcu_pin": "GPIO23",
                "function": "gpio_out",
                "peripheral": "",
                "direction": "out",
                "electrical_mode": "digital",
                "pull": "",
                "alt_function": "",
            },
        ],
        "requirements": [
            {"kind": "i2c_communication",
             "params": {"sda": "GPIO21", "scl": "GPIO22", "freq_hz": 400000}},
            {"kind": "write_gpio", "params": {"pin": "GPIO23"}},
        ],
        "sources": [],
        "build": {"profile": "debug", "arch": "xtensa-lx6",
                  "toolchain": "", "opt_level": "-O2",
                  "output_dir": "build"},
        "metadata": {},
    }


# A green C00 gate: one VALIDATED engine check per constituent artifact
# (status strings are the src/models/schemas.py ValidationLevel chain).
_ENGINE_CHECKS = [
    {"artifact": "pcb", "op": "validate", "status": "VALIDATED"},
    {"artifact": "firmware", "op": "validate", "status": "VERIFIED"},
    {"artifact": "cad", "op": "nest", "status": "VALIDATED"},
]
# A coherent power section: both board power nets classified (VCC rides
# power-typed pads on U1/J1; GND on U1/J1/R1), every rider metered,
# draws at 40-41% of capacity, and the MCU's 3.3 V rail present.
_POWER = {
    "rails": [
        {"net": "VCC", "class": "3.3V", "capacity_ma": 600.0,
         "draws": [
             {"ref": "U1", "draw_ma": 240.0},
             {"ref": "J1", "draw_ma": 0.0},
         ]},
        {"net": "GND", "class": "0V", "capacity_ma": 600.0,
         "draws": [
             {"ref": "U1", "draw_ma": 240.0},
             {"ref": "J1", "draw_ma": 0.0},
             {"ref": "R1", "draw_ma": 5.0},
         ]},
    ],
    "mcu_rail": {"class": "3.3V"},
}


def _good_record() -> object:
    return build_record(
        "good-design",
        pcb=_good_pcb(),
        firmware=_good_firmware(),
        holes=copy.deepcopy(_HOLES),
        connectors=copy.deepcopy(_CONNECTORS),
        enclosure=copy.deepcopy(_ENCLOSURE),
        power=copy.deepcopy(_POWER),
        engine_checks=copy.deepcopy(_ENGINE_CHECKS),
        # C11 needs the BOM; deriving it from the board is M21.1's own
        # decided builder, not a hand-built copy.
        bom=bom_from_pcb(_good_pcb()),
    )


def _check(checks: list, check_id: str):
    by_id = {result.check_id: result for result in checks}
    result = by_id[check_id]
    assert isinstance(result, CheckResult)
    assert all(result.check_id and result.name and result.status
               and result.reason for result in checks)
    return result


def test_good_record_passes_every_implemented_check() -> None:
    checks = run_checks(_good_record())
    assert [result.check_id for result in checks] == list(CHECK_ORDER)
    for result in checks:
        assert result.status == PASS, (
            f"{result.check_id} did not pass: {result.reason}"
        )
    by_id = {result.check_id: result for result in checks}
    assert "ESP32-WROOM-32" in by_id["C02"].reason
    c04 = by_id["C04"]
    assert c04.evidence["pins"], "C04 must carry its per-pin rows"
    assert c04.evidence["pins"][2]["note"].endswith("gpio_out'")
    # The report-level triad: a fully green run is VERIFIED.
    assert overall_status(checks) == OVERALL_VERIFIED
    assert status_counts(checks) == {
        PASS: len(CHECK_ORDER), WARN: 0, FAIL: 0,
    }


def test_missing_sections_fail_c01_and_cannot_verify_the_rest() -> None:
    record = build_record("incomplete", pcb=_good_pcb())
    checks = run_checks(record)
    c01 = _check(checks, "C01")
    assert c01.status == FAIL
    assert "firmware, holes, connectors, enclosure" in c01.reason
    assert c01.location == "firmware"
    for check_id in ("C02", "C03", "C04", "C05"):
        result = _check(checks, check_id)
        assert result.status == WARN
        assert result.reason.startswith("cannot_verify")
    # C01's FAIL dominates: the run cannot be VERIFIED or WARNING.
    assert overall_status(checks) == OVERALL_FAILED


def test_c02_exact_family_and_missing_matches() -> None:
    firmware = _good_firmware()

    # Exact part on both sides: PASS (covered by the good record too).
    assert _check(run_checks(_good_record()), "C02").status == PASS

    # Same family, different package marking: WARN.
    wrong_package = copy.deepcopy(firmware)
    wrong_package["mcu"]["model"] = "ESP32"
    wrong_package["mcu"]["family"] = "ESP32"
    c02 = _check(run_checks(build_record(
        "family-only", pcb=_good_pcb(), firmware=wrong_package,
        holes=_HOLES, connectors=_CONNECTORS, enclosure=_ENCLOSURE,
    )), "C02")
    assert c02.status == WARN
    assert "different packages" in c02.reason

    # A different MCU: FAIL.
    different = copy.deepcopy(firmware)
    different["mcu"]["model"] = "ESP32-C6"
    c02 = _check(run_checks(build_record(
        "wrong-mcu", pcb=_good_pcb(), firmware=different,
        holes=_HOLES, connectors=_CONNECTORS, enclosure=_ENCLOSURE,
    )), "C02")
    assert c02.status == FAIL
    assert "ESP32-C6" in c02.reason and "ESP32-WROOM-32" in c02.reason

    # No model named: cannot_verify, never a PASS.
    empty = copy.deepcopy(firmware)
    empty["mcu"] = {}
    c02 = _check(run_checks(build_record(
        "no-model", pcb=_good_pcb(), firmware=empty,
        holes=_HOLES, connectors=_CONNECTORS, enclosure=_ENCLOSURE,
    )), "C02")
    assert c02.status == WARN
    assert "firmware.mcu.model is empty" in c02.reason


def test_done_when_wrong_pin_fails_with_the_correct_reason() -> None:
    # The board's netlist differs from the firmware map by ONE pin: the
    # firmware drives i2c_sda on GPIO29, which does not exist on U1 at
    # all. C03 must FAIL, and the failure must name the number, the
    # function, and the part — not a stack trace, not a cascade.
    firmware = _good_firmware()
    firmware["pin_mappings"][0]["mcu_pin"] = "GPIO29"
    checks = run_checks(build_record(
        "wrong-pin", pcb=_good_pcb(), firmware=firmware,
        holes=_HOLES, connectors=_CONNECTORS, enclosure=_ENCLOSURE,
    ))
    c03 = _check(checks, "C03")
    assert c03.status == FAIL
    assert "'i2c_sda'" in c03.reason
    assert "'GPIO29'" in c03.reason
    assert "no pin by that name" in c03.reason
    assert c03.location == "U1.GPIO29"
    # The remaining pins still resolve; C03 aggregates the FAIL.
    assert c03.evidence["pins"][0]["status"] == FAIL
    assert c03.evidence["pins"][1]["status"] == PASS


def test_floating_driven_pin_warns_in_c03_and_fails_in_c04() -> None:
    # GPIO21 exists on the board but the pad rides no net. This is the
    # documented double-report: C03's pin-layer WARN plus C04's net-layer
    # FAIL, both naming GPIO21.
    pcb = _good_pcb()
    for pin in pcb["components"][0]["pins"]:
        if pin["name"] == "GPIO21":
            pin["net"] = ""
    checks = run_checks(build_record(
        "floating-pin", pcb=pcb, firmware=_good_firmware(),
        holes=_HOLES, connectors=_CONNECTORS, enclosure=_ENCLOSURE,
    ))
    c03 = _check(checks, "C03")
    assert c03.status == WARN
    assert "floating" in c03.reason and "GPIO21" in c03.reason
    c04 = _check(checks, "C04")
    assert c04.status == FAIL
    assert "GPIO21" in c04.reason and "leaves it unconnected" in c04.reason
    assert c04.location == "U1.GPIO21"


def test_undeclared_net_fails_c04() -> None:
    pcb = _good_pcb()
    for pin in pcb["components"][0]["pins"]:
        if pin["name"] == "GPIO21":
            pin["net"] = "SDA_BOGUS"
    c04 = _check(run_checks(build_record(
        "undeclared-net", pcb=pcb, firmware=_good_firmware(),
        holes=_HOLES, connectors=_CONNECTORS, enclosure=_ENCLOSURE,
    )), "C04")
    assert c04.status == FAIL
    assert "'SDA_BOGUS'" in c04.reason
    assert "declared in the board netlist" in c04.reason


def test_net_refs_must_include_the_driven_pad() -> None:
    pcb = _good_pcb()
    for pin in pcb["components"][0]["pins"]:
        if pin["name"] == "GPIO21":
            pin["net"] = "SWAP"
    pcb["nets"] = [
        net for net in pcb["nets"] if net["name"] != "SDA"
    ] + [{"id": 6, "name": "SWAP", "pins": ["U1.22", "J1.1"]}]
    # U1.21 is on SWAP, but SWAP's own ref list never mentions U1.21.
    c04 = _check(run_checks(build_record(
        "unrouted-pad", pcb=pcb, firmware=_good_firmware(),
        holes=_HOLES, connectors=_CONNECTORS, enclosure=_ENCLOSURE,
    )), "C04")
    assert c04.status == FAIL
    assert "'SWAP'" in c04.reason and "U1.21" in c04.reason
    assert "not the driven pad" in c04.reason


def test_single_attachment_net_warns_c04_and_c05() -> None:
    pcb = _good_pcb()
    pcb["nets"] = [
        net for net in pcb["nets"] if net["name"] != "SDA"
    ] + [{"id": 3, "name": "SDA", "pins": ["U1.21"]}]
    checks = run_checks(build_record(
        "dangling-net", pcb=pcb, firmware=_good_firmware(),
        holes=_HOLES, connectors=_CONNECTORS, enclosure=_ENCLOSURE,
    ))
    c04 = _check(checks, "C04")
    assert c04.status == WARN
    assert "single attachment" in c04.reason
    c05 = _check(checks, "C05")
    assert c05.status == WARN
    assert "counterpart incomplete" in c05.reason and "SDA" in c05.reason


def test_peripheral_without_any_counterpart_fails_c05() -> None:
    # Cut the connector off — honestly: the component AND its ref
    # entries leave both I2C nets, so the netlist itself shows I2C0's
    # nets ending at the MCU pad alone.
    pcb = _good_pcb()
    pcb["components"] = [
        c for c in pcb["components"] if c["ref"] != "J1"
    ]
    for net in pcb["nets"]:
        if net["name"] in ("SDA", "SCL", "VCC"):
            net["pins"] = [
                r for r in net["pins"] if not r.startswith("J1.")
            ]
    c05 = _check(run_checks(build_record(
        "no-counterpart", pcb=pcb, firmware=_good_firmware(),
        holes=_HOLES, connectors=_CONNECTORS, enclosure=_ENCLOSURE,
    )), "C05")
    assert c05.status == FAIL
    assert "I2C0" in c05.reason and "no counterpart on the board" in c05.reason
    assert "SDA" in c05.reason


def test_gpio_pins_carry_no_peripheral_contract() -> None:
    # A firmware fragment whose only mappings are plain GPIO still runs
    # the net checks but has no peripheral contract to violate.
    firmware = _good_firmware()
    firmware["pin_mappings"] = [
        {
            "mcu_pin": "GPIO23",
            "function": "gpio_out",
            "peripheral": "",
            "direction": "out",
            "electrical_mode": "digital",
            "pull": "",
            "alt_function": "",
        }
    ]
    checks = run_checks(build_record(
        "gpio-only", pcb=_good_pcb(), firmware=firmware,
        holes=_HOLES, connectors=_CONNECTORS, enclosure=_ENCLOSURE,
    ))
    assert _check(checks, "C03").status == PASS
    assert _check(checks, "C04").status == PASS
    c05 = _check(checks, "C05")
    assert c05.status == WARN
    assert "names no peripherals" in c05.reason


def test_checks_without_an_mcu_component_refuse_honestly() -> None:
    pcb = _good_pcb()
    pcb["components"] = [c for c in pcb["components"] if c["type"] != "mcu"]
    checks = run_checks(build_record(
        "no-mcu-part", pcb=pcb, firmware=_good_firmware(),
        holes=_HOLES, connectors=_CONNECTORS, enclosure=_ENCLOSURE,
    ))
    assert _check(checks, "C02").status == FAIL
    for check_id in ("C03", "C04", "C05"):
        result = _check(checks, check_id)
        assert result.status == WARN
        assert "no MCU-typed component" in result.reason
        assert result.location == "pcb.components"


def test_empty_firmware_pin_map_cannot_verify_the_pin_checks() -> None:
    firmware = _good_firmware()
    firmware["pin_mappings"] = []
    checks = run_checks(build_record(
        "no-pins", pcb=_good_pcb(), firmware=firmware,
        holes=_HOLES, connectors=_CONNECTORS, enclosure=_ENCLOSURE,
    ))
    for check_id in ("C03", "C04"):
        result = _check(checks, check_id)
        assert result.status == WARN
        assert result.reason.startswith("cannot_verify")
    c05 = _check(checks, "C05")
    assert c05.status == WARN and "names no peripherals" in c05.reason


def test_check_results_always_carry_location_and_evidence() -> None:
    checks = run_checks(_good_record())
    for result in checks:
        assert isinstance(result.location, str)
        assert isinstance(result.evidence, dict) and result.evidence
        assert result.status in (PASS, WARN, FAIL)
