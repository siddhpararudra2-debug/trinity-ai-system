"""M21.3: snapshots — the history is every state a save displaced.

`decided.md` convention 10: an overwrite save first snapshots the
displaced state (reason `save`), refusing to destroy a stored record
it cannot load back; a restore snapshots what it displaces (reason
`restore`) and writes the chosen snapshot's record verbatim — stamps
included — so after generate, change, restore the BOM matches the
generated design again. The compare diffs two snapshots across the
decided dimensions (geometry, nets, pins, firmware), naming changed
parts and quoting both values.
"""

from __future__ import annotations

import dataclasses
import json
from pathlib import Path

import pytest

from src.engines.crosscheck.bom import load_bom_csv
from src.engines.crosscheck.bom import bom_from_pcb, export_bom_csv
from src.engines.crosscheck import record as record_module
from src.engines.crosscheck.record import (
    DesignRecordError,
    build_record,
    load_record,
)
from src.engines.crosscheck.snapshots import (
    ABSENT_PIN_REF,
    compare_snapshots,
    list_snapshots,
    load_snapshot,
    restore_snapshot,
    snapshot_path,
    snapshots_dir,
    take_snapshot,
)


@pytest.fixture()
def _record_storage(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> Path:
    """Point the record module's storage seam at a temp root — the
    same frozen-Settings replace seam every record-touching test uses."""
    monkeypatch.setattr(
        record_module,
        "settings",
        dataclasses.replace(record_module.settings, storage_root=tmp_path),
    )
    return tmp_path


def _v1_pcb() -> dict:
    return {
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
                "value": "ESP32",
                "footprint": "ESP32-WROOM",
                "pins": [{"pin": "1", "net": "VCC"}],
            },
            {
                "ref": "R1",
                "value": "1k",
                "footprint": "R_0402",
                "pins": [],
            },
        ],
        "nets": [{"name": "VCC", "pads": ["U1.1", "C1.1"]}],
        "tracks": [],
        "footprints": {},
        "placements": [],
        "rules": [],
        "metadata": {},
    }


def _v2_pcb() -> dict:
    """A genuine change: wider board, new net, R1 gains a pin AND a
    new value — the change the restore story undoes."""
    pcb = json.loads(json.dumps(_v1_pcb()))  # deep copy
    pcb["board"]["width_mm"] = 60.0
    pcb["components"][1]["value"] = "2k2"
    pcb["components"][1]["pins"] = [{"pin": "1", "net": "VCC"}]
    pcb["nets"].append({"name": "LED_OUT", "pads": ["U1.15", "R1.1"]})
    return pcb


def _holes() -> list[dict]:
    return [
        {"ref": "H1", "pos_mm": {"x": 2.5, "y": 2.5}, "diameter_mm": 3.0}
    ]


# --- the displacement history ----------------------------------------------


def test_first_save_displaces_nothing_and_the_history_is_empty(
    _record_storage: Path,
) -> None:
    record_module.save_record(
        build_record("snap-design", pcb=_v1_pcb())
    )
    assert list_snapshots("snap-design") == []


def test_an_overwrite_save_snapshots_the_displaced_state(
    _record_storage: Path,
) -> None:
    v1 = build_record(
        "snap-design",
        pcb=_v1_pcb(),
        firmware={"mcu_pin": {}},
        holes=_holes(),
    )
    record_module.save_record(v1)
    v2 = build_record("snap-design", pcb=_v2_pcb())
    record_module.save_record(v2)

    rows = list_snapshots("snap-design")
    assert len(rows) == 1
    row = rows[0]
    assert row["snapshot_id"] == 1
    assert row["reason"] == "save"
    assert row["created_at"] == v1.created_at  # the displaced stamps
    assert row["updated_at"] == v1.updated_at

    # The snapshot carries the displaced state verbatim, and the live
    # record is the new one.
    payload = load_snapshot("snap-design", 1)
    assert payload["record"]["pcb"] == _v1_pcb()
    assert payload["record"]["created_at"] == v1.created_at
    assert load_record("snap-design").pcb == _v2_pcb()


