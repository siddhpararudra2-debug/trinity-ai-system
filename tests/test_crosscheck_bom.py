"""M21.1: the BOM — board-derived lines, CSV export, and C11.

`decided.md` convention 9 pin the grammar: per-ref lines
``{ref, part, value, quantity, package}``; ``part`` is annotation
nobody can invent (the board carries no MPN), C11 compares only refs,
value, package (string-collapsed, case-sensitive) and quantity against
the ref's population count; the CSV is stdlib, the columns exactly
``ref, part, value, quantity, package``, the round-trip proven by
parse-back.
"""

from __future__ import annotations

import copy

import pytest

from src.engines.crosscheck.bom import (
    BOM_COLUMNS,
    bom_from_pcb,
    export_bom_csv,
    load_bom_csv,
)
from src.engines.crosscheck.check import (
    FAIL,
    PASS,
    WARN,
    run_checks,
)
from src.engines.crosscheck.record import (
    DesignRecord,
    DesignRecordError,
    build_record,
)


def _component(ref: str, value: str, footprint: str, kind: str) -> dict:
    return {
        "ref": ref, "value": value, "footprint": footprint, "type": kind,
        "rotation_deg": 0.0, "layer": "F.Cu", "pins": [],
    }


_MIN_PCB = {
    "board": {"width_mm": 50.0, "height_mm": 40.0, "thickness_mm": 1.6,
              "origin_mm": {"x": 0.0, "y": 0.0}, "layers": ["F.Cu"]},
    "components": [
        _component("U1", "ESP32-WROOM-32", "ESP32-WROOM-32", "mcu"),
        _component("J1", "USB-C", "USB_C_Receptacle", "connector"),
        _component("R1", "1k", "R_0402", "passive"),
    ],
    "nets": [],
    "tracks": [],
    "footprints": {},
    "placements": [],
    "rules": [],
    "metadata": {},
}


def _record(bom: dict | None, pcb: dict | None = None) -> DesignRecord:
    kwargs: dict = {
        "pcb": copy.deepcopy(_MIN_PCB) if pcb is None else pcb,
        "firmware": {},
    }
    if bom is not None:
        kwargs["bom"] = bom
    return build_record("bom-tests", **kwargs)


def _check(checks: list, check_id: str):
    return {result.check_id: result for result in checks}[check_id]


# --- the board-derived builder --------------------------------------------


def test_bom_from_pcb_derives_honest_lines() -> None:
    bom = bom_from_pcb(_MIN_PCB)
    assert list(bom) == ["lines"]
    assert bom["lines"] == [
        {"ref": "U1", "part": "", "value": "ESP32-WROOM-32",
         "quantity": 1, "package": "ESP32-WROOM-32"},
        {"ref": "J1", "part": "", "value": "USB-C", "quantity": 1,
         "package": "USB_C_Receptacle"},
        {"ref": "R1", "part": "", "value": "1k", "quantity": 1,
         "package": "R_0402"},
    ]
    # Deriving twice yields equal, independent dicts.
    again = bom_from_pcb(_MIN_PCB)
    assert again["lines"] == bom["lines"]
    again["lines"][0]["value"] = "mutated"
    assert bom["lines"][0]["value"] == "ESP32-WROOM-32"


def test_bom_from_pcb_treats_absent_board_fields_as_stated_absence() -> None:
    pcb = copy.deepcopy(_MIN_PCB)
    del pcb["components"][2]["value"]
    del pcb["components"][2]["footprint"]
    bom = bom_from_pcb(pcb)
    assert bom["lines"][2] == {
        "ref": "R1", "part": "", "value": "", "quantity": 1,
        "package": "",
    }


def test_bom_from_pcb_refuses_the_undefinable() -> None:
    with pytest.raises(DesignRecordError) as missing:
        bom_from_pcb(None)
    assert "needs the record's pcb section" in str(missing.value)

    with pytest.raises(DesignRecordError) as malformed:
        bom_from_pcb({"components": "not a list"})
    assert "pcb.components is absent or malformed" in str(malformed.value)

    nameless = copy.deepcopy(_MIN_PCB)
    nameless["components"].append({"value": "DNP", "footprint": "R_0402"})
    with pytest.raises(DesignRecordError) as nameless_error:
        bom_from_pcb(nameless)
    assert "pcb.components[3] carries no ref" in str(nameless_error.value)


# --- the CSV export and its parse-back contract ---------------------------


def test_export_header_is_exactly_the_decided_columns(tmp_path) -> None:
    path = export_bom_csv(_record(bom_from_pcb(_MIN_PCB)),
                          tmp_path / "bom.csv")
    first = path.read_text(encoding="utf-8").splitlines()[0]
    assert first == ",".join(BOM_COLUMNS)
    assert BOM_COLUMNS == ("ref", "part", "value", "quantity", "package")


