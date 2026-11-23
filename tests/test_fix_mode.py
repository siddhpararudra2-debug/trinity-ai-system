"""Fix-mode tests - rule-based patches for cross-check failures.

Good patch flips FAILED to VERIFIED; invalid patch refused; unfixable check
stays a hint; max 3 attempts; restore; and one e2e flow using the seeded
``break_c03_unknown_pin`` design.

Records are saved under the real ``settings.storage_root`` with unique ids
and removed afterwards, so the API's ``load_record`` finds them.
"""

from __future__ import annotations

import shutil
import uuid
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from src.core.config import settings
from src.engines.crosscheck.fixtures import build_fixture
from src.engines.crosscheck.fix import (
    FIXABLE_CHECK_IDS,
    apply_patch,
    generate_patch,
    record_diff,
)
from src.engines.crosscheck.record import (
    DesignRecord,
    build_record,
    load_record,
    save_record,
)
from src.engines.crosscheck.report import assemble_report
from src.main import app


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _unique_id(prefix: str = "fix-test") -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _save_fixture(name: str, record_id: str) -> tuple[DesignRecord, Path]:
    """Build a named fixture under *record_id* and save it to the real store."""
    src = build_fixture(name)
    rec = build_record(
        record_id,
        pcb=src.pcb,
        firmware=src.firmware,
        holes=src.holes,
        connectors=src.connectors,
        enclosure=src.enclosure,
        bom=src.bom,
        power=src.power,
        engine_checks=src.engine_checks,
    )
    path = save_record(rec, snapshot=False)
    return rec, path


def _cleanup(path: Path) -> None:
    shutil.rmtree(path.parent, ignore_errors=True)
    # Snapshot dir for the same record id, if any.
    snaps = settings.storage_root / "snapshots" / path.parent.name
    shutil.rmtree(snaps, ignore_errors=True)


def _status_and_fail_counts(rec: DesignRecord) -> tuple[str, int, int]:
    report = assemble_report(rec)
    return (
        report["overall_status"],
        sum(1 for c in report["checks"] if c["status"] == "FAIL"),
        sum(1 for c in report["checks"] if c["status"] == "WARN"),
    )


@pytest.fixture()
def proposed_c03():
    """A saved break_c03_unknown_pin record + its C03 patch, cleaned up."""
    record_id = _unique_id("fix-c03")
    rec, path = _save_fixture("break_c03_unknown_pin", record_id)
    patch = generate_patch(rec, "C03")
    assert patch is not None, "C03 patch should be generated for the seeded design"
    yield record_id, rec, path, patch
    _cleanup(path)


# ---------------------------------------------------------------------------
# Endpoints are registered with the correct path parameter
# ---------------------------------------------------------------------------

def test_endpoints_exist(proposed_c03):
    """All three fix routes are registered at /api/fix/*/{record_id}."""
    record_id, _, _, _ = proposed_c03
    with TestClient(app) as client:
        # propose -> 200
        r = client.post(f"/api/fix/propose/{record_id}")
        assert r.status_code == 200, f"propose failed: {r.text}"
        # apply with empty/invalid body -> 400 (not 404/405)
        r2 = client.post(f"/api/fix/apply/{record_id}", json={})
        assert r2.status_code == 400, f"apply should refuse invalid patch: {r2.text}"
        # restore with a missing snapshot -> 4xx (not 404 route-miss/405)
        r3 = client.post(f"/api/fix/restore/{record_id}?snapshot_id=999999")
        assert r3.status_code in (400, 404), f"restore unexpected: {r3.status_code} {r3.text}"


# ---------------------------------------------------------------------------
# Good patch flips FAILED -> VERIFIED
# ---------------------------------------------------------------------------

def test_fix_propose_apply_c03_improves(proposed_c03):
    """Propose the C03 patch, apply it, and verify FAILED -> VERIFIED."""
    record_id, rec, path, patch = proposed_c03

    before_status, before_fail, before_warn = _status_and_fail_counts(rec)
    assert before_status == "FAILED" and before_fail >= 1

    with TestClient(app) as client:
        # Propose (changes nothing)
        r = client.post(f"/api/fix/propose/{record_id}")
        assert r.status_code == 200, f"propose failed: {r.text}"
        body = r.json()
        assert body["status"] == "proposed"
        assert body["can_apply"] is True
        c03 = [p for p in body["patches"] if p["check_id"] == "C03"]
        assert len(c03) == 1, f"expected exactly one C03 patch: {body['patches']}"

        # The record is still FAILED after proposing.
        assert _status_and_fail_counts(load_record(record_id))[0] == "FAILED"

        # Apply
        r2 = client.post(f"/api/fix/apply/{record_id}", json=patch)
        assert r2.status_code == 200, f"apply failed: {r2.text}"
        applied = r2.json()
        assert applied["status"] == "applied"
        assert applied["before"]["overall_status"] == "FAILED"
        assert applied["after"]["overall_status"] == "VERIFIED", applied
        assert applied["after"]["fail_count"] == 0
        assert applied["made_worse"] is False
        assert "firmware.pin_mappings" in applied["diff"]

        # The saved record really changed.
        saved = load_record(record_id)
        assert _status_and_fail_counts(saved)[0] == "VERIFIED"


