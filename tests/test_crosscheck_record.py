"""M20.1: the design record — schema, builder, atomic storage.

The contract under test (`decided.md`, Phase 0 + `src/engines/crosscheck/
record.py`'s docstring): the builder stores what it is given and invents
nothing (absence stays absence for check C01 to FAIL with the section
named), storage is atomic write-then-replace under
``TRINITY_STORAGE_ROOT/projects/<id>/record.json``, and a stored record
this build cannot read is refused with the path named — never guessed.

The fragments here are IR-shaped with the native serializers' real wire
names (``PcbDesign``: board/components/nets/tracks/footprints/placements/
rules/metadata, nested struct fields snake_case — ``rotation_deg``,
``pos_mm``; ``FirmwareProject``: name/mcu/has_mcu/clock_hz/pin_mappings/
requirements/sources/build/metadata, ``mcu_pin``, ``electrical_mode``,
directions ``in``/``out``/``inout``) — the builder's
contract is "store what the engines answered", so the tests feed it
exactly that shape.
"""

from __future__ import annotations

import copy
from datetime import datetime
from pathlib import Path

import pytest

from src.engines.crosscheck.record import (
    REQUIRED_SECTIONS,
    DesignRecord,
    DesignRecordError,
    build_record,
    load_record,
    record_path,
    save_record,
)

PCB_FRAGMENT: dict = {
    "board": {
        "width_mm": 50.0,
        "height_mm": 40.0,
        "thickness_mm": 1.6,
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
                {
                    "number": "1",
                    "name": "3V3",
                    "electrical": "power_in",
                    "pos_mm": {"x": 0.0, "y": 0.0},
                    "net": "VCC",
                },
                {
                    "number": "2",
                    "name": "GND",
                    "electrical": "power_in",
                    "pos_mm": {"x": 1.0, "y": 0.0},
                    "net": "GND",
                },
            ],
        }
    ],
    "nets": [
        {"id": 1, "name": "VCC", "pins": ["U1.1"]},
        {"id": 2, "name": "GND", "pins": ["U1.2"]},
    ],
    "tracks": [],
    "footprints": {},
    "placements": [
        {"ref": "U1", "x_mm": 25.0, "y_mm": 20.0, "rotation_deg": 0.0,
         "layer": "F.Cu"}
    ],
    "rules": [{"name": "default", "minClearanceMm": 0.2, "minTrackMm": 0.2}],
    "metadata": {},
}

FIRMWARE_FRAGMENT: dict = {
    "name": "esp32-demo",
    "mcu": {
        "family": "ESP32",
        "manufacturer": "Espressif",
        "model": "ESP32",
        "architecture": "xtensa-lx6",
        "clock_hz_max": 240000000,
        "pins": [{"name": "GPIO21", "digital": True, "adc": False,
                  "pwm": False}],
        "peripherals": [{"name": "I2C0", "kind": "i2c", "instances": 1}],
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
            "pull": "",
            "alt_function": "",
        }
    ],
    "requirements": [
        {"kind": "i2c_communication", "params": {"sda": "GPIO21",
                                                 "scl": "GPIO22"}}
    ],
    "sources": [],
    "build": {
        "profile": "debug",
        "arch": "xtensa-lx6",
        "toolchain": "",
        "opt_level": "-O2",
        "output_dir": "build",
    },
    "metadata": {},
}

ENCLOSURE: dict = {
    "cavity_mm": {"width_mm": 60.0, "height_mm": 50.0, "depth_mm": 20.0},
    "standoffs": [{"id": "S1", "pos_mm": {"x": 5.0, "y": 5.0}}],
    "cutouts": [
        {"id": "C1", "pos_mm": {"x": 30.0, "y": 40.0},
         "width_mm": 10.0, "height_mm": 5.0}
    ],
}
HOLES: list = [{"ref": "H1", "pos_mm": {"x": 5.0, "y": 5.0},
                "diameter_mm": 3.0}]
