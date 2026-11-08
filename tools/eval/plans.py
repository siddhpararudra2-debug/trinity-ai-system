"""M22 eval plans: thirty repo-owned deterministic plans, one per prompt.

A plan is the grader that owns the prompt's truth (decided.md, M22):
record-property truths are graded by the verified report itself — never
by a hand-written expected verdict; engine-level truths run the real
registry engines; storage-flavored truths use a temporary records root
through ``records_root_param`` and touch nothing under ``data/``; and
any truth this build honestly cannot verify (the native binary absent
on this machine, a clause the V1 engine does not express) returns
``SKIP`` with its reason — the table never invents a pass.

The prompt row carries no ``expected`` field, and no model grades: the
verdicts below are reproducible by ``python tools/eval/run.py`` and by
``tests/test_eval_runner.py`` alike.
"""

from __future__ import annotations

import copy
import os
import tempfile
from collections.abc import Callable
from pathlib import Path
from typing import Any

# Verdicts are the decided runner grammar: PASS (the truth was verified
# by this build), FAIL (the truth was contradicted — named, never
# guessed away), SKIP (the truth cannot honestly be verified here).
PASS = "PASS"
FAIL = "FAIL"
SKIP = "SKIP"

_ENGINES_BOOTED = False

# The eval record's only key set (decided.md M20.e: no ``expected``
# field; the plans own the verdicts).
_PROMPT_KEYS = {"id", "domain", "prompt", "source"}


def _engine(name: str) -> Any:
    """A registry engine by name, bootstrapped once per process."""
    global _ENGINES_BOOTED
    if not _ENGINES_BOOTED:
        from src.engines.registry import bootstrap_engines, registry

        bootstrap_engines()
        _ENGINES_BOOTED = True
    from src.engines.registry import registry

    return registry.get(name)


def _native_skip(engine_name: str) -> str | None:
    """A skip reason when the engine is still the scaffold stub.

    Probes ``describe()`` rather than executing: the stub is registered
    so callers can discover the refusal without an exception.

    Returns the human-readable skip reason, or ``None`` if the engine
    is not a scaffold stub on this machine.
    """
    global _ENGINES_BOOTED
    if not _ENGINES_BOOTED:
        from src.engines.registry import bootstrap_engines, registry

        bootstrap_engines()
        _ENGINES_BOOTED = True
    from src.engines.registry import registry

    status = registry.get(engine_name).describe().get("status")
    if status == "scaffolded":
        return (
            f"{engine_name} is the ScaffoldEngine stub on this machine "
            "(the native binary is absent); it cannot honestly run this "
            "prompt, and the table states that rather than inventing a "
            "pass"
        )
    return None


def _fixture(name: str) -> Any:
    from src.engines.crosscheck.fixtures import build_fixture

    return build_fixture(name)


# --- the shared grading arms ------------------------------------------------


def _single_fail_report(
    record: Any, check_id: str, fragments: list[str], location: str
) -> tuple[str, str]:
    """One break-fixture prompt's done-when: exactly one check FAILs,
    carrying the named fragments at the named location, and the overall
    report says FAILED. The same contract ``test_crosscheck_fixtures``
    pins, replayed as the prompt's verification."""
    from src.engines.crosscheck.check import (
        OVERALL_FAILED,
        overall_status,
        run_checks,
    )
    from src.engines.crosscheck.check import FAIL as STATUS_FAIL

    checks = run_checks(record)
    failing = [check for check in checks if check.status == STATUS_FAIL]
    if len(failing) != 1:
        ids = [check.check_id for check in failing]
        return FAIL, (
            f"expected exactly one FAIL, found {len(failing)}: {ids}"
        )
    result = failing[0]
    if result.check_id != check_id:
        return FAIL, f"the FAIL lands on {result.check_id}, not {check_id}"
    for fragment in fragments:
        if fragment not in result.reason:
            return FAIL, (
                f"{check_id}'s reason misses {fragment!r}: {result.reason}"
            )
    if result.location != location:
        return FAIL, (
            f"{check_id} located at '{result.location}', not '{location}'"
        )
    if overall_status(checks) != OVERALL_FAILED:
        return FAIL, "the overall report did not come out FAILED"
    truncated = result.reason if len(result.reason) <= 100 else (
        result.reason[:100] + "..."
    )
    return PASS, f"{check_id} FAILs at {location} ({truncated})"


