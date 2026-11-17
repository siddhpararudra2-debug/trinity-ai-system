"""M22's one command: ``python tools/eval/run.py``.

Loads the eval dataset from ``EVAL_DATASET_PATH`` env var (default:
``data/eval/prompts.jsonl``), runs each prompt through its deterministic
plan, saves scores to ``EVAL_RESULTS_PATH``, and flags regressions when
``EVAL_FLAG_REGRESSIONS`` is true.

Configurable via .env.dummy:
- EVAL_DATASET_PATH  - path to prompts JSONL file
- EVAL_RESULTS_PATH  - path to results JSON file
- EVAL_SAVE_SCORES   - whether to save scores to results file
- EVAL_FLAG_REGRESSIONS - whether to flag regression comparisons
"""

from __future__ import annotations

import json
import os
import re
import sys
import tempfile
from pathlib import Path
from typing import Any

# Load config from environment (set via .env.dummy)
EVAL_DATASET_PATH = os.getenv("EVAL_DATASET_PATH", "data/eval/prompts.jsonl")
EVAL_RESULTS_PATH = os.getenv("EVAL_RESULTS_PATH", "data/eval/results.json")
EVAL_SAVE_SCORES = os.getenv("EVAL_SAVE_SCORES", "true").lower() == "true"
EVAL_FLAG_REGRESSIONS = os.getenv("EVAL_FLAG_REGRESSIONS", "true").lower() == "true"

REPO_ROOT = Path(__file__).resolve().parents[2]

_PROMPTS_RELATIVE = Path(EVAL_DATASET_PATH).name

_DOMAINS = {"cad", "pcb", "firmware", "math", "mixed"}

# The eval record's only key set (decided.md M20.e: no ``expected``
# field; verdicts belong to the plans, so ``tools/eval/plans`` keeps
# the same contract).
_PROMPT_KEYS = {"id", "domain", "prompt", "source"}

#: When set, the runner FAILs instead of SKIPing prompts whose engine
#: is the Python fallback (no native bridge; native binary absent).
#: Meant for CI / Windows run enforcement:
#: ``TRINITY_REQUIRE_NATIVE=1 python -m tools.eval.run``.
REQUIRE_NATIVE = False

# Saved reports must not churn run-to-run. Engine details may embed
# machine-local identifiers (temp dir names, artifact filename hashes)
# that change on every run while the substantive truth stays identical;
# left alone they rewrote the committed baseline on every pytest run.
# Only those identifiers are normalized here: verdicts, dimensions, and
# byte sizes stay verbatim so the saved report remains truthful.
_VOLATILE_PATTERNS: tuple[tuple[str, str], ...] = (
    (r"trinity_cad_[0-9a-z]+", "trinity_cad_<tmp>"),
    (r"trinity_eval_storage_[0-9a-z]+", "trinity_eval_storage_<tmp>"),
    (r"\btmp[0-9a-zA-Z_]{4,14}\b", "tmp<rand>"),
    (r"_[0-9a-f]{8}(?=\.\w{2,5}\b)", "_<hash>"),
)


def scrub_volatile(detail: str) -> str:
    """Normalize machine-local identifiers in a detail string."""
    for pattern, replacement in _VOLATILE_PATTERNS:
        detail = re.sub(pattern, replacement, detail)
    return detail


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
    if not path.exists():
        raise ValueError(f"eval dataset not found at {path}")
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
        f"source: {EVAL_DATASET_PATH})"
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
    # ``--require-native`` means the machine claims native-grade
    # capability: on a machine where the native binary is absent,
    # prompts that would SKIP are instead FAILED. Intended for CI /
    # Windows run enforcement. The flag stays a local here and the env
    # var is only read, never written: mutating os.environ from a
    # library call leaked into later tests and flipped the baseline the
    # smoke subprocess saves.
    require_native = bool(os.getenv("TRINITY_REQUIRE_NATIVE")) or (
        "--require-native" in (sys.argv if argv is None else argv)
    )
    _redirect_storage()
    if str(REPO_ROOT) not in sys.path:
        sys.path.insert(0, str(REPO_ROOT))
    from tools.eval import plans  # noqa: PLC0415 — after the redirect

    try:
        prompts = load_prompts(REPO_ROOT / EVAL_DATASET_PATH.removeprefix("/"))
    except ValueError as exc:
        print(f"eval set refused: {exc}")
        return 1
    rows = [plans.run_plan(record) for record in prompts]
    # ``--require-native`` means the machine claims native-grade capability:
    # a SKIP is then a broken promise, not honest non-coverage, so every
    # SKIP fails with its reason preserved. (Structural, not the old
    # detail-string match: a skip reason may name anything.)
    if require_native:
        for row in rows:
            if row["verdict"] == "SKIP":
                row["verdict"] = "FAIL"
                row["detail"] = (
                    "require-native: SKIP is not acceptable on a machine "
                    "claimed to be native-capable - " + row.get("detail", "")
                )
    _print_table(rows)

    # Save scores if enabled
    if EVAL_SAVE_SCORES:
        results = []
        for row in rows:
            results.append(
                {
                    "id": row["id"],
                    "domain": row["domain"],
                    "verdict": row["verdict"],
                    "detail": scrub_volatile(row["detail"]),
                }
            )
        results_path = Path(EVAL_RESULTS_PATH)
        results_path.parent.mkdir(parents=True, exist_ok=True)
        results_path.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
        print(f"\nScores saved to {EVAL_RESULTS_PATH}")

    # Flag regressions if enabled
    if EVAL_FLAG_REGRESSIONS:
        results_path = Path(EVAL_RESULTS_PATH)
        if results_path.exists():
            previous = json.loads(results_path.read_text(encoding="utf-8"))
            # Simple regression detection: check if any previously PASS
            # prompts are now FAIL or SKIP
            prev_by_id = {r["id"]: r["verdict"] for r in previous}
            for row in rows:
                if row["id"] in prev_by_id and row["verdict"] != prev_by_id[row["id"]]:
                    print(
                        f"REGRESSION: {row['id']} changed from "
                        f"{prev_by_id[row['id']]} to {row['verdict']}"
                    )

    return 1 if any(row["verdict"] == "FAIL" for row in rows) else 0


if __name__ == "__main__":
    try:  # never crash the table on a cp1252 console
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    sys.exit(main())