CONNECTORS: list = [{"ref": "J1", "pos_mm": {"x": 30.0, "y": 40.0},
                     "width_mm": 10.0, "height_mm": 5.0, "kind": "usb_c"}]
BOM: dict = {"lines": [{"ref": "U1", "part": "ESP32-WROOM-32",
                        "value": "ESP32", "quantity": 1,
                        "package": "WROOM"}]}


def _full_kwargs() -> dict:
    return {
        "pcb": PCB_FRAGMENT,
        "firmware": FIRMWARE_FRAGMENT,
        "holes": HOLES,
        "connectors": CONNECTORS,
        "enclosure": ENCLOSURE,
        "bom": BOM,
    }


def test_build_stores_only_what_is_given() -> None:
    record = build_record(
        "snap", pcb=PCB_FRAGMENT, firmware=FIRMWARE_FRAGMENT
    )
    assert record.pcb is not None and record.firmware is not None
    assert record.holes is None
    assert record.connectors is None
    assert record.enclosure is None
    assert record.bom is None
    # The invented-nothing truth, visible before any check runs.
    assert record.absent_required_sections() == [
        "holes", "connectors", "enclosure"
    ]


def test_full_record_reports_no_absent_sections() -> None:
    record = build_record("snap", **_full_kwargs())
    assert record.absent_required_sections() == []
    for name in REQUIRED_SECTIONS + ("bom",):
        assert getattr(record, name) is not None


def test_fragments_are_deep_copied_at_build() -> None:
    pcb = copy.deepcopy(PCB_FRAGMENT)
    record = build_record("snap", pcb=pcb)
    pcb["components"][0]["ref"] = "MUTATED"
    assert record.pcb is not None  # for the type checker
    assert record.pcb["components"][0]["ref"] == "U1"


@pytest.mark.parametrize(
    "bad_id",
    ["", "../evil", "a/b", "a\\b", ".lead", "x" * 65, "sp ace"],
)
def test_bad_record_ids_refuse(bad_id: str) -> None:
    with pytest.raises(DesignRecordError, match="invalid design record id"):
        build_record(bad_id)


@pytest.mark.parametrize("good_id", ["demo", "D4d-1_2.3", "x" * 64])
def test_good_record_ids_pass(good_id: str) -> None:
    assert build_record(good_id).record_id == good_id


def test_save_load_roundtrip(tmp_path: Path) -> None:
    root = tmp_path / "projects"
    record = build_record("demo", **_full_kwargs())
    path = save_record(record, root)
    assert path == record_path("demo", root)
    assert path == root / "demo" / "record.json"
    loaded = load_record("demo", root)
    assert loaded == record
    assert loaded.absent_required_sections() == []
    assert loaded.pcb["components"][0]["ref"] == "U1"
    assert loaded.firmware["pin_mappings"][0]["mcu_pin"] == "GPIO21"
    assert loaded.bom["lines"][0]["part"] == "ESP32-WROOM-32"


def test_partial_record_roundtrip_stays_honestly_partial(
    tmp_path: Path,
) -> None:
    root = tmp_path / "projects"
    record = build_record("demo", pcb=PCB_FRAGMENT, firmware=FIRMWARE_FRAGMENT)
    save_record(record, root)
    loaded = load_record("demo", root)
    for name in ("holes", "connectors", "enclosure", "bom"):
        assert getattr(loaded, name) is None
    assert loaded.absent_required_sections() == [
        "holes", "connectors", "enclosure"
    ]


def test_save_is_atomic_and_creates_dirs(tmp_path: Path) -> None:
    root = tmp_path / "projects"
    record = build_record("demo", pcb=PCB_FRAGMENT)
    save_record(record, root)
    assert sorted(p.name for p in root.glob("demo/*")) == ["record.json"]
    # A re-save replaces the file in place and still leaves no tmp flotsam.
    save_record(record, root)
    assert sorted(p.name for p in root.glob("demo/*")) == ["record.json"]
    assert load_record("demo", root).record_id == "demo"


