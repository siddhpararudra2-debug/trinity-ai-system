"""The cross-check fixture designs (M20.6): 3 good + 5 broken records.

A fixture is a complete design record built with the same section
grammar the engines really emit (`decided.md` convention 1: the
firmware ``PinMapping.mcu_pin`` ↔ board ``Pin.name`` match key, `Net.pins`
as the connectivity truth). The good ones must produce an all-PASS run
(overall VERIFIED); every broken one must fail **exactly one** check —
its target is encoded in the name (``break_c03_unknown_pin`` fails C03)
— with no WARNs and no cascades into other checks (decided.md: "the
wrong-pin fixture ... does not need to blanket-fail everything; cascades
are diagnosed, not shipped").

The poisons live next to their fixtures so the intent is reviewable:

* ``break_c03_unknown_pin`` — the firmware maps the plain-GPIO function
  ``gpio_out`` to ``GPIO39``, which the board's MCU has no pin by.
  Poisoning a *peripheral-less* mapping on purpose: an unmapped I2C pin
  would WARN in C05 too, which is a cascade, not this fixture's point.
* ``break_c04_drives_undeclared_net`` — the board pads GPIO23 to
  ``LED_OUT``, a net the board netlist does not declare (the Net.pins
  source of truth says the driven pad rides no named net).
* ``break_c06_two_classes_one_net`` — VCC gets a second rail entry
  declaring class ``5.0V``; the entry mirrors the original draws so C07
  weighs both rails independently and stays green.
* ``break_c07_rail_over_budget`` — U1 draws 620 mA of VCC's 600 mA.
* ``break_c08_board_oversized`` — the cavity shrinks to 44 mm wide;
  the board sticks out of the cavity's right edge by 9 mm.

Run ``python -m src.engines.crosscheck.fixtures`` to (re)write the JSON
files under ``data/fixtures/`` and print the per-fixture verdict table.
The files are the committed artifacts; the sync test proves they always
equal what these builders produce.
"""

from __future__ import annotations

import copy
import json
import sys
from collections.abc import Callable
from pathlib import Path

from src.core.config import REPO_ROOT
from src.engines.crosscheck.check import run_checks
from src.engines.crosscheck.record import DesignRecord, build_record, load_record

__all__ = [
    "build_fixture",
    "fixture_names",
    "fixtures_root",
    "load_fixture",
    "write_fixtures",
]

_FIXTURES_RELATIVE = Path("data") / "fixtures"


def fixtures_root(repo_root: Path | None = None) -> Path:
    """Where the fixture JSON files live (``<repo>/data/fixtures/``)."""
    return (repo_root or REPO_ROOT) / _FIXTURES_RELATIVE


# --- the shared good design (an ESP32 I2C + LED board, USB-C, boxed) ------

def _pin(number: str, name: str, net: str, electrical: str) -> dict:
    return {
        "number": number,
        "name": name,
        "electrical": electrical,
        "pos_mm": {"x": 0.0, "y": 0.0},
        "net": net,
    }


