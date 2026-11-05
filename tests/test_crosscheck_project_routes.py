"""M21.2: the local project entry points — list, open, save.

The Phase 1 storage layout (``TRINITY_STORAGE_ROOT/projects/<id>/
record.json``) gains its user entry points: ``GET /api/projects`` is
the open menu (a record that cannot be read back is listed with its
load refusal, never hidden), ``GET /api/projects/{record_id}`` opens
the record verbatim as stored, and ``PUT /api/projects/{record_id}``
saves one — validated with the same ``from_dict`` load uses, so a
record this build cannot later open is impossible to save, and the
body id must agree with the path id.
"""

from __future__ import annotations

import dataclasses
import json
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from src.engines.crosscheck import record as record_module
from src.engines.crosscheck.record import RECORD_SCHEMA_VERSION, build_record
from src.main import app


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


def _client() -> TestClient:
    return TestClient(app)


# --- the open menu ---------------------------------------------------------


def test_menu_is_empty_when_nothing_is_saved(_record_storage: Path) -> None:
    with _client() as client:
        response = client.get("/api/projects")
    assert response.status_code == 200
    assert response.json() == {"projects": []}


def test_menu_lists_stored_records_in_order_with_their_stamps(
    _record_storage: Path,
) -> None:
    first = build_record("alpha-design", pcb={"components": []})
    second = build_record("beta-design", firmware={"mcu_pin": {}})
    record_module.save_record(first)  # stamps the object in place
    record_module.save_record(second)
    with _client() as client:
        rows = client.get("/api/projects").json()["projects"]
    assert [row["record_id"] for row in rows] == [
        "alpha-design",
        "beta-design",
    ]
    assert rows[0]["created_at"] == first.created_at
    assert rows[0]["updated_at"] == first.updated_at
    assert rows[0]["app_version"] is not None
    assert rows[0]["error"] is None


def test_menu_names_what_it_cannot_read(_record_storage: Path) -> None:
    """A record this build cannot load is listed with its refusal —
    hiding it would fake an honest menu into an empty one."""
    record_module.save_record(build_record("good-design"))
    corrupt = record_module.records_root() / "corrupt-design"
    corrupt.mkdir(parents=True)
    (corrupt / "record.json").write_text("{not json", encoding="utf-8")
    future = record_module.records_root() / "future-design"
    future.mkdir(parents=True)
    (future / "record.json").write_text(
        json.dumps(
            {
                "record_id": "future-design",
                "record_schema_version": 999,
            }
        ),
        encoding="utf-8",
    )
    with _client() as client:
        rows = client.get("/api/projects").json()["projects"]
    assert [row["record_id"] for row in rows] == [
        "corrupt-design",
        "future-design",
        "good-design",
    ]
    assert "unreadable or not valid JSON" in rows[0]["error"]
    assert rows[0]["created_at"] is None
    assert "record_schema_version 999" in rows[1]["error"]
    assert rows[2]["error"] is None


def test_menu_ignores_a_stale_tmp_file(_record_storage: Path) -> None:
    """A crashed save leaves ``record.json.tmp`` inside a project
    directory only when the directory exists; a bare file in the
    projects root is not a project and not a menu lie either way."""
    stray = record_module.records_root() / "stray.tmp"
    stray.parent.mkdir(parents=True, exist_ok=True)
    stray.write_text("junk", encoding="utf-8")
    with _client() as client:
        rows = client.get("/api/projects").json()["projects"]
    assert rows == []


# --- open ------------------------------------------------------------------


def test_open_returns_the_stored_record_verbatim(
    _record_storage: Path,
) -> None:
    record = build_record(
        "open-design",
        pcb={"components": [{"ref": "R1"}]},
        firmware={"mcu_pin": {}},
    )
    record_module.save_record(record)  # stamps the object in place
    with _client() as client:
        response = client.get("/api/projects/open-design")
    assert response.status_code == 200
    assert response.json() == record.to_dict()