def test_snapshot_ids_never_renumber_history(
    _record_storage: Path,
) -> None:
    record_module.save_record(build_record("snap-design", pcb=_v1_pcb()))
    record_module.save_record(build_record("snap-design", pcb=_v2_pcb()))
    record_module.save_record(
        build_record("snap-design", pcb={"components": []})
    )
    # A middle slot deleted by hand (a broken disk state): the next
    # save displaces the current state and takes the NEXT number — the
    # history is never renumbered, the gap just stays a gap.
    snapshot_path("snap-design", 1).unlink()
    record_module.save_record(
        build_record("snap-design", pcb={"components": []})
    )
    rows = list_snapshots("snap-design")
    assert [row["snapshot_id"] for row in rows] == [2, 3]
    assert all(row["error"] is None for row in rows)


def test_history_names_what_it_cannot_read(_record_storage: Path) -> None:
    record_module.save_record(build_record("snap-design", pcb=_v1_pcb()))
    record_module.save_record(build_record("snap-design", pcb=_v2_pcb()))
    record_module.save_record(
        build_record("snap-design", pcb={"components": []})
    )
    snapshot_path("snap-design", 1).write_text("{broken", encoding="utf-8")
    rows = list_snapshots("snap-design")
    assert [row["snapshot_id"] for row in rows] == [1, 2]
    assert "unreadable or not valid JSON" in rows[0]["error"]
    assert rows[0]["taken_at"] is None
    assert rows[1]["reason"] == "save"
    assert rows[1]["error"] is None


def test_load_snapshot_refuses_a_slot_that_lies_about_its_identity(
    _record_storage: Path,
) -> None:
    record_module.save_record(build_record("snap-design", pcb=_v1_pcb()))
    record_module.save_record(build_record("snap-design", pcb=_v2_pcb()))
    target = snapshot_path("snap-design", 1)
    payload = json.loads(target.read_text(encoding="utf-8"))
    payload["snapshot_id"] = 7
    target.write_text(json.dumps(payload), encoding="utf-8")
    with pytest.raises(DesignRecordError) as refused:
        load_snapshot("snap-design", 1)
    assert "says snapshot_id 7, stored as 1" in str(refused.value)

    with pytest.raises(DesignRecordError) as absent:
        load_snapshot("snap-design", 99)
    assert "no snapshot 99 for record 'snap-design'" in str(absent.value)


# --- restore ----------------------------------------------------------------


def test_restore_round_trips_verbatim_and_preserves_what_it_displaces(
    _record_storage: Path,
) -> None:
    """The done-when: generate, change, restore — and the BOM matches."""
    v1 = build_record(
        "snap-design",
        pcb=_v1_pcb(),
        bom=bom_from_pcb(_v1_pcb()),
        holes=_holes(),
    )
    record_module.save_record(v1)
    generated_bom = v1.bom

    v2 = build_record(
        "snap-design",
        pcb=_v2_pcb(),
        bom=bom_from_pcb(_v2_pcb()),
    )
    record_module.save_record(v2)
    assert load_record("snap-design").bom != generated_bom

    restored = restore_snapshot("snap-design", 1)

    # The restored record IS that version — sections and stamps.
    assert restored.pcb == _v1_pcb()
    assert restored.bom == generated_bom
    assert restored.created_at == v1.created_at
    assert load_record("snap-design").created_at == restored.created_at
    assert restored.updated_at == v1.updated_at
    assert load_record("snap-design").updated_at == restored.updated_at

    # The displaced current state is snapshotted (reason restore), so
    # the history holds every left-behind state and nothing is lost.
    rows = list_snapshots("snap-design")
    assert [(row["snapshot_id"], row["reason"]) for row in rows] == [
        (1, "save"),
        (2, "restore"),
    ]
    displaced = load_snapshot("snap-design", 2)
    assert displaced["record"]["pcb"] == _v2_pcb()

    # The BOM export of the restored record matches the generated one
    # (parse-back contract, M21.1): the decided done-when.
    path = export_bom_csv(restored, _record_storage / "bom.csv")
    assert load_bom_csv(path)["lines"] == generated_bom["lines"]


