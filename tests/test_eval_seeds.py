"""M20.e: the eval prompt seeds — format and content guards.

`decided.md` (M20.e) settles the seed format: JSONL, one prompt per
line, `{id, domain, prompt, source}`, domain ∈ cad|pcb|firmware|math|
mixed. A seed carries no `expected` field — expected-verdict grading
arrives with the Phase 3 runner, where the verified report grades
record-property prompts itself. These tests pin that honestly-empty
shape: a seed with an invented expectation is a failure, not a feature.
"""

from __future__ import annotations

import json

import pytest

from src.core.config import REPO_ROOT

PROMPTS_PATH = REPO_ROOT / "data" / "evals" / "prompts.jsonl"
DOMAINS = frozenset({"cad", "pcb", "firmware", "math", "mixed"})
SEED_KEYS = frozenset({"id", "domain", "prompt", "source"})


def _load() -> list[dict]:
    """Read the JSONL back, failing a malformed line with its number."""
    text = PROMPTS_PATH.read_text(encoding="utf-8")
    records: list[dict] = []
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as exc:
            raise AssertionError(
                f"prompts.jsonl line {number} is not valid JSON: {exc}"
            ) from exc
        assert isinstance(record, dict), f"line {number} is not an object"
        records.append(record)
    return records


def test_seed_file_exists_and_parses_line_by_line() -> None:
    records = _load()
    assert records, "data/evals/prompts.jsonl has no prompts"


def test_seed_ids_are_unique_and_sequential() -> None:
    ids = [record["id"] for record in _load()]
    assert len(ids) == len(set(ids)), "duplicate prompt id"
    # M20.e seeded ten; M22 grew the set to thirty-eight — the ids stay
    # unique and sequential with no gaps.
    assert ids == [f"eval-{number:03d}" for number in range(1, 39)]


def test_every_seed_carries_exactly_the_decided_keys() -> None:
    for record in _load():
        assert set(record) == SEED_KEYS, (
            f"{record['id']} keys {sorted(record)} != {sorted(SEED_KEYS)}"
        )
        assert "expected" not in record, record["id"]


@pytest.mark.parametrize("record_index", range(30))
def test_seed_shape_and_domains(record_index: int) -> None:
    record = _load()[record_index]
    assert record["domain"] in DOMAINS, record["domain"]
    prompt = record["prompt"]
    assert isinstance(prompt, str) and len(prompt.strip()) >= 20
    # Authorship is declared per seed: the original ten are M20.e's;
    # M22's growth rows say so rather than inheriting the old source.
    if record_index < 10:
        assert record["source"] == "M20.e seed"
    else:
        assert record["source"] == "M22 growth"


def test_domain_coverage_matches_the_decided_spread() -> None:
    domains = {record["domain"] for record in _load()}
    missing = DOMAINS - domains
    assert not missing, f"seeds never cover {sorted(missing)}"
