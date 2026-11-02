"""M20.5: the verified report — assembly, hints, and the HTML route.

The report is assembled from a fresh ``run_checks`` call every time —
it is generated, never stored — and every check row carries one
deterministic, repo-owned fix hint. The HTML route serves a
self-contained page: no scripts, no external references, and every
record-derived value escaped, so a poisoned record renders as text.

``decided.md`` coverage rule the tests here pin: a record is VERIFIED
only when every *implemented* check PASSes — C11 (BOM match) is named
as inactive in the footer and never counted.
"""

from __future__ import annotations

import dataclasses
from html import escape as html_escape
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from src.engines.crosscheck import record as record_module
from src.engines.crosscheck import report as report_module
from src.engines.crosscheck.check import CHECK_ORDER, run_checks
from src.engines.crosscheck.report import (
    FIX_HINTS,
    INACTIVE_CHECKS,
    assemble_report,
    render_html,
)
from src.main import app
from tests.test_crosscheck_checks import _good_record
from tests.test_crosscheck_enclosure_checks import _record as _enclosure_record


# --- assembly --------------------------------------------------------------


def test_fix_hints_cover_exactly_the_implemented_checks() -> None:
    # Every implemented check has a hint; no hint exists for a check
    # that is not in the run order. Both directions, or the table lies.
    assert set(FIX_HINTS) == set(CHECK_ORDER)
    for hint in FIX_HINTS.values():
        assert isinstance(hint, str) and hint.strip()
    # C11 shipped with M21.1: implemented and active, so it is in the
    # run order with a hint — and the inactive list is empty.
    assert INACTIVE_CHECKS == ()
    assert "C11" in CHECK_ORDER


def test_assemble_report_matches_a_fresh_run_checks_call() -> None:
    record = _good_record()
    report = assemble_report(record)
    assert report["record_id"] == "good-design"
    checks = run_checks(record)
    assert len(report["checks"]) == len(checks) == len(CHECK_ORDER)
    for row, result in zip(report["checks"], checks):
        assert row["check_id"] == result.check_id
        assert row["name"] == result.name
        assert row["status"] == result.status
        assert row["reason"] == result.reason
        assert row["location"] == result.location
        assert row["hint"] == FIX_HINTS[result.check_id]
        assert row["hint"].strip()
    # A green record: the triad favours nothing and the tally adds up.
    assert report["overall_status"] == "VERIFIED"
    assert report["status_counts"] == {
        "PASS": len(CHECK_ORDER),
        "WARN": 0,
        "FAIL": 0,
    }


def test_assemble_report_of_an_honestly_incomplete_record_fails() -> None:
    record = record_module.build_record("broken-min")
    report = assemble_report(record)
    assert report["overall_status"] == "FAILED"
    counts = report["status_counts"]
    assert counts["FAIL"] == 1
    assert counts["FAIL"] + counts["WARN"] == len(CHECK_ORDER)
    c01 = next(row for row in report["checks"] if row["check_id"] == "C01")
    assert c01["status"] == "FAIL"


def test_a_warn_only_run_reports_warning_never_verified() -> None:
    # The enclosure fixture is fully green on the checks its sections
    # can serve, and carries no power/engine_checks annotations — the
    # electrical checks WARN cannot_verify. WARN never grants VERIFIED.
    report = assemble_report(_enclosure_record())
    assert report["overall_status"] == "WARNING"
    counts = report["status_counts"]
    assert counts["WARN"] >= 1 and counts["FAIL"] == 0
    assert counts["PASS"] + counts["WARN"] == len(CHECK_ORDER)


# --- HTML rendering --------------------------------------------------------


def _poisoned_reason_record() -> object:
    """A record whose firmware MCU model carries script markup.

    The model is embedded verbatim in C02's failure reason, so the
    escaping test exercises the exact path user data reaches the page.
    """
    pcb = {
        "board": {"width_mm": 50.0, "height_mm": 40.0,
                  "thickness_mm": 1.6,
                  "origin_mm": {"x": 0.0, "y": 0.0},
                  "layers": ["F.Cu"]},
        "components": [
            {"ref": "U1", "value": "ESP32-WROOM-32", "type": "mcu",
             "footprint": "ESP32-WROOM-32", "layer": "F.Cu",
             "rotation_deg": 0.0, "pins": []},
        ],
        "nets": [],
        "tracks": [],
        "footprints": {},
        "placements": [],
        "rules": [],
        "metadata": {},
    }
    firmware = {
        "mcu": {"model": "<script>alert('x')</script>",
                "family": "esp32"},
        "peripherals": [],
        "pin_map": [],
    }
    return record_module.build_record(
        "poisoned", pcb=pcb, firmware=firmware
    )


