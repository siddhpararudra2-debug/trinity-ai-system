"""Small SQLite cache for explicitly deterministic engine operations."""
from __future__ import annotations

import hashlib
import json
from datetime import datetime, timezone
from typing import Any

from app.db.database import get_connection


def cache_key(engine: str, operation: str, parameters: dict[str, Any]) -> str:
    canonical = json.dumps(parameters, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return hashlib.sha256(f"{engine}:{operation}:{canonical}".encode()).hexdigest()


def get(key: str) -> dict[str, Any] | None:
    with get_connection() as conn:
        row = conn.execute(
            "SELECT response, expires_at FROM cache_entries WHERE cache_key = ?", (key,)
        ).fetchone()
    if not row or (row["expires_at"] and row["expires_at"] <= datetime.now(timezone.utc).isoformat()):
        return None
    return json.loads(row["response"])


def put(key: str, engine: str, operation: str, response: dict[str, Any]) -> None:
    with get_connection() as conn:
        conn.execute(
            "INSERT OR REPLACE INTO cache_entries (cache_key, engine, operation, response, created_at, expires_at) VALUES (?, ?, ?, ?, ?, NULL)",
            (key, engine, operation, json.dumps(response), datetime.now(timezone.utc).isoformat()),
        )
        conn.commit()