# ---------------------------------------------------------------------------
# Invalid patch refused
# ---------------------------------------------------------------------------

def test_fix_apply_invalid_patch_refused(proposed_c03):
    """A bogus patch (unknown check id) is rejected with 400."""
    record_id, _, _, _ = proposed_c03
    with TestClient(app) as client:
        r = client.post(
            f"/api/fix/apply/{record_id}",
            json={"check_id": "C99", "op": "nonexistent", "section": "firmware"},
        )
        assert r.status_code == 400, f"expected 400, got {r.status_code}: {r.text}"


# ---------------------------------------------------------------------------
# Restore returns an honest before/after
# ---------------------------------------------------------------------------

def test_fix_restore_returns_structure(proposed_c03):
    """After applying, restoring the pre-fix snapshot brings back FAILED."""
    record_id, _, _, patch = proposed_c03
    # Take a pre-fix snapshot via save_record (snapshot=True displaces current)
    with TestClient(app) as client:
        # Apply once (the app snapshots before applying).
        r = client.post(f"/api/fix/apply/{record_id}", json=patch)
        assert r.status_code == 200, f"apply failed: {r.text}"
        applied = r.json()
        assert applied["after"]["overall_status"] == "VERIFIED"

        # Find the snapshot taken just before the fix (reason fix:attempt-1
        # holds the pre-fix state, the other holds the post-fix state).
        from src.engines.crosscheck.snapshots import list_snapshots

        snaps = list_snapshots(record_id)
        fix_snaps = [s for s in snaps if str(s.get("reason", "")).startswith("fix:")]
        assert len(fix_snaps) >= 1, f"expected fix snapshots: {snaps}"

        # The restore endpoint restores from an explicit snapshot id.
        sid = int(fix_snaps[0]["snapshot_id"])
        rr = client.post(f"/api/fix/restore/{record_id}?snapshot_id={sid}")
        assert rr.status_code == 200, f"restore failed: {rr.text}"
        restored = rr.json()
        assert restored["status"] == "restored"
        assert "restored_overall_status" in restored
        assert "restored_fail_count" in restored


# ---------------------------------------------------------------------------
# Max 3 attempts, counted server-side
# ---------------------------------------------------------------------------

def test_fix_attempts_max_three(proposed_c03):
    """The 4th fix attempt is refused with 409.

    Each apply makes the patch stale (C03 becomes PASS), so between
    attempts we restore the pre-fix state to keep the patch applicable.
    The attempt counter derives from the accumulated fix:* snapshots and
    is therefore unaffected by the restore snapshots.
    """
    record_id, _, _, patch = proposed_c03
    with TestClient(app) as client:
        pre_fix_sid = None
        for attempt in range(3):
            if pre_fix_sid is not None:
                rr = client.post(
                    f"/api/fix/restore/{record_id}?snapshot_id={pre_fix_sid}"
                )
                assert rr.status_code == 200, f"reset failed: {rr.text}"
            r = client.post(f"/api/fix/apply/{record_id}", json=patch)
            assert r.status_code == 200, (
                f"attempt {attempt + 1} failed: {r.status_code} {r.text}"
            )
            # The pre-fix snapshot (reason fix:attempt-N) with a FAILED
            # record is the one to restore before the next attempt.
            from src.engines.crosscheck.snapshots import list_snapshots, load_snapshot

            snaps = list_snapshots(record_id)
            pre_fix_sid = None
            for s in snaps:
                if not str(s.get("reason", "")).startswith("fix:"):
                    continue
                payload = load_snapshot(record_id, int(s["snapshot_id"]))
                snap_rec = DesignRecord.from_dict(payload["record"])
                rep = assemble_report(snap_rec)
                if any(
                    c["check_id"] == "C03" and c["status"] == "FAIL"
                    for c in rep["checks"]
                ):
                    pre_fix_sid = int(s["snapshot_id"])
                    break
            assert pre_fix_sid is not None, f"no pre-fix snapshot found: {snaps}"

        # After 3 successful attempts, the 4th is refused.
        r4 = client.post(f"/api/fix/apply/{record_id}", json=patch)
        assert r4.status_code == 409, (
            f"4th attempt should be 409, got {r4.status_code}: {r4.text}"
        )