def test_render_html_escapes_record_derived_values() -> None:
    page = render_html(assemble_report(_poisoned_reason_record()))
    # The poisoned model renders as inert text...
    assert "&lt;script&gt;alert(&#x27;x&#x27;)&lt;/script&gt;" in page
    # ...never as markup: no scripts, no external references at all.
    assert "<script" not in page
    assert "<link" not in page
    assert "url(" not in page
    assert "onerror" not in page
    assert "onload" not in page


def test_render_html_banner_reflects_the_status() -> None:
    verified = render_html(assemble_report(_good_record()))
    assert 'class="banner banner-verified"' in verified
    # The stylesheet legitimate contains every rule; assert the actual
    # attribute, not the class name in isolation.
    assert 'class="banner banner-failed"' not in verified
    assert ">VERIFIED<" in verified

    failed = render_html(assemble_report(record_module.build_record("x")))
    assert 'class="banner banner-failed"' in failed
    assert 'class="banner banner-verified"' not in failed
    assert ">FAILED<" in failed

    warned = render_html(assemble_report(_enclosure_record()))
    assert 'class="banner banner-warning"' in warned
    assert ">WARNING<" in warned


def test_render_html_shows_hints_only_where_a_fix_is_due() -> None:
    page = render_html(assemble_report(_good_record()))
    # All PASS: no fix hints anywhere.
    assert "<b>Fix:</b>" not in page

    page = render_html(assemble_report(_enclosure_record()))
    assert "<b>Fix:</b>" in page
    # The hint text is the repo-owned one, echoed verbatim after
    # escaping (apostrophes render as entities on the page).
    assert html_escape(FIX_HINTS["C06"], quote=True) in page


def test_render_html_footer_states_semantics_and_honesty() -> None:
    page = render_html(assemble_report(_good_record()))
    lowered = page.lower()
    # The decided status semantics, spelled out on the page.
    assert "a warn never grants verified" in lowered
    # No PDF generator is vendored — the page says so, and offers the
    # browser's print-to-PDF as the export path.
    assert "print this page to pdf" in lowered
    assert "no pdf generator is vendored" in lowered
    # Every implemented check runs (C11 shipped with M21.1): no
    # inactive-checks paragraph may claim otherwise.
    assert "inactive checks" not in lowered


def test_render_html_names_inactive_checks_when_any_exist(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # The report's honesty convention (decided.md convention 8): a
    # named-but-inactive check is stated in the footer, never counted.
    monkeypatch.setattr(report_module, "INACTIVE_CHECKS", ("C11",))
    page = render_html(assemble_report(_good_record()))
    assert "C11" in page
    assert "is not implemented yet" in page
    assert "never counted in this report" in page


# --- the route -------------------------------------------------------------


@pytest.fixture()
def _record_storage(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """Point the record module's storage seam at a temp root.

    ``src.engines.crosscheck.record`` reads ``settings.storage_root``
    at call time, and ``Settings`` is a frozen dataclass: swap the
    module-level name for a replaced copy, never mutate in place.
    """
    monkeypatch.setattr(
        record_module,
        "settings",
        dataclasses.replace(record_module.settings, storage_root=tmp_path),
    )
    return tmp_path


def test_route_serves_the_generated_report(_record_storage: Path) -> None:
    record_module.save_record(_good_record())
    with TestClient(app) as client:
        response = client.get("/api/projects/good-design/report")
    assert response.status_code == 200
    assert response.headers["content-type"].startswith("text/html")
    body = response.text
    assert body.startswith("<!DOCTYPE html>")
    assert 'class="banner banner-verified"' in body
    assert "good-design" in body


def test_route_404s_an_unknown_record(_record_storage: Path) -> None:
    with TestClient(app) as client:
        response = client.get("/api/projects/no-such-record/report")
    assert response.status_code == 404
    # main.py's StarletteHTTPException handler owns the error envelope;
    # the route's HTTPException lands in it as an http_error entry.
    body = response.json()
    assert body["success"] is False
    error = body["errors"][0]
    assert error["code"] == "http_error"
    assert error["details"]["status_code"] == 404
    assert "no design record named 'no-such-record'" in error["message"]


def test_route_404s_an_invalid_record_id(_record_storage: Path) -> None:
    with TestClient(app) as client:
        response = client.get("/api/projects/.lead/report")
    assert response.status_code == 404


def test_route_404s_an_unreadable_record(_record_storage: Path) -> None:
    # A saved but corrupt record is refused, never served as text.
    project = _record_storage / "projects" / "corrupt"
    project.mkdir(parents=True)
    (project / "record.json").write_text("not json", encoding="utf-8")
    with TestClient(app) as client:
        response = client.get("/api/projects/corrupt/report")
    assert response.status_code == 404
    assert "not valid JSON" in response.json()["errors"][0]["message"]