def test_open_404s_unknown_and_invalid_record_ids(
    _record_storage: Path,
) -> None:
    with _client() as client:
        unknown = client.get("/api/projects/nope-design")
        # `.hidden` is a syntactically valid URL segment but an invalid
        # record id (ids may not start with a dot — no hidden paths).
        invalid = client.get("/api/projects/.hidden")
    assert unknown.status_code == 404
    error = unknown.json()["errors"][0]
    assert error["code"] == "http_error"
    assert "no design record named 'nope-design'" in error["message"]
    assert invalid.status_code == 404
    assert "invalid design record id" in invalid.json()["errors"][0][
        "message"
    ]


# --- save ------------------------------------------------------------------


def test_save_creates_a_record_and_round_trips(
    _record_storage: Path,
) -> None:
    body = {
        "record_id": "saved-design",
        "record_schema_version": RECORD_SCHEMA_VERSION,
        "pcb": {"components": [{"ref": "R1", "value": "1k"}]},
    }
    with _client() as client:
        response = client.put("/api/projects/saved-design", json=body)
        opened = client.get("/api/projects/saved-design")
    assert response.status_code == 200
    saved = response.json()
    assert saved["record_id"] == "saved-design"
    # A fresh body is treated as a newly created record: first save
    # sets created_at, and this save is the first.
    assert saved["created_at"] is not None
    assert saved["created_at"] == saved["updated_at"]
    assert saved["app_version"] is not None
    # The record opens back with the sections it carried.
    assert opened.status_code == 200
    assert opened.json()["pcb"] == body["pcb"]


def test_save_replaces_and_respects_a_carried_created_at(
    _record_storage: Path,
) -> None:
    record = build_record("edit-design")
    record_module.save_record(record)  # stamps the object in place
    first_created = record.created_at
    stored = record.to_dict()
    stored["pcb"] = {"components": [{"ref": "R9", "value": "10k"}]}
    with _client() as client:
        response = client.put("/api/projects/edit-design", json=stored)
        reopened = record_module.load_record("edit-design")
    assert response.status_code == 200
    # created_at never moves (the Phase 1 contract); the change is in
    # the record and updated_at moved forward.
    assert reopened.created_at == first_created
    assert reopened.pcb == {"components": [{"ref": "R9", "value": "10k"}]}
    assert reopened.updated_at == stored["updated_at"] or (
        reopened.updated_at >= stored["updated_at"]
    )


def test_save_refuses_a_body_id_that_disagrees_with_the_path(
    _record_storage: Path,
) -> None:
    body = {
        "record_id": "other-id",
        "record_schema_version": RECORD_SCHEMA_VERSION,
    }
    with _client() as client:
        response = client.put("/api/projects/one-id", json=body)
    assert response.status_code == 400
    message = response.json()["errors"][0]["message"]
    assert "does not match the path id 'one-id'" in message
    # Nothing was written under either id.
    assert not record_module.record_path("one-id").exists()
    assert not record_module.record_path("other-id").exists()


def test_save_refuses_what_load_would_refuse(_record_storage: Path) -> None:
    cases = [
        # Unknown keys: the from_dict refusal, verbatim.
        (
            {
                "record_id": "x-design",
                "record_schema_version": RECORD_SCHEMA_VERSION,
                "mystery_key": True,
            },
            "unknown design-record key(s) mystery_key",
        ),
        # A future schema version cannot be saved either: the write
        # would be unreadable by this build.
        (
            {"record_id": "x-design", "record_schema_version": 99},
            "record was written with record_schema_version 99",
        ),
        # A non-string stamp.
        (
            {
                "record_id": "x-design",
                "record_schema_version": RECORD_SCHEMA_VERSION,
                "created_at": 17,
            },
            "created_at must be a string or absent",
        ),
        # A body id no load would accept.
        (
            {"record_id": "", "record_schema_version": RECORD_SCHEMA_VERSION},
            "invalid design record id",
        ),
    ]
    with _client() as client:
        for body, fragment in cases:
            response = client.put("/api/projects/x-design", json=body)
            assert response.status_code == 400
            assert fragment in response.json()["errors"][0]["message"]
    assert not record_module.record_path("x-design").exists()


def test_save_refuses_a_nonobject_body(_record_storage: Path) -> None:
    with _client() as client:
        response = client.put("/api/projects/x-design", json=[1, 2])
    assert response.status_code == 422
    assert (
        response.json()["errors"][0]["code"] == "request_validation_error"
    )
    assert not record_module.record_path("x-design").exists()