def test_export_round_trips_by_parse_back(tmp_path) -> None:
    # Awkward cells on purpose: a comma, a quote, a leading zero — the
    # csv module must quote them so the parse-back is byte-faithful.
    bom = bom_from_pcb(_MIN_PCB)
    bom["lines"][1]["value"] = 'USB-C mux, rev "C"'
    bom["lines"][2]["value"] = "010k"
    record = _record(bom)
    path = export_bom_csv(record, tmp_path / "bom.csv")
    parsed = load_bom_csv(path)
    assert parsed["lines"] == bom["lines"]


def test_export_and_parse_back_are_quantity_faithful(tmp_path) -> None:
    bom = {"lines": [
        {"ref": "U1", "part": "", "value": "ESP32-WROOM-32",
         "quantity": 1, "package": "ESP32-WROOM-32"},
        {"ref": "R2", "part": "", "value": "0.5", "quantity": 2.0,
         "package": "R_0805"},
    ]}
    path = export_bom_csv(_record(bom), tmp_path / "bom.csv")
    lines = load_bom_csv(path)["lines"]
    assert lines[0]["quantity"] == 1  # int stays int
    assert lines[1]["quantity"] == 2.0  # float stays float


def test_export_refuses_everything_it_cannot_render(
    tmp_path,
) -> None:
    with pytest.raises(DesignRecordError) as absent:
        export_bom_csv(_record(bom=None), tmp_path / "bom.csv")
    assert "carries no bom section" in str(absent.value)

    with pytest.raises(DesignRecordError) as malformed:
        export_bom_csv(_record({"lines": "not a list"}),
                       tmp_path / "bom.csv")
    assert "bom.lines is absent or malformed" in str(malformed.value)

    with pytest.raises(DesignRecordError) as entries:
        export_bom_csv(_record({"lines": ["garbage"]}),
                       tmp_path / "bom.csv")
    assert "bom.lines[0] is not an object" in str(entries.value)

    nonscalar = bom_from_pcb(_MIN_PCB)
    nonscalar["lines"][0]["package"] = {"nested": "object"}
    with pytest.raises(DesignRecordError) as cell:
        export_bom_csv(_record(nonscalar), tmp_path / "bom.csv")
    assert "bom.lines[0].package is dict" in str(cell.value)


def test_parse_back_refuses_foreign_headers_and_bad_quantities(
    tmp_path,
) -> None:
    foreign = tmp_path / "foreign.csv"
    foreign.write_text("ref,part,value,qty,package\n", encoding="utf-8")
    with pytest.raises(DesignRecordError) as header:
        load_bom_csv(foreign)
    assert "columns ['ref', 'part', 'value', 'qty', 'package']" in str(
        header.value
    )

    bad_quantity = tmp_path / "bad_quantity.csv"
    bad_quantity.write_text(
        "ref,part,value,quantity,package\nR1,,1k,two,R_0402\n",
        encoding="utf-8",
    )
    with pytest.raises(DesignRecordError) as quantity_error:
        load_bom_csv(bad_quantity)
    assert "quantity cell 'two' is not a number" in str(quantity_error.value)


def test_empty_board_and_empty_bom_export_a_bare_header(
    tmp_path,
) -> None:
    empty_pcb = copy.deepcopy(_MIN_PCB)
    empty_pcb["components"] = []
    path = export_bom_csv(
        _record(bom_from_pcb(empty_pcb)), tmp_path / "bom.csv"
    )
    assert [line for line in path.read_text(
        encoding="utf-8").splitlines()] == [",".join(BOM_COLUMNS)]
    assert load_bom_csv(path)["lines"] == []


# --- C11: the BOM matches the populated parts -----------------------------


def test_c11_board_derived_bom_passes() -> None:
    record = _record(bom_from_pcb(_MIN_PCB))
    checks = run_checks(record)
    c11 = _check(checks, "C11")
    assert c11.status == PASS
    assert c11.name == "bom_matches_parts"
    assert c11.reason == (
        "the BOM lines match the populated components 1:1 on ref, "
        "value, package and quantity"
    )
    assert c11.location == ""  # an all-PASS aggregate names no location
    rows = c11.evidence["bom"]
    assert all(row["status"] == PASS for row in rows)
    assert len(rows) == 3


def test_c11_empty_sides_pass_vacuously() -> None:
    empty_pcb = copy.deepcopy(_MIN_PCB)
    empty_pcb["components"] = []
    bom = bom_from_pcb(empty_pcb)
    record = _record(bom, pcb=empty_pcb)
    c11 = _check(run_checks(record), "C11")
    assert c11.status == PASS
    assert "both empty" in c11.reason


def _mutated_bom(mutate) -> DesignRecord:
    bom = bom_from_pcb(_MIN_PCB)
    mutate(bom)
    return _record(bom)


