"""Connection logic to Pinecone/Chroma, with SQLite lineage fallback.

Trinity V1 stores artifact metadata in SQLite (src/db/database.py).
Use this class as the seam for vector backends without touching engines.
"""
from __future__ import annotations

from pathlib import Path
from typing import Any


class VectorStore:
    def __init__(self, persist_dir: str | Path | None = None):
        from src.core.config import settings

        self.persist_dir = Path(persist_dir) if persist_dir else settings.embeddings_dir
        self.persist_dir.mkdir(parents=True, exist_ok=True)

    def add(self, doc_id: str, embedding: list[float], metadata: dict[str, Any] | None = None) -> None:
        raise NotImplementedError("Plug in chroma/pinecone here; SQLite lineage remains source of truth.")

    def query(self, embedding: list[float], k: int = 5) -> list[dict[str, Any]]:
        raise NotImplementedError