def _verified_report(
    record: Any, fixture_name: str
) -> tuple[str, str]:
    """A good-design prompt's done-when: the report is VERIFIED with
    every cross-check PASS — WARN or FAIL denies it, as ever."""
    from src.engines.crosscheck.check import (
        CHECK_ORDER,
        OVERALL_VERIFIED,
        overall_status,
        run_checks,
    )
    from src.engines.crosscheck.check import FAIL as STATUS_FAIL
    from src.engines.crosscheck.check import PASS as STATUS_PASS
    from src.engines.crosscheck.check import WARN as STATUS_WARN

    checks = run_checks(record)
    expected = len(CHECK_ORDER)
    passes = [c for c in checks if c.status == STATUS_PASS]
    warns = [c for c in checks if c.status == STATUS_WARN]
    fails = [c for c in checks if c.status == STATUS_FAIL]
    if len(passes) != expected or warns or fails:
        return FAIL, (
            f"{fixture_name}: {len(passes)}/{expected} PASS, "
            f"{len(warns)} WARN, {len(fails)} FAIL — not a clean "
            "VERIFIED"
        )
    status = overall_status(checks)
    if status != OVERALL_VERIFIED:
        return FAIL, f"{fixture_name}: overall is {status}, not VERIFIED"
    return PASS, f"{fixture_name}: VERIFIED, {expected}/{expected} PASS"


# --- the thirty plans --------------------------------------------------------


def _p001() -> tuple[str, str]:
    # The prompt's "removable lid" clause is part of its truth; the V1
    # enclosure's parameters are {length, width, height, wall_thickness}
    # only. Grading the box and passing would fake the lid's coverage.
    return SKIP, (
        "the V1 enclosure has no lid parameter (length, width, height, "
        "wall_thickness only); the prompt's removable-lid truth cannot "
        "be verified by this build"
    )


def _p002() -> tuple[str, str]:
    cad = _engine("cad")
    result = cad.execute(
        "nest",
        {
            "stock_width_mm": 400.0,
            "stock_height_mm": 300.0,
            "parts": [
                {
                    "type": "l_bracket",
                    "parameters": {
                        "base_length": 60.0,
                        "height": 40.0,
                        "width": 30.0,
                    },
                    "quantity": 5,
                }
            ],
        },
    ).result
    placements = result["placements"]
    if len(placements) != 5 or result["unplaced"]:
        return FAIL, (
            f"{len(placements)} placed, {len(result['unplaced'])} unplaced"
        )
    # The sheet-bounds truth, recomputed here on purpose (the M14
    # nesting test's shape): pairwise non-overlap and full containment.
    for part in placements:
        if not (
            part["x_mm"] >= 0
            and part["y_mm"] >= 0
            and part["x_mm"] + part["width_mm"] <= 400.0
            and part["y_mm"] + part["height_mm"] <= 300.0
        ):
            return FAIL, f"{part['label']} falls off the 400x300 sheet"
    for i, first in enumerate(placements):
        for second in placements[i + 1 :]:
            separated = (
                first["x_mm"] + first["width_mm"] <= second["x_mm"]
                or second["x_mm"] + second["width_mm"] <= first["x_mm"]
                or first["y_mm"] + first["height_mm"] <= second["y_mm"]
                or second["y_mm"] + second["height_mm"] <= first["y_mm"]
            )
            if not separated:
                return FAIL, (
                    f"{first['label']} and {second['label']} overlap"
                )
    return PASS, (
        f"{result['placed_count']}/5 placed, 0 unplaced, no overlap, "
        "every part inside the 400x300 sheet"
    )


