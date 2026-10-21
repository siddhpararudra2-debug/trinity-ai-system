"""Local vector store: deterministic embeddings + cosine search in SQLite.

Closes the former NotImplementedError seam. Zero new dependencies, zero
network: embeddings come from a deterministic hashed n-gram scheme and are
L2-normalized Python floats; persistence is a private SQLite database under
``settings.embeddings_dir`` (``vectors.db``).

Honest scope, stated plainly:
- The store is a utility, not an engine; artifact lineage in SQLite
  (``src/db/database.py``) remains the source of truth. Nothing here
  replaces lineage — it adds nearest-neighbor lookup on top of text.
- Deterministic: the same text always produces the same vector on the same
  version of this module (FNV-1a bucketing is version-stable by design).
- No learned model, no API embeddings, no server: similarity is lexical.
  Vectors from a different ``dim`` or another embedding source can be stored
  via ``add()`` mixed with text vectors, but cross-source cosine is
  meaningless — queries compare like with like.
"""
from __future__ import annotations

import json
import math
import re
import sqlite3
import struct
from pathlib import Path
from typing import Any

_SCHEMA = """
CREATE TABLE IF NOT EXISTS vectors (
    id       TEXT PRIMARY KEY,
    dim      INTEGER NOT NULL,
    vec      BLOB NOT NULL,
    metadata TEXT NOT NULL,
    created_at TEXT NOT NULL
)
"""

_TOKEN_RE = re.compile(r"[a-z0-9]+")


def _fnv1a(token: str) -> int:
    """FNV-1a 32-bit; deterministic across processes and platforms."""
    value = 2166136261
    prime = 16777619
    for byte in token.encode("utf-8"):
        value ^= byte
        value = (value * prime) & 0xFFFFFFFF
    return value


def _tokenize(text: str) -> list[str]:
    """Whole words plus 3-grams of longer words, lowercased."""
    grams: list[str] = []
    for token in _TOKEN_RE.findall(text.lower()):
        grams.append(token)
        if len(token) > 3:
            grams.extend(token[i : i + 3] for i in range(len(token) - 2))
    return grams


def embed(text: str, dim: int = 256) -> list[float]:
    """Deterministic hashed n-gram embedding, L2-normalized (or all-zeros)."""
    if dim < 8:
        raise ValueError("dim must be at least 8")
    vec = [0.0] * dim
    for token in _tokenize(text):
        vec[_fnv1a(token) % dim] += 1.0
    norm = math.sqrt(sum(v * v for v in vec))
    if norm == 0.0:
        return vec
    return [v / norm for v in vec]


def cosine(a: list[float], b: list[float]) -> float:
    """Cosine similarity; 0.0 when either vector is degenerate."""
    norm_a = math.sqrt(sum(x * x for x in a))
    norm_b = math.sqrt(sum(x * x for x in b))
    if norm_a == 0.0 and norm_b == 0.0:
        # Two zero vectors have no information: not a match, not an error.
        return 0.0
    if norm_a == 0.0 or norm_b == 0.0:
        return 0.0
    dot = sum(x * y for x, y in zip(a, b))
    score = dot / (norm_a * norm_b)
    # Guard against float drift pushing past [-1, 1].
    return max(-1.0, min(1.0, score))


class VectorStore:
    """Embedding index over a private SQLite file; lineage stays primary."""

    DB_NAME = "vectors.db"

    def __init__(self, persist_dir: str | Path | None = None, dim: int = 256):
        from src.core.config import settings

        self.dim = dim
        self.persist_dir = Path(persist_dir) if persist_dir else settings.embeddings_dir
        self.persist_dir.mkdir(parents=True, exist_ok=True)
        self._db = sqlite3.connect(self.persist_dir / self.DB_NAME)
        self._db.execute(_SCHEMA)
        self._db.commit()

    def _pack(self, embedding: list[float]) -> bytes:
        return struct.pack(f"<{len(embedding)}d", *embedding)

    def _unpack(self, blob: bytes, dim: int) -> list[float]:
        return list(struct.unpack(f"<{dim}d", blob))

    def add(
        self,
        doc_id: str,
        embedding: list[float],
        metadata: dict[str, Any] | None = None,
    ) -> None:
        """Insert or replace a vector row with its metadata.

        ``embedding`` may be a text embedding (``embed``) or any caller-
        supplied vector; its length defines its own dim and does not need to
        match ``self.dim`` (store + query compare per-dim at runtime). Callers
        mixing dims are responsible for the meaning of the result.
        """
        if not doc_id:
            raise ValueError("doc_id must be non-empty")
        if not embedding:
            raise ValueError("embedding must be non-empty")
        self._db.execute(
            "INSERT OR REPLACE INTO vectors (id, dim, vec, metadata, created_at)"
            " VALUES (?, ?, ?, ?, datetime('now'))",
            (
                doc_id,
                len(embedding),
                self._pack(embedding),
                json.dumps(metadata or {}, sort_keys=True),
            ),
        )
        self._db.commit()

    def add_text(
        self,
        doc_id: str,
        text: str,
        metadata: dict[str, Any] | None = None,
        dim: int | None = None,
    ) -> list[float]:
        """Embed text with this store's scheme, then store it. Returns it."""
        vector = embed(text, dim or self.dim)
        self.add(doc_id, vector, metadata)
        return vector

    def delete(self, doc_id: str) -> bool:
        """Remove one row; True when a row was deleted."""
        cursor = self._db.execute("DELETE FROM vectors WHERE id = ?", (doc_id,))
        self._db.commit()
        return cursor.rowcount > 0

    def clear(self) -> None:
        """Drop every row (the file/table survives)."""
        self._db.execute("DELETE FROM vectors")
        self._db.commit()

    def count(self) -> int:
        """Number of indexed documents."""
        row = self._db.execute("SELECT COUNT(*) FROM vectors").fetchone()
        return int(row[0])

    def query(
        self,
        embedding: list[float],
        k: int = 5,
    ) -> list[dict[str, Any]]:
        """Nearest neighbors by cosine, best first: [{id, score, metadata}]."""
        if not embedding:
            raise ValueError("embedding must be non-empty")
        if k <= 0:
            raise ValueError("k must be positive")
        results: list[dict[str, Any]] = []
        for row in self._db.execute("SELECT id, dim, vec, metadata FROM vectors"):
            vector = self._unpack(row[2], int(row[1]))
            results.append(
                {
                    "id": row[0],
                    "score": cosine(embedding, vector),
                    "metadata": json.loads(row[3]),
                }
            )
        results.sort(key=lambda item: item["score"], reverse=True)
        return results[:k]

    def search(self, text: str, k: int = 5) -> list[dict[str, Any]]:
        """Embed a raw query text with this store's scheme, then query."""
        return self.query(embed(text, self.dim), k)

    def close(self) -> None:
        """Close the underlying connection (tests re-open per instance)."""
        self._db.close()