def test_c11_attribute_difference_warns_naming_each_attribute() -> None:
    def mutate(bom):
        line = bom["lines"][2]  # R1
        line["value"] = "10k"
        line["package"] = "R_0603"
        line["quantity"] = 2

    c11 = _check(run_checks(_mutated_bom(mutate)), "C11")
    assert c11.status == WARN
    assert c11.location == "R1"
    assert (
        "BOM line 'R1' differs from the populated part: "
        "value '10k' vs '1k'; package 'R_0603' vs 'R_0402'; "
        "quantity 2 vs 1"
    ) in c11.reason


def test_c11_case_sensitivity_is_the_decided_compare() -> None:
    def mutate(bom):
        bom["lines"][2]["value"] = "1K"  # same number, different case

    c11 = _check(run_checks(_mutated_bom(mutate)), "C11")
    assert c11.status == WARN
    assert "value '1K' vs '1k'" in c11.reason


def test_c11_missing_and_extra_refs_fail() -> None:
    # Missing: R1 has no BOM line.
    def without_r1(bom):
        bom["lines"].pop(2)

    c11 = _check(run_checks(_mutated_bom(without_r1)), "C11")
    assert c11.status == FAIL
    assert c11.reason == (
        "no BOM line for populated component 'R1' (missing ref)"
    )
    assert c11.location == "R1"

    # Extra: a line for R9 the board never populated.
    def with_r9(bom):
        bom["lines"].append(
            {"ref": "R9", "part": "", "value": "10k", "quantity": 1,
             "package": "R_0402"}
        )

    c11 = _check(run_checks(_mutated_bom(with_r9)), "C11")
    assert c11.status == FAIL
    assert c11.reason == (
        "BOM line 'R9' matches no populated component (extra ref)"
    )
    assert c11.location == "R9"


def test_c11_duplicate_populated_refs_meter_their_quantity() -> None:
    # The board populates two R1 instances. The BOM convention wants one
    # annotated line; its quantity must meter the true population.
    doubled = copy.deepcopy(_MIN_PCB)
    doubled["components"].append(_component("R1", "1k", "R_0402", "passive"))
    # bom_from_pcb emits one line per populated instance, so a doubled
    # board yields two R1 lines — collapse them to the single line.
    bom = bom_from_pcb(doubled)
    r1_lines = [line for line in bom["lines"] if line["ref"] == "R1"]
    assert len(r1_lines) == 2
    bom["lines"].remove(r1_lines[1])
    # One line, quantity 1: the population is two, so it meters short.
    c11 = _check(run_checks(_record(bom, pcb=doubled)), "C11")
    assert c11.status == WARN
    assert "quantity 1 vs 2" in c11.reason
    assert c11.location == "R1"
    # Declare the true population and the line matches again.
    bom["lines"][2]["quantity"] = 2
    c11 = _check(run_checks(_record(bom, pcb=doubled)), "C11")
    assert c11.status == PASS
    matched = next(
        row for row in c11.evidence["bom"] if row["location"] == "R1"
    )
    assert "quantity 2)" in matched["reason"]


def test_c11_refuses_or_relents_on_the_missing_inputs() -> None:
    # A record with no pcb section at all (built directly, no default
    # board): the genuine cannot_verify path.
    record = build_record("bom-tests", bom=bom_from_pcb(_MIN_PCB))
    c11 = _check(run_checks(record), "C11")
    assert c11.status == WARN and c11.reason.startswith("cannot_verify")
    assert "the record lacks a pcb section" in c11.reason

    c11 = _check(run_checks(_record(bom=None)), "C11")
    assert c11.status == WARN
    assert "carries no bom section" in c11.reason
    assert c11.location == "bom"

    record = _record(bom_from_pcb(_MIN_PCB))
    record.bom = {"lines": "gone"}  # malformed at check time
    c11 = _check(run_checks(record), "C11")
    assert c11.status == WARN
    assert "bom.lines is absent or malformed" in c11.reason


def test_c11_malformed_entries_warn_never_pass() -> None:
    bom = bom_from_pcb(_MIN_PCB)
    bom["lines"].insert(2, "garbage")
    c11 = _check(run_checks(_record(bom)), "C11")
    assert c11.status == WARN
    rows = c11.evidence["bom"]
    garbage_row = rows[2]
    assert garbage_row["status"] == WARN
    assert "is not an object" in garbage_row["reason"]


def test_c11_lines_without_a_ref_cannot_match() -> None:
    bom = bom_from_pcb(_MIN_PCB)
    bom["lines"][2] = {"value": "1k", "quantity": 1, "package": "R_0402"}
    c11 = _check(run_checks(_record(bom)), "C11")
    assert c11.status == FAIL
    assert c11.reason == (
        "no BOM line for populated component 'R1' (missing ref)"
    )