def test_restore_refuses_to_displace_an_unreadable_state(
    _record_storage: Path,
) -> None:
    record_module.save_record(build_record("snap-design", pcb=_v1_pcb()))
    record_module.save_record(build_record("snap-design", pcb=_v2_pcb()))
    record_module.record_path("snap-design").write_text(
        "{corrupt", encoding="utf-8"
    )
    with pytest.raises(DesignRecordError) as refused:
        restore_snapshot("snap-design", 1)
    assert "unreadable or not valid JSON" in str(refused.value)
    # The history was not touched by the refused restore.
    assert [row["snapshot_id"] for row in list_snapshots("snap-design")] == [
        1
    ]


def test_restore_onto_a_missing_record_is_the_repair_path(
    _record_storage: Path,
) -> None:
    record_module.save_record(build_record("snap-design", pcb=_v1_pcb()))
    record_module.save_record(build_record("snap-design", pcb=_v2_pcb()))
    record_module.record_path("snap-design").unlink()

    restored = restore_snapshot("snap-design", 1)
    assert restored.pcb == _v1_pcb()
    assert load_record("snap-design").pcb == _v1_pcb()
    # Nothing was displaced (there was no live record), so the history
    # stays honest about what was displaced: only the save.
    assert [row["snapshot_id"] for row in list_snapshots("snap-design")] == [
        1
    ]


def test_restore_refuses_a_snapshot_of_a_foreign_record(
    _record_storage: Path,
) -> None:
    record_module.save_record(build_record("snap-design", pcb=_v1_pcb()))
    record_module.save_record(build_record("snap-design", pcb=_v2_pcb()))
    target = snapshot_path("snap-design", 1)
    payload = json.loads(target.read_text(encoding="utf-8"))
    payload["record"]["record_id"] = "other-design"
    target.write_text(json.dumps(payload), encoding="utf-8")
    with pytest.raises(DesignRecordError) as refused:
        restore_snapshot("snap-design", 1)
    assert "carries record 'other-design'" in str(refused.value)


def test_snapshot_plumbing_refuses_the_undefinable(
    _record_storage: Path,
) -> None:
    with pytest.raises(DesignRecordError):
        snapshots_dir(".hidden")  # ids may not start with a dot
    with pytest.raises(DesignRecordError):
        snapshot_path("ok", 0)
    with pytest.raises(DesignRecordError):
        snapshot_path("ok", True)  # a bool is not a snapshot id


def test_take_snapshot_appends_highest_number_plus_one(
    _record_storage: Path,
) -> None:
    record = build_record("snap-design", pcb=_v1_pcb())
    first = take_snapshot(record, reason="manual")
    second = take_snapshot(record, reason="manual")
    assert (first, second) == (1, 2)
    payload = load_snapshot("snap-design", 1)
    assert payload["reason"] == "manual"
    assert payload["record"]["pcb"] == _v1_pcb()


# --- the compare ------------------------------------------------------------


def test_compare_states_every_dimension_with_quotes(
    _record_storage: Path,
) -> None:
    record_module.save_record(
        build_record(
            "snap-design",
            pcb=_v1_pcb(),
            firmware={"mcu_pin": {}},
            holes=_holes(),
        )
    )
    record_module.save_record(
        build_record(
            "snap-design",
            pcb=_v2_pcb(),
            firmware={"mcu_pin": {"GPIO4": "gpio_out"}},
            holes=[
                {
                    "ref": "H1",
                    "pos_mm": {"x": 9.0, "y": 2.5},
                    "diameter_mm": 3.0,
                }
            ],
        )
    )
    # Snapshot ids are displaced states: compare(1, 2) diffs the
    # generated design (v1) against the changed one (v2). The third
    # save displaces v2 into slot 2 (its content repeats v2).
    record_module.save_record(
        build_record(
            "snap-design",
            pcb=_v2_pcb(),
            firmware={"mcu_pin": {"GPIO4": "gpio_out"}},
            holes=[
                {
                    "ref": "H1",
                    "pos_mm": {"x": 9.0, "y": 2.5},
                    "diameter_mm": 3.0,
                }
            ],
        )
    )
    diff = compare_snapshots("snap-design", 1, 2)
    assert diff["first"]["snapshot_id"] == 1
    assert diff["second"]["reason"] == "save"

    dims = diff["dimensions"]
    # Geometry: the board rect moved and the hole moved; connectors
    # and enclosure were absent on both sides (absent means absent).
    geometry = dims["geometry"]
    assert geometry["status"] == "changed"
    assert [part["part"] for part in geometry["parts"]] == [
        "pcb.board",
        "holes",
    ]
    changed_board = geometry["parts"][0]
    assert '"width_mm": 50.0' in changed_board["first"]
    assert '"width_mm": 60.0' in changed_board["second"]

    # Nets: LED_OUT exists only in the second snapshot.
    nets = dims["nets"]
    assert nets["status"] == "changed"
    assert '"name": "LED_OUT"' in nets["parts"][0]["second"]
    assert '"name": "LED_OUT"' not in nets["parts"][0]["first"]

    # Pins: R1 gained a pin; U1's pins are identical and are NOT quoted.
    pins = dims["pins"]
    assert [part["part"] for part in pins["parts"]] == ["R1"]

    # Firmware: the map grew.
    firmware = dims["firmware"]
    assert firmware["status"] == "changed"
    assert "GPIO4" in firmware["parts"][0]["second"]


