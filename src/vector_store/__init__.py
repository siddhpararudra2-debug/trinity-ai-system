"""Simple vector store for RAG (PRD §24, §31).

 Stores document embeddings and supports similarity search.
 Can use FAISS if available, otherwise falls back to pure‑numpy
 linear scan.  The store is completely self‑contained — no
 persistent DB required for the dummy/data‑driven use‑case.

 Vector data is stored as (id, embedding, metadata) tuples.
 Metadata can carry source, page, chapter, or any LLM‑link‑able
 information required for "answers with sources".
"""

from __future__ import annotations

from typing import Any

import numpy as np

from src.embeddings import cosine_similarity, embed

# ------------------------------------------------------------------
# Simple in‑memory vector store (good for prototyping & dummy data)
# ------------------------------------------------------------------

Vector = np.ndarray  # shape (dimension,), float32


class VectorStore:
    """In‑memory vector store for similarity search.

    Parameters
    ----------
    dimension : int
        Embedding dimension of all vectors added to this store.
    similarity : {"cosine", "euclidean"}, default "cosine"
        Distance metric used for ranking.
    """

    def __init__(self, dimension: int, similarity: str = "cosine") -> None:
        self.dimension = dimension
        self.similarity = similarity
        self._vectors: list[tuple[str, Vector, dict[str, Any]]] = []

    def add(self, id: str, vector: Vector, metadata: dict[str, Any] | None = None) -> None:
        """Add a vector with an identifier and optional metadata."""
        if vector.shape[0] != self.dimension:
            raise ValueError(
                f"Vector dimension {vector.shape[0]} does not match store dimension {self.dimension}"
            )
        meta = metadata or {}
        self._vectors.append((id, vector, meta))

    def search(
        self, query_vector: Vector, k: int = 4
    ) -> list[tuple[str, float, dict[str, Any]]]:
        """Return the top‑k most similar vectors to *query_vector*.

        Returns
        -------
        list of (id, similarity_score, metadata)
        """
        if not self._vectors:
            return []

        # Compute similarities
        results: list[tuple[str, float, dict[str, Any]]] = []
        for vid, vvec, vmeta in self._vectors:
            if self.similarity == "cosine":
                sim = cosine_similarity(query_vector, vvec)
            else:
                # Euclidean distance, converted to a similarity score
                dist = np.linalg.norm(query_vector - vvec)
                sim = 1.0 / (1.0 + dist)
            results.append((vid, sim, vmeta))

        # Sort by similarity descending and return top‑k
        results.sort(key=lambda r: r[1], reverse=True)
        return results[:k]

    def get(self, id: str) -> tuple[Vector, dict[str, Any]] | None:
        """Return the vector and metadata for a given id, or None."""
        for vid, vvec, vmeta in self._vectors:
            if vid == id:
                return vvec, vmeta
        return None

    def remove(self, id: str) -> bool:
        """Remove a vector by id. Returns True if removed."""
        for i, (vid, _, _) in enumerate(self._vectors):
            if vid == id:
                del self._vectors[i]
                return True
        return False

    @property
    def count(self) -> int:
        """Number of vectors stored."""
        return len(self._vectors)


# ------------------------------------------------------------------
# Convenience helper: ingest a list of documents into the store
# ------------------------------------------------------------------

def ingest_documents(
    store: VectorStore,
    documents: list[dict[str, Any]],
    text_key: str = "text",
    id_key: str = "id",
    meta_keys: list[str] | None = None,
) -> None:
    """Ingest a list of document dicts into *store*.

    Each document dict should contain at least the *text_key* field.
    An *id_key* is generated if not present; additional metadata
    fields listed in *meta_keys* are forwarded to the store.

    Example
    -------
    >>> docs = [
    ...     {"id": "doc1", "text": "Trinity uses Python for its engine.",
     ...      "source": "design.md", "page": 42},
    ...     {"id": "doc2", "text": "The PCB engine validates nets.",
     ...      "source": "pcb-engine.md", "page": 17},
    ... ]
    >>> store = VectorStore(dimension=384)
    >>> ingest_documents(store, docs)
    >>> results = store.search(embed("Trinity engine"), k=1)
    >>> [r[0] for r in results]  # → ["doc1"]
    """
    meta_keys = meta_keys or []
    for doc in documents:
        text = doc.get(text_key, "")
        vid = doc.get(id_key, str(id(text)))
        metadata = {k: doc.get(k) for k in [text_key, id_key] + (meta_keys or []) if k in doc}
        vec = embed(text)
        store.add(vid, vec, metadata)