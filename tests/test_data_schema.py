"""M17: versioned data-schema marker and explicit migration seam.

The storage root is user data. These tests pin the three honest
behaviours: legacy storage is adopted without loss, a re-run is a
no-op, and anything this build does not understand refuses *before*
the database is touched.
"""

from __future__ import annotations

import json
import sqlite3
from pathlib import Path

import pytest

from src.core import data_schema
from src.core.data_schema import (
    MIGRATIONS,
    SCHEMA_VERSION,
    DataSchemaError,
    apply_data_schema,
    marker_path,
    read_marker,
)
from src.db.database import init_db


def _query(db_path: Path, sql: str, params: tuple = ()) -> list[sqlite3.Row]:
    conn = sqlite3.connect(db_path)
    try:
        return conn.execute(sql, params).fetchall()
    finally:
        conn.close()


def _tables(db_path: Path) -> set[str]:
    return {row[0] for row in _query(db_path, "SELECT name FROM sqlite_master WHERE type='table'")}


def test_fresh_root_gets_marker_and_schema(tmp_path):
    root = tmp_path / "storage"
    db = root / "trinity.db"

    assert apply_data_schema(root, db) == SCHEMA_VERSION

    marker = json.loads(marker_path(root).read_text(encoding="utf-8"))
    assert marker["schema_version"] == SCHEMA_VERSION
    assert marker["app_version"]
    assert marker["updated_at"]
    assert {"jobs", "artifacts", "validations", "cache_entries", "projects"} <= _tables(db)
    # Write-then-replace leaves no temp file behind.
    assert not marker_path(root).with_suffix(".json.tmp").exists()


def test_marker_is_idempotent(tmp_path):
    root = tmp_path / "storage"
    db = root / "trinity.db"

    apply_data_schema(root, db)
    first = marker_path(root).read_bytes()

    assert apply_data_schema(root, db) == SCHEMA_VERSION
    assert marker_path(root).read_bytes() == first


def test_legacy_storage_is_adopted_without_data_loss(tmp_path):
    root = tmp_path / "storage"
    root.mkdir()
    db = root / "trinity.db"
    init_db(db)
    conn = sqlite3.connect(db)
    try:
        conn.execute(
            "INSERT INTO projects (project_id, name, created_at) VALUES (?, ?, ?)",
            ("p1", "kept", "2026-01-01T00:00:00+00:00"),
        )
        conn.commit()
    finally:
        conn.close()

    # Pre-M17 storage: real data, no marker.
    assert read_marker(root) is None

    apply_data_schema(root, db)

    assert read_marker(root)["schema_version"] == SCHEMA_VERSION
    rows = _query(db, "SELECT name FROM projects WHERE project_id = 'p1'")
    assert [row[0] for row in rows] == ["kept"]


def test_newer_schema_refuses_before_touching_db(tmp_path):
    root = tmp_path / "storage"
    root.mkdir()
    db = root / "trinity.db"
    future = SCHEMA_VERSION + 1
    marker_path(root).write_text(
        json.dumps({"schema_version": future}), encoding="utf-8"
    )

    with pytest.raises(DataSchemaError, match="newer Trinity"):
        apply_data_schema(root, db)

    assert not db.exists()
    assert json.loads(marker_path(root).read_text(encoding="utf-8"))["schema_version"] == future


@pytest.mark.parametrize(
    "payload",
    [
        "{not json",
        json.dumps(["schema_version", 1]),
        json.dumps({"schema_version": "1"}),
        json.dumps({"schema_version": True}),
        json.dumps({"schema_version": None}),
    ],
)
def test_corrupt_marker_refuses(tmp_path, payload):
    root = tmp_path / "storage"
    root.mkdir()
    marker_path(root).write_text(payload, encoding="utf-8")

    with pytest.raises(DataSchemaError, match="refusing to guess"):
        apply_data_schema(root, root / "trinity.db")

    assert not (root / "trinity.db").exists()


def test_negative_version_refuses(tmp_path):
    root = tmp_path / "storage"
    root.mkdir()
    marker_path(root).write_text(
        json.dumps({"schema_version": -1}), encoding="utf-8"
    )

    with pytest.raises(DataSchemaError, match="negative schema_version"):
        apply_data_schema(root, root / "trinity.db")


def test_migration_chain_is_explicit_and_complete():
    assert set(MIGRATIONS) == set(range(1, SCHEMA_VERSION + 1))


def test_missing_migration_refuses_and_writes_nothing(tmp_path, monkeypatch):
    root = tmp_path / "storage"
    monkeypatch.setattr(data_schema, "MIGRATIONS", {})

    with pytest.raises(DataSchemaError, match="no migration registered"):
        apply_data_schema(root, root / "trinity.db")

    assert not marker_path(root).exists()