def _base_pcb() -> dict:
    return {
        "board": {
            "width_mm": 50.0, "height_mm": 40.0, "thickness_mm": 1.6,
            "origin_mm": {"x": 0.0, "y": 0.0},
            "layers": ["F.Cu", "B.Cu"],
        },
        "components": [
            {
                "ref": "U1", "value": "ESP32-WROOM-32",
                "footprint": "ESP32-WROOM-32", "type": "mcu",
                "rotation_deg": 0.0, "layer": "F.Cu",
                "pins": [
                    _pin("1", "3V3", "VCC", "power_in"),
                    _pin("2", "GND", "GND", "power_in"),
                    _pin("21", "GPIO21", "SDA", "bidirectional"),
                    _pin("22", "GPIO22", "SCL", "output"),
                    _pin("23", "GPIO23", "LED_D1", "output"),
                ],
            },
            {
                "ref": "J1", "value": "USB-C",
                "footprint": "USB_C_Receptacle", "type": "connector",
                "rotation_deg": 0.0, "layer": "F.Cu",
                "pins": [
                    _pin("1", "DP", "SDA", "bidirectional"),
                    _pin("2", "DN", "SCL", "input"),
                    _pin("4", "VBUS", "VCC", "power_out"),
                    _pin("5", "GND", "GND", "power_in"),
                ],
            },
            {
                "ref": "R1", "value": "1k", "footprint": "R_0402",
                "type": "passive", "rotation_deg": 0.0, "layer": "F.Cu",
                "pins": [
                    _pin("1", "1", "LED_D1", "passive"),
                    _pin("2", "2", "GND", "passive"),
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


def _base_firmware() -> dict:
    return {
        "name": "esp32-i2c-led",
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
                "mcu_pin": "GPIO21", "function": "i2c_sda",
                "peripheral": "I2C0", "direction": "inout",
                "electrical_mode": "digital", "pull": "up",
                "alt_function": "",
            },
            {
                "mcu_pin": "GPIO22", "function": "i2c_scl",
                "peripheral": "I2C0", "direction": "out",
                "electrical_mode": "digital", "pull": "up",
                "alt_function": "",
            },
            {
                "mcu_pin": "GPIO23", "function": "gpio_out",
                "peripheral": "", "direction": "out",
                "electrical_mode": "digital", "pull": "",
                "alt_function": "",
            },
        ],
        "requirements": [
            {"kind": "i2c_communication",
             "params": {"sda": "GPIO21", "scl": "GPIO22",
                        "freq_hz": 400000}},
            {"kind": "write_gpio", "params": {"pin": "GPIO23"}},
        ],
        "sources": [],
        "build": {"profile": "debug", "arch": "xtensa-lx6",
                  "toolchain": "", "opt_level": "-O2",
                  "output_dir": "build"},
        "metadata": {},
    }


def _base_enclosure() -> dict:
    """A 50x40 board in a 60x50 cavity: tightest edge 3.00 mm (left)."""
    return {
        "cavity_mm": {"width_mm": 60.0, "height_mm": 50.0,
                      "depth_mm": 20.0,
                      "origin_mm": {"x": -3.0, "y": -4.0}},
        "standoffs": [{"id": "S1", "pos_mm": {"x": 2.5, "y": 2.5}}],
        "cutouts": [{"id": "C1", "pos_mm": {"x": 29.0, "y": 39.0},
                     "width_mm": 12.0, "height_mm": 7.0}],
    }


def _base_sections() -> dict[str, object]:
    return {
        "pcb": _base_pcb(),
        "firmware": _base_firmware(),
        "holes": [{"ref": "H1", "pos_mm": {"x": 2.5, "y": 2.5},
                   "diameter_mm": 3.0}],
        "connectors": [{"ref": "J1", "pos_mm": {"x": 30.0, "y": 40.0},
                        "width_mm": 10.0, "height_mm": 5.0,
                        "kind": "usb_c"}],
        "enclosure": _base_enclosure(),
        # Every rider of each rail is metered; both rails sit at about
        # 40% of capacity (601/600 would fail C07 — 240/600 here PASSes).
        "power": {
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
        },
        "engine_checks": [
            {"artifact": "pcb", "op": "validate", "status": "VALIDATED"},
            {"artifact": "firmware", "op": "validate",
             "status": "VERIFIED"},
            {"artifact": "cad", "op": "nest", "status": "VALIDATED"},
        ],
    }


def _record(record_id: str, **section_overrides: object) -> DesignRecord:
    spec = _base_sections()
    for key, value in section_overrides.items():
        spec[key] = copy.deepcopy(value)
    return build_record(record_id, **spec)  # type: ignore[arg-type]


# --- the three good designs ----------------------------------------------

def _good_esp32_i2c() -> DesignRecord:
    """The canonical green design: ESP32, I2C0 wired, LED on GPIO23."""
    return _record("good_esp32_i2c")


def _good_esp32_regulator() -> DesignRecord:
    """A two-rail board: USB 5V in, AMS1117 LDO makes VCC.

    The firmware half matches the shared good design (same MCU, same
    GPIO jobs); the board, the rails and the counterpart connector J2
    (the QWIIC-style I2C tail) are what differ.
    """
    regulator_pcb = _base_pcb()
    regulator_pcb["components"] = [
        {
            "ref": "U1", "value": "ESP32-WROOM-32",
            "footprint": "ESP32-WROOM-32", "type": "mcu",
            "rotation_deg": 0.0, "layer": "F.Cu",
            "pins": [
                _pin("1", "3V3", "VCC", "power_in"),
                _pin("2", "GND", "GND", "power_in"),
                _pin("21", "GPIO21", "SDA", "bidirectional"),
                _pin("22", "GPIO22", "SCL", "output"),
                _pin("23", "GPIO23", "LED_D1", "output"),
            ],
        },
        {
            "ref": "U2", "value": "AMS1117-3.3", "footprint": "SOT-223",
            "type": "regulator", "rotation_deg": 0.0, "layer": "F.Cu",
            "pins": [
                _pin("3", "VIN", "5V", "power_in"),
                _pin("2", "VOUT", "VCC", "power_out"),
                _pin("1", "GND", "GND", "power_in"),
            ],
        },
        {
            "ref": "J1", "value": "USB-C",
            "footprint": "USB_C_Receptacle", "type": "connector",
            "rotation_deg": 0.0, "layer": "F.Cu",
            "pins": [
                _pin("4", "VBUS", "5V", "power_out"),
                _pin("5", "GND", "GND", "power_in"),
            ],
        },
        {
            "ref": "J2", "value": "QWIIC", "footprint": "JST_SH_4",
            "type": "connector", "rotation_deg": 0.0, "layer": "F.Cu",
            "pins": [
                _pin("1", "SDA", "SDA", "bidirectional"),
                _pin("2", "SCL", "SCL", "bidirectional"),
                _pin("3", "GND", "GND", "power_in"),
            ],
        },
        {
            "ref": "R1", "value": "1k", "footprint": "R_0402",
            "type": "passive", "rotation_deg": 0.0, "layer": "F.Cu",
            "pins": [
                _pin("1", "1", "LED_D1", "passive"),
                _pin("2", "2", "GND", "passive"),
            ],
        },
    ]
    regulator_pcb["nets"] = [
        {"id": 1, "name": "VCC", "pins": ["U1.1", "U2.2"]},
        {"id": 2, "name": "GND",
         "pins": ["U1.2", "U2.1", "J1.5", "J2.3", "R1.2"]},
        {"id": 3, "name": "5V", "pins": ["U2.3", "J1.4"]},
        {"id": 4, "name": "SDA", "pins": ["U1.21", "J2.1"]},
        {"id": 5, "name": "SCL", "pins": ["U1.22", "J2.2"]},
        {"id": 6, "name": "LED_D1", "pins": ["U1.23", "R1.1"]},
    ]
    power = {
        "rails": [
            {"net": "VCC", "class": "3.3V", "capacity_ma": 600.0,
             "draws": [
                 {"ref": "U1", "draw_ma": 240.0},
                 {"ref": "U2", "draw_ma": 0.0},
             ]},
            {"net": "5V", "class": "5.0V", "capacity_ma": 1000.0,
             "draws": [
                 {"ref": "U2", "draw_ma": 250.0},
                 {"ref": "J1", "draw_ma": 0.0},
             ]},
            {"net": "GND", "class": "0V", "capacity_ma": 1000.0,
             "draws": [
                 {"ref": "U1", "draw_ma": 240.0},
                 {"ref": "U2", "draw_ma": 0.0},
                 {"ref": "J1", "draw_ma": 0.0},
                 {"ref": "J2", "draw_ma": 0.0},
                 {"ref": "R1", "draw_ma": 5.0},
             ]},
        ],
        "mcu_rail": {"class": "3.3V"},
    }
    return _record(
        "good_esp32_regulator", pcb=regulator_pcb, power=power
    )


def _good_esp32_i2c_roomy() -> DesignRecord:
    """The shared design in a roomier lid: 80x60x30 cavity."""
    enclosure = _base_enclosure()
    enclosure["cavity_mm"] = {
        "width_mm": 80.0, "height_mm": 60.0, "depth_mm": 30.0,
        "origin_mm": {"x": -8.0, "y": -8.0},
    }
    return _record("good_esp32_i2c_roomy", enclosure=enclosure)


# --- the five one-check-broken designs ------------------------------------

def _break_c03_unknown_pin() -> DesignRecord:
    firmware = _base_firmware()
    firmware["pin_mappings"][2]["mcu_pin"] = "GPIO39"
    return _record("break_c03_unknown_pin", firmware=firmware)


def _break_c04_drives_undeclared_net() -> DesignRecord:
    pcb = _base_pcb()
    for component in pcb["components"]:
        for pin in component["pins"]:
            if pin["name"] == "GPIO23":
                pin["net"] = "LED_OUT"
    return _record("break_c04_drives_undeclared_net", pcb=pcb)


def _break_c06_two_classes_one_net() -> DesignRecord:
    power = copy.deepcopy(_base_sections()["power"])
    duplicate_rail = copy.deepcopy(power["rails"][0])  # VCC, same draws
    duplicate_rail["class"] = "5.0V"
    power["rails"].append(duplicate_rail)
    return _record("break_c06_two_classes_one_net", power=power)


def _break_c07_rail_over_budget() -> DesignRecord:
    power = copy.deepcopy(_base_sections()["power"])
    power["rails"][0]["draws"][0]["draw_ma"] = 620.0  # U1 on VCC
    return _record("break_c07_rail_over_budget", power=power)


def _break_c08_board_oversized() -> DesignRecord:
    enclosure = copy.deepcopy(_base_sections()["enclosure"])
    enclosure["cavity_mm"]["width_mm"] = 44.0
    return _record("break_c08_board_oversized", enclosure=enclosure)


_FIXTURE_BUILDERS: dict[str, Callable[[], DesignRecord]] = {
    "good_esp32_i2c": _good_esp32_i2c,
    "good_esp32_regulator": _good_esp32_regulator,
    "good_esp32_i2c_roomy": _good_esp32_i2c_roomy,
    "break_c03_unknown_pin": _break_c03_unknown_pin,
    "break_c04_drives_undeclared_net": _break_c04_drives_undeclared_net,
    "break_c06_two_classes_one_net": _break_c06_two_classes_one_net,
    "break_c07_rail_over_budget": _break_c07_rail_over_budget,
    "break_c08_board_oversized": _break_c08_board_oversized,
}


def fixture_names() -> list[str]:
    """All fixture ids, goods first. Every broken fixture's name parses
    as ``break_<check-id-in-lowercase>_<slug>``."""
    return list(_FIXTURE_BUILDERS)


def build_fixture(name: str) -> DesignRecord:
    """Build one fixture in memory (a DesignRecord, stamps left unset
    until saved)."""
    if name not in _FIXTURE_BUILDERS:
        raise KeyError(f"unknown fixture '{name}'")
    return _FIXTURE_BUILDERS[name]()


def load_fixture(name: str, root: Path | None = None) -> DesignRecord:
    """Load the committed fixture file back through the record loader."""
    return load_record(name, records_root_param=fixtures_root(root))


def write_fixtures(root: Path | None = None) -> list[Path]:
    """Write every fixture as ``<root>/<name>/record.json`` and return
    the paths. Stamps stay unset: the files are pure data, byte-stable
    across regenerations."""
    base = fixtures_root(root)
    written: list[Path] = []
    for name, build in _FIXTURE_BUILDERS.items():
        fixture_dir = base / name
        fixture_dir.mkdir(parents=True, exist_ok=True)
        path = fixture_dir / "record.json"
        path.write_text(
            json.dumps(build().to_dict(), indent=2) + "\n",
            encoding="utf-8",
        )
        written.append(path)
    return written


def main(argv: list[str] | None = None) -> int:
    """The M20.6 one command: (re)write the fixture files and print the
    per-fixture verdict table so the set's truth is visible at a glance."""
    args = argv if argv is not None else sys.argv[1:]
    root = Path(args[0]) if args else None
    paths = write_fixtures(root)
    header = (
        f"{'fixture':<34} {'overall':<9} {'PASS':>4} {'WARN':>5}"
        f" {'FAIL':>5}  failing"
    )
    print("wrote", len(paths), "fixture records under", fixtures_root(root))
    print(header)
    print("-" * len(header))
    for name, build in _FIXTURE_BUILDERS.items():
        checks = run_checks(build())
        counts = {
            status: sum(1 for c in checks if c.status == status)
            for status in ("PASS", "WARN", "FAIL")
        }
        failing = ", ".join(
            c.check_id for c in checks if c.status == "FAIL"
        )
        overall = (
            "FAILED" if counts["FAIL"] else
            "WARNING" if counts["WARN"] else
            "VERIFIED"
        )
        print(
            f"{name:<34} {overall:<9} {counts['PASS']:>4} "
            f"{counts['WARN']:>5} {counts['FAIL']:>5}  {failing}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
