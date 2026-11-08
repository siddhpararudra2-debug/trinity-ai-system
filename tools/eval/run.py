"""M22's one command: ``python tools/eval/run.py``.

Loads ``data/evals/prompts.jsonl`` under the seed contract decided in
M20.e — one prompt per line, exactly ``{id, domain, prompt, source}``.
An ``expected`` field is refused outright: expected-verdict grading is
owned by the plans, never by the prompt data, and models never grade.

Executes exactly one deterministic plan per prompt (tools/eval/plans),
prints the pass/fail table by id, and exits 0 when no plan FAILs —
SKIPs stay in the table, honestly unverified — and 1 otherwise.

Engine writes never touch the repo: the storage root is redirected to
a process-private temp dir before src is imported (a pre-set
``TRINITY_STORAGE_ROOT`` is honored as the operator's explicit choice).
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]

_PROMPTS_RELATIVE = Path("data") / "evals" / "prompts.jsonl"

_DOMAINS = {"cad", "pcb", "firmware", "math", "mixed"}

# The eval record's only key set (decided.md M20.e: no ``expected``
# field; verdicts belong to the plans, so ``tools/eval/plans`` keeps
# the same contract).
_PROMPT_KEYS = {"id", "domain", "prompt", "source"}

#: When set, the runner FAILs instead of SKIPing prompts whose engine
#: is the ScaffoldEngine stub (native binary absent). Meant for CI / Windows
#: run enforcement: ``TRINITY_REQUIRE_NATIVE=1 python -m tools.eval.run``.
REQUIRE_NATIVE = False


def _redirect_storage() -> None:
    """Point engine writes at an isolated root, never at repo data."""
    os.environ.setdefault(
        "TRINITY_STORAGE_ROOT",
        tempfile.mkdtemp(prefix="trinity_eval_storage_"),
    )


def load_prompts(path: Path) -> list[dict[str, Any]]:
    """Read the eval set strictly; refuse anything this build cannot use.

    The refused cases are stated, each at its file line: a non-dict
    row, a missing key, an ``expected`` field (verdicts belong to the
    plans), an unknown domain, a malformed id, and a duplicate id.
    """
    lines = path.read_text(encoding="utf-8").splitlines()
    prompts: list[dict[str, Any]] = []
    seen: dict[str, int] = {}
    for number, raw in enumerate(lines, start=1):
        if not raw.strip():
            continue
        location = f"{path}:{number}"
        try:
            row = json.loads(raw)
        except json.JSONDecodeError as exc:
            raise ValueError(
                f"{location}: not valid JSON ({exc}); the eval set is "
                "one JSON object per line"
            ) from exc
        if not isinstance(row, dict):
            raise ValueError(
                f"{location}: an eval record is an object, got "
                f"{type(row).__name__}"
            )
        extra = sorted(set(row) - _PROMPT_KEYS)
        if extra:
            raise ValueError(
                f"{location}: an eval record carries unexpected keys "
                f"{extra} - verdicts belong to the plans, so an "
                "'expected' field never rides on a prompt"
            )
        missing = sorted(_PROMPT_KEYS - set(row))
        if missing:
            raise ValueError(
                f"{location}: an eval record is missing keys {missing}"
            )
        if row["domain"] not in _DOMAINS:
            raise ValueError(
                f"{location}: domain {row['domain']!r} is not one of "
                f"{sorted(_DOMAINS)}"
            )
        eval_id = row["id"]
        if not isinstance(eval_id, str) or not eval_id.startswith("eval-"):
            raise ValueError(f"{location}: id {eval_id!r} is not an eval id")
        if eval_id in seen:
            raise ValueError(
                f"{location}: duplicate id {eval_id!r} (first seen at "
                f"line {seen[eval_id]})"
            )
        if not row["prompt"] or not isinstance(row["prompt"], str):
            raise ValueError(f"{location}: the prompt text is empty")
        if not row["source"] or not isinstance(row["source"], str):
            raise ValueError(f"{location}: the source is empty")
        seen[eval_id] = number
        prompts.append(row)
    if not prompts:
        raise ValueError(f"{path}: the eval set is empty")
    return prompts


def _print_table(rows: list[dict[str, str]]) -> None:
    print(
        f"Trinity AI eval runner - M22 ({len(rows)} prompts, "
        f"source: {_PROMPTS_RELATIVE.as_posix()})"
    )
    print()
    print(f"{'id':10} {'domain':9} {'verdict':8} detail")
    for row in rows:
        print(
            f"{row['id']:10} {row['domain']:9} {row['verdict']:8} "
            f"{row['detail'][:120]}"
        )
    passes = sum(1 for row in rows if row["verdict"] == "PASS")
    fails = sum(1 for row in rows if row["verdict"] == "FAIL")
    skips = sum(1 for row in rows if row["verdict"] == "SKIP")
    print()
    print(f"summary: {passes} PASS / {fails} FAIL / {skips} SKIP of {len(rows)}")


def main(argv: list[str] | None = None) -> int:
    # Support ``--require-native`` so that, on a machine where the native
    # binary is absent, prompts that would SKIP are instead FAILED.  This
    # is intended for CI / Windows‑run enforcement.
    if argv is None and "--require-native" in sys.argv:
        os.environ["TRINITY_REQUIRE_NATIVE"] = "1"
    elif argv is not None and "--require-native" in argv:
        os.environ["TRINITY_REQUIRE_NATIVE"] = "1"
    _redirect_storage()
    if str(REPO_ROOT) not in sys.path:
        sys.path.insert(0, str(REPO_ROOT))
    from tools.eval import plans  # noqa: PLC0415 — after the redirect

    try:
        prompts = load_prompts(REPO_ROOT / _PROMPTS_RELATIVE)
    except ValueError as exc:
        print(f"eval set refused: {exc}")
        return 1
    rows = [plans.run_plan(record) for record in prompts]
    # If --require-native was given, upgrade any SKIP whose detail mentions
    # the native binary being absent to a FAIL — this enforces honest
    # verdicts when a native build is required.
    if os.getenv("TRINITY_REQUIRE_NATIVE"):
        for row in rows:
            if row["verdict"] == "SKIP" and "native binary is absent" in row.get("detail", ""):
                row["verdict"] = "FAIL"
    _print_table(rows)
    return 1 if any(row["verdict"] == "FAIL" for row in rows) else 0


if __name__ == "__main__":
    try:  # never crash the table on a cp1252 console
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    sys.exit(main())
