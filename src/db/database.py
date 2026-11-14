"""
SQLite persistence layer.

PRD section 8 is explicit: no ORM in V1, and never store large CAD/3D
files in the database — only their metadata. This module owns the
schema and gives every other module a plain sqlite3 connection.
"""

from __future__ import annotations

import sqlite3
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path

from src.core.config import settings

SCHEMA = """
CREATE TABLE IF NOT EXISTS jobs (
    job_id      TEXT PRIMARY KEY,
    engine      TEXT NOT NULL,
    operation   TEXT NOT NULL,
    status      TEXT NOT NULL,
    progress    REAL NOT NULL DEFAULT 0.0,
    request     TEXT NOT NULL,
    result      TEXT,
    error       TEXT,
    created_at  TEXT NOT NULL,
    updated_at  TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS artifacts (
    artifact_id TEXT PRIMARY KEY,
    job_id      TEXT REFERENCES jobs(job_id),
    type        TEXT NOT NULL,
    path        TEXT NOT NULL,
    size_bytes  INTEGER NOT NULL,
    checksum    TEXT NOT NULL,
    created_at  TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS validations (
    validation_id TEXT PRIMARY KEY,
    job_id        TEXT REFERENCES jobs(job_id),
    engine        TEXT NOT NULL,
    status        TEXT NOT NULL,   -- GENERATED | VALIDATED | VERIFIED | FAILED
    checks        TEXT NOT NULL,   -- JSON blob of individual check results
    created_at    TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS cache_entries (
    cache_key TEXT PRIMARY KEY,
    engine TEXT NOT NULL,
    operation TEXT NOT NULL,
    response TEXT NOT NULL,
    created_at TEXT NOT NULL,
    expires_at TEXT
);

CREATE TABLE IF NOT EXISTS projects (
    project_id TEXT PRIMARY KEY,
    name       TEXT NOT NULL,
    created_at TEXT NOT NULL
);

-- M30 accounts: users and the single-use token registry. Passwords are
-- scrypt hashes; auth_tokens rows carry the jti of every verify/reset/
-- refresh JWT so replays are detectable server-side.
CREATE TABLE IF NOT EXISTS users (
    user_id         TEXT PRIMARY KEY,
    email           TEXT NOT NULL UNIQUE,
    password_hash   TEXT NOT NULL,
    verified_at     TEXT,
    failed_attempts INTEGER NOT NULL DEFAULT 0,
    locked_until    REAL,
    created_at      TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS auth_tokens (
    jti        TEXT PRIMARY KEY,
    user_id    TEXT NOT NULL REFERENCES users(user_id) ON DELETE CASCADE,
    purpose    TEXT NOT NULL,   -- refresh | verify | reset
    expires_at REAL NOT NULL,
    used_at    REAL,
    created_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_artifacts_job_id ON artifacts(job_id);
CREATE INDEX IF NOT EXISTS idx_validations_job_id ON validations(job_id);
CREATE INDEX IF NOT EXISTS idx_auth_tokens_user_id ON auth_tokens(user_id);
"""


def _connect(db_path: Path) -> sqlite3.Connection:
    conn = sqlite3.connect(db_path, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON;")
    conn.execute("PRAGMA journal_mode = WAL;")
    return conn


def init_db(db_path: Path | None = None) -> None:
    path = db_path or settings.db_path
    path.parent.mkdir(parents=True, exist_ok=True)
    conn = _connect(path)
    try:
        conn.executescript(SCHEMA)
        conn.commit()
    finally:
        conn.close()


@contextmanager
def get_connection(db_path: Path | None = None) -> Iterator[sqlite3.Connection]:
    path = db_path or settings.db_path
    conn = _connect(path)
    try:
        yield conn
    finally:
        conn.close()