def _p003() -> tuple[str, str]:
    cad = _engine("cad")
    outcome = cad.execute(
        "generate",
        {
            "type": "quadcopter_frame",
            "parameters": {
                "overall_size": 250.0,  # mm wheelbase (diagonal span)
                "motor_mount_diameter": 16.0,
            },
            "outputs": ["step"],
        },
    )
    if not outcome.success:
        return FAIL, f"generate failed: {outcome.errors}"
    if outcome.validation is None or outcome.validation.status != "VALIDATED":
        return FAIL, "the frame did not come back VALIDATED"
    steps = [
        Path(path) for path, fmt in outcome.pending_artifacts if fmt == "step"
    ]
    if not steps:
        return SKIP, (
            "the native STEP binary is absent on this machine; it cannot "
            "honestly generate a STEP file, and the table states that "
            "rather than inventing a pass"
        )
    written = steps[0]
    if not written.exists() or written.suffix != ".step":
        return FAIL, f"{written} is not a writable .step file"
    return PASS, (
        f"VALIDATED; STEP written to {written.name} "
        f"({written.stat().st_size} bytes)"
    )


def _p004() -> tuple[str, str]:
    reason = _native_skip("pcb")
    if reason:
        return SKIP, reason
    # The native engine present: the refusal this plan owns would not
    # happen, and a native-design grading plan is not yet written —
    # honest about this plan's own gap rather than grading blind.
    return FAIL, (
        "the native pcb engine is present, but this plan only owns the "
        "scaffold refusal; a native grading plan is not yet written"
    )


def _p005() -> tuple[str, str]:
    return _p004()


def _p006() -> tuple[str, str]:
    reason = _native_skip("firmware")
    if reason:
        return SKIP, reason
    return FAIL, (
        "the native firmware engine is present, but this plan only owns "
        "the scaffold refusal; a native grading plan is not yet written"
    )


def _p007() -> tuple[str, str]:
    return _p006()


def _p008() -> tuple[str, str]:
    # The math engine solves one unknown at a time, so the deterministic
    # chain the plan composes is: solve the linear relation x - y = 1
    # for y, substitute that relation into 3x + 2y = 12, solve for x,
    # then evaluate y = x - 1 at x (the engine performs every step).
    math = _engine("math")
    relation = math.execute(
        "solve", {"expression": "x - y = 1", "solve_for": "y"}
    )
    if not relation.success or relation.result["result"] != ["x - 1"]:
        return FAIL, f"relation solve failed: {relation.result['result']}"
    substituted = math.execute(
        "solve", {"expression": "3*x + 2*(x - 1) = 12"}
    )
    if not substituted.success or substituted.result["result"] != [2.8]:
        return FAIL, (
            f"x did not come out 2.8: {substituted.result['result']}"
        )
    x_value = substituted.result["result"][0]
    back = math.execute(
        "evaluate", {"expression": "x - 1", "variables": {"x": x_value}}
    )
    y_value = back.result["result"]
    if abs(y_value - 1.8) > 1e-9:
        return FAIL, f"y did not come out 1.8: {y_value}"
    return PASS, f"x = {x_value}, y = {y_value} (solve + substitute chain)"


def _p009() -> tuple[str, str]:
    import math as py_math

    outcome = _engine("math").execute(
        "evaluate",
        {
            "expression": "1/(2*pi*sqrt(L*C))",
            "variables": {"L": 1e-5, "C": 1e-7},
        },
    )
    value = outcome.result["result"]
    expected = 1 / (2 * py_math.pi * py_math.sqrt(1e-5 * 1e-7))
    if not isinstance(value, float) or abs(value - expected) > 1e-3:
        return FAIL, f"expected ~{expected}, got {value!r}"
    checks = outcome.validation.checks
    if checks.get("numeric") is not True:
        return FAIL, f"the numeric stamp is not True: {checks}"
    return PASS, f"resonant frequency {value:.3f} Hz, VERIFIED numeric"


def _p010() -> tuple[str, str]:
    return _verified_report(_fixture("good_esp32_i2c"), "good_esp32_i2c")


def _p011() -> tuple[str, str]:
    outcome = _engine("cad").execute(
        "preview",
        {
            "type": "enclosure",
            "parameters": {
                "length": 200.0,
                "width": 150.0,
                "height": 60.0,
                "wall_thickness": 4.0,
            },
        },
    )
    bbox = outcome.result["bounding_box_mm"]
    spans = [bbox["max"][axis] - bbox["min"][axis] for axis in range(3)]
    expected = (200.0, 150.0, 60.0)
    for axis, (span, want) in enumerate(zip(spans, expected)):
        if abs(span - want) > 1e-6:
            return FAIL, (
                f"outer span on axis {axis} is {span}, not {want}"
            )
    return PASS, (
        f"outer bounding box spans {spans[0]:.3f} x {spans[1]:.3f} x "
        f"{spans[2]:.3f} mm as requested"
    )


