"""Versioned data-schema marker for the storage root (M17).

TRINITY_STORAGE_ROOT holds everything Trinity writes: the SQLite
database, jobs, artifacts, caches. Until M17 that directory had no
record of which schema it was built with — an upgrade could only guess.
This module makes the version explicit and the migration path numbered:

* ``DATA_SCHEMA.json`` in the storage root records ``schema_version``.
* A missing marker means legacy storage, defined as version 0; every
  numbered migration runs in order (migration 1 is the idempotent
  schema init that pre-M17 installs already match).
* A marker from a *newer* Trinity refuses to start with a clear error.
  Guessing or silently downgrading user data is not an option.

Startup order in ``src/main.py`` is ``ensure_storage_layout()`` then
``apply_data_schema()``, so the future-version refusal happens before
the database is touched.
"""

from __future__ import annotations

import json
import os
from collections.abc import Callable
from datetime import UTC, datetime
from pathlib import Path

from src.core.config import settings
from src.db.database import init_db

SCHEMA_VERSION = 1
MARKER_NAME = "DATA_SCHEMA.json"


class DataSchemaError(RuntimeError):
    """The storage root's schema cannot be used by this build."""


def marker_path(root: Path) -> Path:
    return root / MARKER_NAME


def read_marker(root: Path) -> dict | None:
    """Return the parsed marker, or None when this is legacy storage.

    Anything present but unreadable is refused, not guessed: a corrupt
    marker is exactly the situation where silently assuming "probably
    version 1" would corrupt user data.
    """
    path = marker_path(root)
    if not path.exists():
        return None
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise DataSchemaError(
            f"{path} is unreadable or not valid JSON ({exc}); refusing to "
            "guess which schema this storage root uses"
        ) from exc
    version = raw.get("schema_version") if isinstance(raw, dict) else None
    if not isinstance(version, int) or isinstance(version, bool):
        raise DataSchemaError(
            f"{path} has no integer schema_version; refusing to guess which "
            "schema this storage root uses"
        )
    return raw


def _write_marker(root: Path, version: int) -> None:
    path = marker_path(root)
    payload = {
        "schema_version": version,
        "app_version": settings.api_version,
        "updated_at": datetime.now(UTC).isoformat(),
    }
    # Write-then-replace so a crash never leaves a half-written marker
    # that the next startup would have to guess about.
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, path)


def _migrate_0_to_1(root: Path, db_path: Path) -> None:
    """Pre-M17 storage: the M-schema tables are the v1 baseline."""
    del root  # reserved for file migrations; v1 only needs the database
    init_db(db_path)


# Explicit and ordered: a gap in this mapping is a refusal, never a
# silent skip. Future versions append here; existing entries never move.
MIGRATIONS: dict[int, Callable[[Path, Path], None]] = {
    1: _migrate_0_to_1,
}


def apply_data_schema(root: Path | None = None, db_path: Path | None = None) -> int:
    """Bring the storage root to ``SCHEMA_VERSION`` and return that version.

    Raises ``DataSchemaError`` for a marker newer than this build, a
    corrupt marker, or a missing migration entry — all before writing
    anything to the storage root.
    """
    root = root or settings.storage_root
    db_path = db_path or settings.db_path

    marker = read_marker(root)
    current = marker["schema_version"] if marker else 0
    if current < 0:
        raise DataSchemaError(
            f"{marker_path(root)} records negative schema_version {current}; "
            "refusing to guess what that storage root contains"
        )
    if current > SCHEMA_VERSION:
        raise DataSchemaError(
            f"storage root {root} was written by a newer Trinity "
            f"(data schema {current}); this build understands schema "
            f"{SCHEMA_VERSION}. Refusing to guess or downgrade — use a newer "
            "build or a different TRINITY_STORAGE_ROOT."
        )
    if current == SCHEMA_VERSION:
        return current

    root.mkdir(parents=True, exist_ok=True)
    for version in range(current + 1, SCHEMA_VERSION + 1):
        migration = MIGRATIONS.get(version)
        if migration is None:
            raise DataSchemaError(
                f"no migration registered for data schema {version} "
                f"(storage root {root} is at {current}, this build needs "
                f"{SCHEMA_VERSION})"
            )
        migration(root, db_path)
    _write_marker(root, SCHEMA_VERSION)
    return SCHEMA_VERSION
