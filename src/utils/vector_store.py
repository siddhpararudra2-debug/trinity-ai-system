"""Seam for future vector backends (Pinecone/Chroma).

Trinity V1 ships no embedding search: ``add`` and ``query`` intentionally
raise ``NotImplementedError`` so no caller can mistake this seam for a
working store. Artifact lineage lives in SQLite (``src/db/database.py``),
which is the source of truth in V1.
"""
from __future__ import annotations

from pathlib import Path
from typing import Any


class VectorStore:
    """Placeholder seam; V1 keeps all lineage in SQLite, not vectors."""

    def __init__(self, persist_dir: str | Path | None = None):
        from src.core.config import settings

        self.persist_dir = Path(persist_dir) if persist_dir else settings.embeddings_dir
        self.persist_dir.mkdir(parents=True, exist_ok=True)

    def add(self, doc_id: str, embedding: list[float], metadata: dict[str, Any] | None = None) -> None:
        """Refuse: V1 has no vector backend; SQLite lineage remains source of truth."""
        raise NotImplementedError(
            "Vector search is not implemented in V1; SQLite lineage remains source of truth."
        )

    def query(self, embedding: list[float], k: int = 5) -> list[dict[str, Any]]:
        """Refuse: V1 has no vector backend."""
        raise NotImplementedError(
            "Vector search is not implemented in V1; SQLite lineage remains source of truth."
        )