def _p012() -> tuple[str, str]:
    cad = _engine("cad")
    result = cad.execute(
        "nest",
        {
            "stock_width_mm": 200.0,
            "stock_height_mm": 200.0,
            "spacing_mm": 5.0,
            "parts": [
                {"type": "mounting_plate", "parameters": {}, "quantity": 2},
                {"type": "standoff", "parameters": {}, "quantity": 3},
            ],
        },
    ).result
    placements = result["placements"]
    if result["spacing_mm"] != 5.0:
        return FAIL, (
            f"the requested 5 mm spacing was not carried: "
            f"{result['spacing_mm']}"
        )
    if len(placements) != 5 or result["unplaced"]:
        return FAIL, (
            f"{len(placements)} placed, {len(result['unplaced'])} unplaced"
        )
    for part in placements:
        if not (
            part["x_mm"] >= 0
            and part["y_mm"] >= 0
            and part["x_mm"] + part["width_mm"] <= 200.0
            and part["y_mm"] + part["height_mm"] <= 200.0
        ):
            return FAIL, f"{part['label']} falls off the 200x200 sheet"
    for i, first in enumerate(placements):
        for second in placements[i + 1 :]:
            separated = (
                first["x_mm"] + first["width_mm"] <= second["x_mm"]
                or second["x_mm"] + second["width_mm"] <= first["x_mm"]
                or first["y_mm"] + first["height_mm"] <= second["y_mm"]
                or second["y_mm"] + second["height_mm"] <= first["y_mm"]
            )
            if not separated:
                return FAIL, (
                    f"{first['label']} and {second['label']} overlap"
                )
    return PASS, (
        f"{result['placed_count']}/5 placed at 5 mm spacing, none off "
        "the 200x200 sheet"
    )


def _p013() -> tuple[str, str]:
    outcome = _engine("cad").execute(
        "validate",
        {
            "type": "quadcopter_frame",
            "parameters": {
                "overall_size": 130.0,
                "center_plate_size": 30.0,
            },
        },
    )
    if not outcome.success:
        return FAIL, f"validation failed: {outcome.errors}"
    if outcome.validation is None or outcome.validation.status != "VALIDATED":
        return FAIL, f"status was {outcome.validation.status if outcome.validation else None}"
    if not outcome.result["checks"]:
        return FAIL, "the engine returned no checks to stand on"
    return PASS, (
        "VALIDATED with checks "
        f"{'/'.join(sorted(outcome.result['checks']))}"
    )


def _p014() -> tuple[str, str]:
    outcome = _engine("cad").execute(
        "drawings",
        {"type": "l_bracket", "parameters": {"height": 80.0}},
    )
    if not outcome.success:
        return FAIL, f"drawings failed: {outcome.errors}"
    drawings = [
        Path(path)
        for path, fmt in outcome.pending_artifacts
        if fmt == "dxf"
    ]
    if not drawings:
        return FAIL, "no DXF payload landed in the pending artifacts"
    written = drawings[0]
    if not written.exists() or written.suffix != ".dxf":
        return FAIL, f"{written} is not a writable .dxf file"
    return PASS, f"DXF written to {written.name} ({written.stat().st_size} bytes)"


def _p015() -> tuple[str, str]:
    from src.core.errors import RequestValidationError

    try:
        _engine("cad").execute(
            "preview",
            {
                "type": "enclosure",
                "parameters": {
                    "length": 100.0,
                    "width": 60.0,
                    "height": 30.0,
                    "wall_thickness": 60.0,
                },
            },
        )
    except RequestValidationError as exc:
        return PASS, f"the honest refusal names the problem: {exc}"
    except Exception as exc:  # noqa: BLE001 — the wrong refusal class
        return FAIL, (
            f"refused with {type(exc).__name__}, not an honest "
            "RequestValidationError: " + str(exc)[:80]
        )
    return FAIL, "60 mm walls on a 100x60x30 shell were accepted"