def test_created_at_preserved_and_updated_at_advances(
    tmp_path: Path,
) -> None:
    root = tmp_path / "projects"
    record = build_record("demo", pcb=PCB_FRAGMENT)
    save_record(record, root)
    first_created = record.created_at
    first_updated = record.updated_at
    assert first_created is not None and first_updated is not None
    assert record.app_version  # stamped from the real build version
    save_record(record, root)
    assert record.created_at == first_created
    assert datetime.fromisoformat(first_updated) <= datetime.fromisoformat(
        record.updated_at
    )


def test_load_unknown_record_refuses_with_path(tmp_path: Path) -> None:
    root = tmp_path / "projects"
    with pytest.raises(DesignRecordError, match="no design record named"):
        load_record("ghost", root)


def test_load_corrupt_record_refuses(tmp_path: Path) -> None:
    root = tmp_path / "projects"
    (root / "bad").mkdir(parents=True)
    (root / "bad" / "record.json").write_text("{ not json", encoding="utf-8")
    with pytest.raises(DesignRecordError, match="refusing to guess"):
        load_record("bad", root)


def test_load_refuses_a_non_object_record(tmp_path: Path) -> None:
    root = tmp_path / "projects"
    (root / "array").mkdir(parents=True)
    (root / "array" / "record.json").write_text("[]", encoding="utf-8")
    with pytest.raises(DesignRecordError, match="is a JSON object"):
        load_record("array", root)


def test_from_dict_refuses_future_schema_version() -> None:
    record = build_record("demo", pcb=PCB_FRAGMENT)
    data = record.to_dict()
    data["record_schema_version"] = 99
    with pytest.raises(DesignRecordError, match="record_schema_version 99"):
        DesignRecord.from_dict(data)


def test_from_dict_refuses_boolean_schema_version() -> None:
    record = build_record("demo", pcb=PCB_FRAGMENT)
    data = record.to_dict()
    data["record_schema_version"] = True  # bool is not an honest int here
    with pytest.raises(
        DesignRecordError, match="no integer record_schema_version"
    ):
        DesignRecord.from_dict(data)


def test_from_dict_refuses_unknown_keys() -> None:
    record = build_record("demo", pcb=PCB_FRAGMENT)
    data = record.to_dict()
    data["mystery_field"] = 1
    with pytest.raises(DesignRecordError, match="mystery_field"):
        DesignRecord.from_dict(data)


def test_from_dict_refuses_missing_record_id() -> None:
    data = build_record("demo", pcb=PCB_FRAGMENT).to_dict()
    data.pop("record_id")
    with pytest.raises(DesignRecordError, match="invalid design record id"):
        DesignRecord.from_dict(data)


@pytest.mark.parametrize(
    ("field", "bad_value"),
    [
        ("pcb", "not a dict"),
        ("firmware", []),
        ("enclosure", 7),
        ("bom", []),
        ("holes", {"wrongly": "an object"}),
        ("connectors", "x"),
    ],
)
def test_section_shape_refusals(field: str, bad_value: object) -> None:
    with pytest.raises(DesignRecordError, match=f"section '{field}'"):
        build_record("demo", **{field: bad_value})  # type: ignore[arg-type]
    data = build_record("demo", pcb=PCB_FRAGMENT).to_dict()
    data[field] = bad_value
    with pytest.raises(DesignRecordError, match=f"section '{field}'"):
        DesignRecord.from_dict(data)


def test_stamp_fields_must_be_strings_or_absent() -> None:
    data = build_record("demo", pcb=PCB_FRAGMENT).to_dict()
    data["updated_at"] = 42
    with pytest.raises(DesignRecordError, match="updated_at"):
        DesignRecord.from_dict(data)


def test_bom_stays_optional_until_phase_2(tmp_path: Path) -> None:
    root = tmp_path / "projects"
    kwargs = _full_kwargs()
    kwargs.pop("bom")
    record = build_record("demo", **kwargs)
    assert record.bom is None
    assert record.absent_required_sections() == []
    save_record(record, root)
    assert load_record("demo", root).absent_required_sections() == []
