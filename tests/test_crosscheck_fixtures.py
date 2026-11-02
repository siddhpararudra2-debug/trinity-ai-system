"""M20.6: the fixture designs — 3 good + 5 broken, one check each.

The fixture names carry the contract: ``good_*`` must come out VERIFIED
(all implemented checks PASS); ``break_c<nn>_*`` must fail exactly the
check its name names — one FAIL, no WARNs, no cascade into other
checks. The committed files under ``data/fixtures/`` must always equal
what the builders produce, so the artifacts and the builders cannot
drift.
"""

from __future__ import annotations

import pytest

from src.core.config import REPO_ROOT
from src.engines.crosscheck.check import (
    CHECK_ORDER,
    PASS,
    overall_status,
    run_checks,
    status_counts,
)
from src.engines.crosscheck.fixtures import (
    build_fixture,
    fixture_names,
    fixtures_root,
    load_fixture,
)


def _target_of(name: str) -> str:
    """``break_c04_drives_undeclared_net`` names check C04."""
    assert name.startswith("break_c"), name
    target = name.split("_")[1].upper()
    assert target in CHECK_ORDER, (name, target)
    return target


@pytest.mark.parametrize("name", fixture_names())
def test_fixture_produces_its_declared_verdict(name: str) -> None:
    checks = run_checks(build_fixture(name))
    by_id = {result.check_id: result for result in checks}
    assert list(by_id) == list(CHECK_ORDER)
    if name.startswith("good_"):
        for result in checks:
            assert result.status == PASS, (
                f"{name}: {result.check_id} did not pass: {result.reason}"
            )
        assert overall_status(checks) == "VERIFIED"
        assert status_counts(checks) == {
            PASS: len(CHECK_ORDER), "WARN": 0, "FAIL": 0,
        }
        return
    target = _target_of(name)
    assert by_id[target].status == "FAIL"
    for check_id, result in by_id.items():
        if check_id != target:
            assert result.status == PASS, (
                f"{name}: cascade — {check_id} says {result.status}: "
                f"{result.reason}"
            )
    counts = status_counts(checks)
    assert counts == {PASS: len(CHECK_ORDER) - 1, "WARN": 0, "FAIL": 1}
    assert overall_status(checks) == "FAILED"


@pytest.mark.parametrize(
    ("name", "expected_fragments", "expected_location"),
    [
        ("break_c03_unknown_pin",
         ["gpio_out", "GPIO39", "no pin by that name"], "U1.GPIO39"),
        ("break_c04_drives_undeclared_net",
         ["LED_OUT", "no net by that name is declared"], "U1.GPIO23"),
        ("break_c06_two_classes_one_net",
         ["carries 2 classes (3.3V, 5.0V)"], "VCC"),
        ("break_c07_rail_over_budget",
         ["over budget: 620.0 mA drawn of 600.0 mA capacity"], "VCC"),
        ("break_c08_board_oversized",
         ["sticks out past the cavity's right edge by 9.00 mm"],
         "board.right"),
    ],
)
def test_broken_fixture_fails_with_the_specific_reason(
    name: str, expected_fragments: list[str], expected_location: str
) -> None:
    by_id = {r.check_id: r for r in run_checks(build_fixture(name))}
    result = by_id[_target_of(name)]
    for fragment in expected_fragments:
        assert fragment in result.reason, (name, fragment, result.reason)
    assert result.location == expected_location


def test_committed_fixture_files_match_the_builders() -> None:
    base = fixtures_root()
    assert base == REPO_ROOT / "data" / "fixtures"
    on_disk: set[str] = set()
    for path in sorted(base.glob("*/record.json")):
        name = path.parent.name
        on_disk.add(name)
        loaded = load_fixture(name)
        assert loaded.record_id == name
        assert loaded.to_dict() == build_fixture(name).to_dict(), (
            f"data/fixtures/{name}/record.json has drifted from the "
            "builder; run `python -m src.engines.crosscheck.fixtures` "
            "to regenerate"
        )
    # Builders without files (or files without builders) are drift too.
    assert on_disk == set(fixture_names())