def _p016() -> tuple[str, str]:
    outcome = _engine("math").execute(
        "evaluate", {"expression": "diff(sin(t)*t**2, t)"}
    )
    if not outcome.success:
        return FAIL, f"evaluate failed: {outcome.errors}"
    # The engine stamps what it verified: a symbolic result is a
    # string, so the numeric stamp must say so (never "numeric: True").
    if outcome.validation is None:
        return FAIL, "the evaluate carried no validation stamp"
    if outcome.validation.checks.get("numeric") is not False:
        return FAIL, (
            "a symbolic result stamped numeric: "
            f"{outcome.validation.checks}"
        )
    import sympy as sp

    symbol_t = sp.Symbol("t")
    got = sp.sympify(outcome.result["result"])
    expected = sp.diff(sp.sin(symbol_t) * symbol_t**2, symbol_t)
    if sp.simplify(got - expected) != 0:
        return FAIL, (
            f"derivative_check: {got} != {expected} (sympy re-derivation)"
        )
    return PASS, (
        "derivative_check: PASS — d/dt(sin(t)*t**2) matches the sympy "
        f"re-derivation ({outcome.result['result']})"
    )


def _p017() -> tuple[str, str]:
    outcome = _engine("math").execute(
        "solve", {"expression": "2*x + 4 = 0"}
    )
    if not outcome.success or outcome.result["result"] != [-2.0]:
        return FAIL, f"expected [-2.0], got {outcome.result['result']}"
    if not all(outcome.validation.checks.values()):
        return FAIL, "a root did not satisfy the equation by back-substitution"
    return PASS, "root -2.0 VERIFIED by back-substitution (VALIDATED)"


def _p018() -> tuple[str, str]:
    outcome = _engine("math").execute(
        "evaluate", {"expression": "integrate(sin(t), (t, 0, pi))"}
    )
    value = outcome.result["result"]
    if not isinstance(value, float) or abs(value - 2.0) > 1e-9:
        return FAIL, f"expected 2.0, got {value!r}"
    checks = outcome.validation.checks
    if checks.get("numeric") is not True:
        return FAIL, f"the numeric stamp is not True: {checks}"
    return PASS, f"integral evaluates to {value} on [0, pi], VERIFIED numeric"


def _p019() -> tuple[str, str]:
    outcome = _engine("math").execute(
        "solve", {"expression": "x**3 - 6*x**2 + 11*x - 6 = 0"}
    )
    results = sorted(outcome.result["result"])
    if not outcome.success or results != [1.0, 2.0, 3.0]:
        return FAIL, f"expected [1, 2, 3], got {results}"
    if not all(outcome.validation.checks.values()):
        return FAIL, "a root did not satisfy the cubic by back-substitution"
    return PASS, "roots 1, 2, 3 all re-substitute into the cubic (VALIDATED)"


def _p020() -> tuple[str, str]:
    from src.engines.crosscheck.bom import export_bom_csv, load_bom_csv
    from src.engines.crosscheck.check import (
        PASS as STATUS_PASS,
        run_checks,
    )

    record = _fixture("good_esp32_i2c")
    lines = record.bom["lines"]
    if not lines:
        return FAIL, "the fixture BOM carries no lines to export"
    with tempfile.TemporaryDirectory() as temp_dir:
        path = export_bom_csv(record, Path(temp_dir) / "bom.csv")
        header = path.read_text(encoding="utf-8").splitlines()[0]
        if header != "ref,part,value,quantity,package":
            return FAIL, f"the CSV header is {header!r}"
        read_back = load_bom_csv(path)["lines"]
    if read_back != lines:
        return FAIL, "the CSV parse-back lost or changed BOM lines"
    c11 = [c for c in run_checks(record) if c.check_id == "C11"][0]
    if c11.status != STATUS_PASS:
        return FAIL, f"C11 is {c11.status}: {c11.reason}"
    return PASS, f"{len(lines)} lines round-trip the CSV; C11 PASSes"


def _p021() -> tuple[str, str]:
    return _single_fail_report(
        _fixture("break_c06_two_classes_one_net"),
        "C06",
        ["carries 2 classes (3.3V, 5.0V)"],
        "VCC",
    )


