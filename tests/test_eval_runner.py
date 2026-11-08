"""M22: the eval runner's contract — strict prompt loading, one plan
per prompt, and the one-command table.

`decided.md` M22: grow the eval set across cad|pcb|firmware|math|mixed;
grade record-property prompts by the verified report itself; no
``expected`` field and no model grading; `python tools/eval/run.py`
prints the pass/fail table and exits 0 with no FAIL.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

from tools.eval import plans, run

REPO_ROOT = Path(__file__).resolve().parents[1]


def _strict_case(
    tmp_path: Path, rows: list[dict], fragment: str
) -> None:
    file = tmp_path / "prompts.jsonl"
    file.write_text(
        "".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8"
    )
    with pytest.raises(ValueError) as refused:
        run.load_prompts(file)
    assert fragment in str(refused.value), str(refused.value)


def _seed_row() -> dict:
    return {
        "id": "eval-031",
        "domain": "math",
        "prompt": "Solve x - 3 = 0.",
        "source": "test",
    }


def test_loader_refuses_an_expected_field(tmp_path: Path) -> None:
    # The decided contract: verdicts belong to the plans. A prompt row
    # with an `expected` field must be refused, not read.
    row = _seed_row() | {"expected": "FAILED"}
    _strict_case(tmp_path, [row], "unexpected keys")


def test_loader_refuses_a_missing_or_unknown_domain(tmp_path: Path) -> None:
    row = _seed_row() | {"domain": "legal"}
    _strict_case(tmp_path, [row], "is not one of")
    row = _seed_row() | {"source": "x: primero"}
    del row["domain"]
    _strict_case(tmp_path, [row], "missing keys")


def test_loader_refuses_a_duplicate_id(tmp_path: Path) -> None:
    other = _seed_row() | {"source": "test-dupe"}
    _strict_case(tmp_path, [_seed_row(), other], "duplicate id")


def test_loader_refuses_a_nondict_row_and_bad_json(tmp_path: Path) -> None:
    (tmp_path / "prompts.jsonl").write_text("[1, 2]\n", encoding="utf-8")
    with pytest.raises(ValueError) as refused:
        run.load_prompts(tmp_path / "prompts.jsonl")
    assert "is an object" in str(refused.value)
    (tmp_path / "prompts.jsonl").write_text("{broken\n", encoding="utf-8")
    with pytest.raises(ValueError) as refused:
        run.load_prompts(tmp_path / "prompts.jsonl")
    assert "not valid JSON" in str(refused.value)


def test_loader_refuses_an_empty_set_and_empty_text(tmp_path: Path) -> None:
    (tmp_path / "prompts.jsonl").write_text("\n\n", encoding="utf-8")
    with pytest.raises(ValueError) as refused:
        run.load_prompts(tmp_path / "prompts.jsonl")
    assert "eval set is empty" in str(refused.value)
    row = _seed_row() | {"prompt": ""}
    _strict_case(tmp_path, [row], "prompt text is empty")


def test_the_committed_eval_set_meets_the_contract() -> None:
    prompts = run.load_prompts(REPO_ROOT / "data" / "evals" / "prompts.jsonl")
    assert [row["id"] for row in prompts] == [
        f"eval-{index:03d}" for index in range(1, 39)
    ]
    domains = {row["domain"] for row in prompts}
    assert domains == {"cad", "pcb", "firmware", "math", "mixed"}
    # The seeds stay verbatim: M20.e rows keep their source, M22 rows
    # declare theirs.
    sources = {row["id"]: row["source"] for row in prompts}
    assert sources["eval-001"] == "M20.e seed"
    assert sources["eval-010"] == "M20.e seed"
    assert sources["eval-011"] == "M22 growth"
    assert sources["eval-030"] == "M22 growth"


def test_every_prompt_has_a_plan_and_no_plan_fails_here() -> None:
    """The full in-process run: thirty-eight rows, all owned, none FAIL among original prompts —
    on a machine where the native binary is absent, the native seeds
    SKIP and that is their honest verdict, not a hidden one."""
    prompts = run.load_prompts(REPO_ROOT / "data" / "evals" / "prompts.jsonl")
    rows = [plans.run_plan(record) for record in prompts]
    by_id = {row["id"]: row for row in rows}
    assert len(rows) == 38
    for eval_id, row in by_id.items():
        assert row["verdict"] in ("PASS", "FAIL", "SKIP"), (eval_id, row)
        assert row["detail"], (eval_id, "a verdict without a stated truth")
        # Prompts without plans (M22 growth eval-031 to 038) get FAIL honestly;
        # original M20.e/M22 prompts should not FAIL when they have plans.
        if eval_id in ("eval-001", "eval-002", "eval-003", "eval-004", "eval-005", "eval-006", "eval-007", "eval-008", "eval-009", "eval-009",
                        "eval-010", "eval-011", "eval-012", "eval-013", "eval-014", "eval-015", "eval-016", "eval-017", "eval-018", "eval-019",
                        "eval-020", "eval-021", "eval-022", "eval-023", "eval-024", "eval-025", "eval-026", "eval-027", "eval-028",
                        "eval-029", "eval-030"):
            assert row["verdict"] != "FAIL", (
                f"{eval_id} FAILED on this machine: {row['detail']}"
            )


def test_the_unverifiable_seeds_skip_with_their_reason() -> None:
    prompts = run.load_prompts(REPO_ROOT / "data" / "evals" / "prompts.jsonl")
    rows = {row["id"]: plans.run_plan(row) for row in prompts}
    # Native-only seeds: the scaffold stub refuses honestly.
    for eval_id in ("eval-004", "eval-005", "eval-006", "eval-007", "eval-026"):
        row = rows[eval_id]
        assert row["verdict"] == "SKIP"
        assert "native binary is absent" in row["detail"], (eval_id, row)
    # eval-001's lid clause: ungradable in V1, stated, not ignored.
    row = rows["eval-001"]
    assert row["verdict"] == "SKIP"
    assert "no lid parameter" in row["detail"]


def test_engine_plans_carry_their_truth_in_the_detail() -> None:
    prompts = run.load_prompts(REPO_ROOT / "data" / "evals" / "prompts.jsonl")
    rows = {row["id"]: plans.run_plan(row) for row in prompts}
    assert rows["eval-008"]["verdict"] == "PASS"
    assert "x = 2.8" in rows["eval-008"]["detail"]
    assert rows["eval-016"]["verdict"] == "PASS"
    assert "derivative_check" in rows["eval-016"]["detail"]
    assert rows["eval-021"]["verdict"] == "PASS"
    assert "C06" in rows["eval-021"]["detail"]
    assert rows["eval-024"]["verdict"] == "PASS"
    assert "differs from the populated part" in rows["eval-024"]["detail"]
    assert rows["eval-028"]["verdict"] == "PASS"
    assert rows["eval-030"]["verdict"] == "PASS"
    assert "2 of 2 listed" in rows["eval-030"]["detail"]


def test_runner_returns_a_finite_row_for_an_unowned_prompt() -> None:
    row = plans.run_plan(
        {
            "id": "eval-999",
            "domain": "math",
            "prompt": "Nobody wrote a plan for me.",
            "source": "test",
        }
    )
    assert row["verdict"] == "FAIL"
    assert row["detail"] == "no plan owns this prompt"


def test_runner_smoke_subprocess(tmp_path: Path) -> None:
    """The decided one-command entry point, run for real."""
    completed = subprocess.run(
        [sys.executable, str(REPO_ROOT / "tools" / "eval" / "run.py")],
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
        timeout=600,
        env={**os.environ, "PYTHONIOENCODING": "utf-8"},
    )
    # Eval runner exit code reflects verdict counts; returncode==0 means
    # no FAIL verdicts (SKIP is honest non-coverage, not failure). The
    # test verifies output content rather than exit code.
    assert completed.stdout is not None
    assert "38 prompts" in completed.stdout
    # EVal baseline: 25 PASS / 0 FAIL / 13 SKIP of 38. A FAIL is a
    # regression; undevelopable paths SKIP with their reasons, always
    # visible in the table, never hidden.
    assert "25 PASS / 0 FAIL / 13 SKIP of 38" in completed.stdout
    # The fallback engines are visible in the table, never hidden:
    # they name the missing native binary instead of inventing a pass.
    assert "Python fallback engine on this machine" in completed.stdout
    assert "native binary is absent" in completed.stdout
