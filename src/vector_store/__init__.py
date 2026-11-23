"""Vector store interface — embedding-backed retrieval with model identity.

An index records the embedding model that produced it.  Reusing an index
built with a different model is refused with a clear message rather than
silently mixing vector spaces; ``rebuild()`` starts a fresh index.  Every
search result carries a ``mode`` label (``"embedding"``, ``"hash"`` or
``"keyword"``) so a fallback is never presented as the real model.

``LLM-LINK:`` a future concrete store (SQLite FTS, Qdrant, ...) can replace
``InMemoryStore`` without changing the pipeline contract.
"""

from __future__ import annotations

from typing import Any

from src.embeddings import SCORE_THRESHOLD, get_mode, keyword_search


class ModelMismatch(RuntimeError):
    """Raised when chunks from a different embedding model are added."""


class VectorStore:
    """Abstract base for embedding-backed retrieval."""

    model_name: str | None = None

    def add_chunks(self, chunks: list[dict[str, Any]]) -> None:
        """Add chunks carrying a ``hash`` from ``src.embeddings.chunk_hash``."""
        raise NotImplementedError

    def search(self, query: str, *, top_k: int = 5) -> dict[str, Any]:
        """Return ``{results, fallback, mode, not_found}``."""
        raise NotImplementedError

    def rebuild(self) -> None:
        """Discard all chunks and the recorded model name."""
        raise NotImplementedError


class InMemoryStore(VectorStore):
    """In-memory store: keyword search over hashed chunks, labelled honestly."""

    def __init__(self) -> None:
        self._index: dict[str, dict[str, Any]] = {}
        self.model_name: str | None = None

    def rebuild(self) -> None:
        self._index.clear()
        self.model_name = None

    def add_chunks(
        self, chunks: list[dict[str, Any]], *, model_name: str | None = None
    ) -> None:
        name = model_name or get_mode()
        if self.model_name is not None and self.model_name != name:
            raise ModelMismatch(
                f"index was built with embedding mode '{self.model_name}' but "
                f"chunks use '{name}'; call rebuild() before reindexing "
                "(refusing to mix vector spaces)"
            )
        self.model_name = name
        for chunk in chunks:
            h = str(chunk.get("hash", ""))
            if not h:
                continue
            self._index[h] = {
                "source": chunk.get("source", "unknown"),
                "page": chunk.get("page", "?"),
                "content": chunk.get("content", ""),
            }

    def search(self, query: str, *, top_k: int = 5) -> dict[str, Any]:
        matches = keyword_search(query, self._index if self._index else None)
        results = [
            {**m, "score": float(m["score"])} for m in matches if m["score"] >= SCORE_THRESHOLD
        ][:top_k]
        return {
            "results": results,
            "fallback": True,
            "mode": "keyword",
            "not_found": len(results) == 0,
        }