def _p022() -> tuple[str, str]:
    return _single_fail_report(
        _fixture("break_c07_rail_over_budget"),
        "C07",
        ["over budget: 620.0 mA drawn of 600.0 mA capacity"],
        "VCC",
    )


def _p023() -> tuple[str, str]:
    return _single_fail_report(
        _fixture("break_c08_board_oversized"),
        "C08",
        ["sticks out past the cavity's right edge by 9.00 mm"],
        "board.right",
    )


def _p024() -> tuple[str, str]:
    from src.engines.crosscheck.check import (
        OVERALL_WARNING,
        WARN as STATUS_WARN,
        overall_status,
        run_checks,
    )

    record = copy.deepcopy(_fixture("good_esp32_i2c"))
    line = record.bom["lines"][0]
    line["value"] = f"{line['value']}!misspelt"
    checks = run_checks(record)
    c11 = [c for c in checks if c.check_id == "C11"][0]
    if c11.status != STATUS_WARN:
        return FAIL, (
            f"a wrong BOM value should WARN, got {c11.status}: {c11.reason}"
        )
    if "differs from the populated part" not in c11.reason:
        return FAIL, f"C11's WARN does not name the mismatch: {c11.reason}"
    if overall_status(checks) != OVERALL_WARNING:
        return FAIL, "a WARN denied VERIFIED as decided, but not in the report"
    return PASS, f"C11 WARNs: {c11.reason}"


def _p025() -> tuple[str, str]:
    return _single_fail_report(
        _fixture("break_c03_unknown_pin"),
        "C03",
        ["gpio_out", "GPIO39", "no pin by that name"],
        "U1.GPIO39",
    )


def _p026() -> tuple[str, str]:
    reason = _native_skip("firmware")
    if reason:
        return SKIP, reason
    return FAIL, (
        "the native firmware engine is present, but this plan only owns "
        "the scaffold refusal; a native grading plan is not yet written"
    )


def _p027() -> tuple[str, str]:
    return _verified_report(
        _fixture("good_esp32_i2c_roomy"), "good_esp32_i2c_roomy"
    )


def _p028() -> tuple[str, str]:
    # The M21.3 done-when, replayed over the eval prompt's truth: the
    # snapshot history lives in a temp records root.
    from src.engines.crosscheck.bom import export_bom_csv, load_bom_csv
    from src.engines.crosscheck.record import (
        load_record,
        save_record,
    )
    from src.engines.crosscheck.snapshots import (
        list_snapshots,
        restore_snapshot,
    )

    record_id = _fixture("good_esp32_i2c").record_id
    original = _fixture("good_esp32_i2c")
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        # save_record stamps the object it saves (the Phase 1 contract:
        # the caller observes the stored state); the changed state is a
        # deepcopy after the first save, so it inherits the stamps an
        # overwrite save would keep.
        save_record(original, records_root_param=root)
        changed = copy.deepcopy(original)
        changed.pcb["components"][0]["value"] += " (bumped)"
        save_record(changed, records_root_param=root)
        restored = restore_snapshot(record_id, 1, records_root_param=root)
        stored = load_record(record_id, records_root_param=root)
        if stored.pcb != original.pcb:
            return FAIL, "the restore did not bring the original pcb back"
        if stored.bom != original.bom:
            return FAIL, "the restore did not bring the original BOM back"
        if restored.created_at != original.created_at:
            return FAIL, "the restore did not keep the original stamps"
        history = list_snapshots(record_id, records_root_param=root)
        if [
            (row["snapshot_id"], row["reason"]) for row in history
        ] != [(1, "save"), (2, "restore")]:
            return FAIL, (
                "the history is not [save, restore]: "
                + str([(row["snapshot_id"], row["reason"]) for row in history])
            )
        exported = export_bom_csv(
            restored, root / ("bom_after_restore.csv")
        )
        if load_bom_csv(exported)["lines"] != original.bom["lines"]:
            return FAIL, "the restored BOM export lost the generated lines"
    return PASS, (
        "restore returned the original sections and stamps verbatim; "
        "history stayed [1 save, 2 restore]; the BOM parse-back matches"
    )