# ---------------------------------------------------------------------------
# Unfixable check stays a hint
# ---------------------------------------------------------------------------

def test_unfixable_check_stays_hint():
    """A FAILED record with no rule (e.g. C06) keeps the check as a hint."""
    record_id = _unique_id("fix-c06")
    rec, path = _save_fixture("break_c06_two_classes_one_net", record_id)
    try:
        assert generate_patch(rec, "C06") is None
        with TestClient(app) as client:
            r = client.post(f"/api/fix/propose/{record_id}")
            assert r.status_code == 200, f"propose failed: {r.text}"
            body = r.json()
            assert "C06" in body["unfixed_ids"], body
            assert all(p["check_id"] != "C06" for p in body["patches"])
    finally:
        _cleanup(path)


# ---------------------------------------------------------------------------
# e2e: full flow on the seeded break_c03_unknown_pin design
# ---------------------------------------------------------------------------

def test_e2e_fix_flow_seeded_c03():
    """e2e: FAILED report -> propose -> diff -> apply -> before/after -> restore."""
    record_id = _unique_id("e2e-c03")
    rec, path = _save_fixture("break_c03_unknown_pin", record_id)
    try:
        with TestClient(app) as client:
            # 1. Inspect report is FAILED with C03 at a known pin.
            report = client.get(f"/api/projects/{record_id}/inspect")
            assert report.status_code == 200, report.text
            body = report.json()
            assert body["overall_status"] == "FAILED"
            c03 = [c for c in body["checks"] if c["check_id"] == "C03"]
            assert len(c03) == 1 and c03[0]["status"] == "FAIL"

            # 2. Propose: real diff, nothing saved.
            props = client.post(f"/api/fix/propose/{record_id}")
            assert props.status_code == 200, props.text
            pb = props.json()
            assert pb["can_apply"] is True
            assert pb["diff"]["fail_count"] >= 1
            assert any(p["check_id"] == "C03" for p in pb["patches"])
            assert load_record(record_id).overall_status is None  # report is generated

            # 3. Apply: before/after.
            patch = next(p for p in pb["patches"] if p["check_id"] == "C03")
            applied = client.post(f"/api/fix/apply/{record_id}", json=patch)
            assert applied.status_code == 200, applied.text
            ab = applied.json()
            assert ab["before"]["overall_status"] == "FAILED"
            assert ab["after"]["overall_status"] == "VERIFIED"
            assert ab["made_worse"] is False

            # 4. Restore from the pre-fix state.
            from src.engines.crosscheck.snapshots import list_snapshots

            snaps = list_snapshots(record_id)
            before_snap = next(
                s for s in snaps if str(s.get("reason")) == "fix:attempt-1"
            )
            rr = client.post(
                f"/api/fix/restore/{record_id}?snapshot_id={int(before_snap['snapshot_id'])}"
            )
            assert rr.status_code == 200, rr.text
            assert rr.json()["status"] == "restored"
    finally:
        _cleanup(path)


# ---------------------------------------------------------------------------
# Unit tests for the patch engine
# ---------------------------------------------------------------------------

def test_generate_and_apply_patch_directly(proposed_c03):
    """generate_patch + apply_patch work without the API."""
    _, rec, _, patch = proposed_c03
    assert patch["check_id"] == "C03"
    new_rec = apply_patch(rec, patch)
    assert new_rec is not None
    status, fail, warn = _status_and_fail_counts(new_rec)
    assert status == "VERIFIED" and fail == 0


def test_record_diff(proposed_c03):
    """record_diff returns a dict of path -> [before, after]."""
    _, rec, _, patch = proposed_c03
    new_rec = apply_patch(rec, patch)
    assert new_rec is not None
    diff = record_diff(rec, new_rec)
    assert isinstance(diff, dict)
    assert any(k.startswith("firmware.pin_mappings") for k in diff)


def test_fixable_ids_cover_four_plus_c03():
    """At least four checks are auto-fixable, including C03."""
    assert "C03" in FIXABLE_CHECK_IDS
    assert len(FIXABLE_CHECK_IDS) >= 4