def test_compare_of_identical_states_says_so(
    _record_storage: Path,
) -> None:
    record = build_record(
        "snap-design", pcb=_v1_pcb(), firmware={"mcu_pin": {}}
    )
    first = take_snapshot(record, reason="manual")
    second = take_snapshot(record, reason="manual")
    diff = compare_snapshots("snap-design", first, second)
    assert all(
        dims["status"] == "identical"
        for dims in diff["dimensions"].values()
    )
    assert set(diff["dimensions"]) == {
        "geometry",
        "nets",
        "pins",
        "firmware",
    }


def test_compare_names_component_pins_keyed_by_ref(
    _record_storage: Path,
) -> None:
    """Pin maps are keyed by ref in board order: a repeated ref gets
    ``ref#2``, a refless component ``(no ref)``, and a component absent
    from one snapshot carries the absence marker on that side —
    absence is a diffable value, not a skip."""
    record_module.save_record(
        build_record(
            "snap-design",
            pcb={
                "components": [
                    {"ref": "R1", "value": "1k", "pins": []},
                    {"ref": "R1", "value": "1k", "pins": [{"pin": "1"}]},
                    {"pins": [{"pin": "9"}]},
                    {"ref": "U9", "value": "x", "pins": []},
                ],
            },
        )
    )
    record_module.save_record(
        build_record(
            "snap-design",
            pcb={
                "components": [
                    {"ref": "R1", "value": "1k", "pins": [{"pin": "2"}]},
                    {"ref": "R1", "value": "1k", "pins": [{"pin": "3"}]},
                    {"pins": [{"pin": "9"}]},
                ],
            },
        )
    )
    # The third save displaces the second state into slot 2, so
    # compare(1, 2) diffs the two keyed components above.
    record_module.save_record(
        build_record(
            "snap-design",
            pcb={
                "components": [
                    {"ref": "R1", "value": "1k", "pins": [{"pin": "2"}]},
                    {"ref": "R1", "value": "1k", "pins": [{"pin": "3"}]},
                    {"pins": [{"pin": "9"}]},
                ],
            },
        )
    )
    diff = compare_snapshots("snap-design", 1, 2)
    parts = {
        part["part"]: part for part in diff["dimensions"]["pins"]["parts"]
    }
    # The decided key naming is exercised by the changed parts: R1's
    # first instance re-pinned, its SECOND instance got ``R1#2``, and
    # U9 vanished from the second state (absent quotes as null on that
    # side). (no ref) is identical, so it is not quoted.
    assert set(parts) == {"R1", "R1#2", "U9"}
    assert '"pin": "2"' in parts["R1"]["second"]
    assert '"pin": "3"' in parts["R1#2"]["second"]
    # The quoted side is JSON serialization, so the marker arrives
    # quoted exactly like any other JSON string.
    assert parts["U9"]["second"] == json.dumps(ABSENT_PIN_REF)


def test_compare_refuses_an_unknown_snapshot(
    _record_storage: Path,
) -> None:
    record_module.save_record(build_record("snap-design", pcb=_v1_pcb()))
    record_module.save_record(build_record("snap-design", pcb=_v2_pcb()))
    with pytest.raises(DesignRecordError) as refused:
        compare_snapshots("snap-design", 1, 42)
    assert "no snapshot 42 for record 'snap-design'" in str(refused.value)