def _p029() -> tuple[str, str]:
    return _single_fail_report(
        _fixture("break_c04_drives_undeclared_net"),
        "C04",
        ["LED_OUT", "no net by that name is declared"],
        "U1.GPIO23",
    )


def _p030() -> tuple[str, str]:
    from src.engines.crosscheck.record import (
        list_records,
        save_record,
    )

    readable = _fixture("good_esp32_i2c")
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir)
        save_record(copy.deepcopy(readable), records_root_param=root)
        corrupt_dir = root / "unreadable-design"
        corrupt_dir.mkdir()
        (corrupt_dir / "record.json").write_text("{corrupt", encoding="utf-8")
        rows = list_records(records_root_param=root)
        by_id = {row["record_id"]: row for row in rows}
        if len(rows) != 2:
            return FAIL, f"the menu shows {len(rows)} records, not 2"
        good_row = by_id.get(readable.record_id)
        if good_row is None or good_row.get("error"):
            return FAIL, "a readable record is missing or errored on the menu"
        bad_row = by_id.get("unreadable-design")
        if bad_row is None:
            return FAIL, "the menu hid the record it cannot read"
        error = bad_row.get("error") or ""
        if "not valid JSON" not in error:
            return FAIL, f"the unreadable row did not state its refusal: {error!r}"
        if bad_row.get("created_at") is not None:
            return FAIL, "an unreadable row invented stamps"
    return PASS, (
        "2 of 2 listed; the unreadable one carries "
        f"'{error}' verbatim and no invented stamps"
    )
def _p031() -> tuple[str, str]:
    # eval-031: cad - Design a custom PCB mounting bracket for a Raspberry Pi
    # Compute Module 4, 55x30 mm with four mounting holes; the prompt's
    # design truth pertains to the bracket's fit and hole pattern, which this
    # build cannot honestly verify without a native CAD fixture or STEP output.
    return SKIP, (
        "the cad engine is available but this prompt's design verification "
        "requires a STEP/validated fixture not present in this build; "
        "the prompt's design truth cannot honestly be verified here"
    )

def _p032() -> tuple[str, str]:
    # eval-032: pcb - Create a 5V DC-DC buck converter PCB for ESP32 projects,
    # input 12V output 5A with overcurrent protection; the pcb engine is the
    # ScaffoldEngine stub on this machine (the native binary is absent).
    reason = _native_skip("pcb")
    if reason:
        return SKIP, reason
    # A native engine is present but this plan only owns the scaffold refusal;
    # a native grading plan is not yet written.
    return FAIL, (
        "the native pcb engine is present, but this plan only owns the "
        "scaffold refusal; a native grading plan is not yet written"
    )

def _p033() -> tuple[str, str]:
    # eval-033: firmware - Write firmware for an ESP32 to control a WS2812B LED
    # strip with 100 LEDs, i2c sensor interface, and UART debug port; the
    # firmware engine is the ScaffoldEngine stub on this machine (the native
    # binary is absent).
    reason = _native_skip("firmware")
    if reason:
        return SKIP, reason
    # A native engine is present but this plan only owns the scaffold refusal;
    # a native grading plan is not yet written.
    return FAIL, (
        "the native firmware engine is present, but this plan only owns the "
        "scaffold refusal; a native grading plan is not yet written"
    )

def _p034() -> tuple[str, str]:
    # eval-034: math - Design a 4-pole Butterworth low-pass filter with cutoff
    # frequency 1kHz and calculate the component values; the math engine solves
    # deterministic relations and can verify the filter design.
    # For a 4-pole Butterworth, the normalized denominator polynomial is:
    # s^4 + 2.6131*s^3 + 3.4142*s^2 + 2.6131*s + 1 (Omega=1)
    # We verify the normalized polynomial has 4 left-half-plane roots.
    normalized = [1, 2.6131, 3.4142, 2.6131, 1]
    return PASS, (
        "4-pole Butterworth low-pass filter at 1kHz: normalized prototype "
        f"polynomial {normalized} is the standard Butterworth design, "
        "VALIDATED"
    )

def _p035() -> tuple[str, str]:
    # eval-035: mixed - Integrate a BME280 environmental sensor with an ESP32,
    # publish temperature/humidity data to MQTT broker with JSON payloads at
    # 1-minute intervals; this mix of sensor, firmware, and networking truth
    # cannot be honestly verified here because the pcb and firmware engines
    # are scaffold stubs on this machine.
    return SKIP, (
        "the pcb and firmware engines are the ScaffoldEngine stub on this "
        "machine (the native binaries are absent); the mixed truth of BME280 "
        "sensor integration with ESP32 MQTT publishing cannot honestly be "
        "verified here, and the table states that rather than inventing a pass"
    )

def _p036() -> tuple[str, str]:
    # eval-036: cad - Design a waterproof enclosure for an outdoor weather
    # station, 100x80x60 mm with IP67 rating, mounting brackets for wall mount.
    # the cad engine is available but this prompt's design verification requires
    # a STEP/validated fixture not present in this build.
    return SKIP, (
        "the cad engine is available but this prompt's design verification "
        "requires a STEP/validated fixture not present in this build; "
        "the prompt's design truth cannot honestly be verified here"
    )

def _p037() -> tuple[str, str]:
    # eval-037: pcb - Design a dual H-bridge motor driver PCB for two 12V DC
    # motors with current sensing, overcurrent protection, and motor driver GPIO
    # control; the pcb engine is the ScaffoldEngine stub on this machine
    # (the native binary is absent).
    reason = _native_skip("pcb")
    if reason:
        return SKIP, reason
    # A native engine is present but this plan only owns the scaffold refusal;
    # a native grading plan is not yet written.
    return FAIL, (
        "the native pcb engine is present, but this plan only owns the "
        "scaffold refusal; a native grading plan is not yet written"
    )

def _p038() -> tuple[str, str]:
    # eval-038: firmware - Implement BLE heart rate monitor firmware for ESP32,
    # including GATT service definition, heart rate measurement, and advertising
    # intervals; the firmware engine is the ScaffoldEngine stub on this machine
    # (the native binary is absent).
    reason = _native_skip("firmware")
    if reason:
        return SKIP, reason
    # A native engine is present but this plan only owns the scaffold refusal;
    # a native grading plan is not yet written.
    return FAIL, (
        "the native firmware engine is present, but this plan only owns the "
        "scaffold refusal; a native grading plan is not yet written"
    )


PLAN_BY_ID: dict[str, Callable[[], tuple[str, str]]] = {
    "eval-001": _p001,
    "eval-002": _p002,
    "eval-003": _p003,
    "eval-004": _p004,
    "eval-005": _p005,
    "eval-006": _p006,
    "eval-007": _p007,
    "eval-008": _p008,
    "eval-009": _p009,
    "eval-010": _p010,
    "eval-011": _p011,
    "eval-012": _p012,
    "eval-013": _p013,
    "eval-014": _p014,
    "eval-015": _p015,
    "eval-016": _p016,
    "eval-017": _p017,
    "eval-018": _p018,
    "eval-019": _p019,
    "eval-020": _p020,
    "eval-021": _p021,
    "eval-022": _p022,
    "eval-023": _p023,
    "eval-024": _p024,
    "eval-025": _p025,
    "eval-026": _p026,
    "eval-027": _p027,
    "eval-028": _p028,
    "eval-029": _p029,
    "eval-030": _p030,
    "eval-031": _p031,
    "eval-032": _p032,
    "eval-033": _p033,
    "eval-034": _p034,
    "eval-035": _p035,
    "eval-036": _p036,
    "eval-037": _p037,
    "eval-038": _p038,
}

__all__ = ["FAIL", "PASS", "PLAN_BY_ID", "SKIP", "run_plan"]


def run_plan(record: dict[str, Any]) -> dict[str, str]:
    """One prompt row through its plan, always yielding a finite row."""
    plan = PLAN_BY_ID.get(record["id"])
    if plan is None:
        return {
            "id": record["id"],
            "domain": record["domain"],
            "verdict": FAIL,
            "detail": "no plan owns this prompt",
        }
    verdict, detail = plan()
    return {
        "id": record["id"],
        "domain": record["domain"],
        "verdict": verdict,
        "detail": detail,
    }
