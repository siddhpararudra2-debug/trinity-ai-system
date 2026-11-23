"""RAG pipeline — offline embeddings with honest, labelled fallbacks.

``RagPipeline`` ingests documents into an in-memory index and answers
queries.  Every result states which mode produced it:

- ``"embedding"`` — the local model from the ``[rag]`` extra;
- ``"hash"`` — the deterministic hashed n-gram fallback;
- ``"keyword"`` — pure lexical search when no vector is usable.

Below ``SCORE_THRESHOLD`` the answer is literally ``"not found"`` — the
nearest junk is never returned as an answer.  Chunks keep ``source`` and
``page`` so every answer is traceable.

The index records the embedding model name.  Reusing an index built with a
different model is refused with a clear message rather than silently
mixing vector spaces; ``rebuild_index()`` starts a fresh one.

``LLM-LINK:`` a future model could rerank or rephrase answers, but the
pipeline never calls one today — all scoring is deterministic.
"""

from __future__ import annotations

from typing import Any

from src.embeddings import (
    DEFAULT_EMBEDDING_DIM,
    EMBEDDING_MODEL_NAME,
    SCORE_THRESHOLD,
    chunk_text as _chunk_text,
    cosine,
    embed,
    embed_with_mode,
    get_mode,
    keyword_search,
    model_name,
)

__all__ = [
    "RagPipeline",
    "answer_question",
    "chunk_text",
    "embed",
    "index_chunks",
    "index_model_name",
    "init_dummy_corpus",
    "rebuild_index",
    "retrieve",
]


class IndexModelMismatch(RuntimeError):
    """Raised when an index was built with a different embedding model."""


# ---------------------------------------------------------------------------
# Index model-name tracking — never silently mix embedding spaces
# ---------------------------------------------------------------------------

_index_model_name: str | None = None


def index_model_name() -> str | None:
    """The embedder that produced the current in-memory index, if any."""
    return _index_model_name


def rebuild_index() -> None:
    """Discard the current index so a new model can populate it."""
    global _index_model_name
    _chunks.clear()
    _index_model_name = None


_chunks: dict[str, dict[str, Any]] = {}  # id -> {passage, source, page, vector, ...}


def _ensure_model_compatible() -> None:
    global _index_model_name
    current = model_name()
    if _index_model_name is not None and _index_model_name != current:
        raise IndexModelMismatch(
            f"index was built with embedding model '{_index_model_name}' but "
            f"the current model is '{current}'; call rebuild_index() before "
            "ingesting with a different model (refusing to mix vector spaces)"
        )
    if _index_model_name is None:
        _index_model_name = current


def index_chunks(chunks: list[dict[str, Any]]) -> None:
    """Add chunks (each with a ``hash``) to the in-memory index."""
    _ensure_model_compatible()
    for chunk in chunks:
        h = str(chunk.get("hash", ""))
        if not h:
            continue
        _chunks[h] = {
            "passage": chunk.get("content", ""),
            "source": chunk.get("source", "unknown"),
            "page": chunk.get("page", "?"),
        }


# ---------------------------------------------------------------------------
# Pipeline
# ---------------------------------------------------------------------------

class _Store:
    """Minimal store facade exposing a ``count`` attribute."""

    def __init__(self, pipeline: RagPipeline) -> None:
        self._pipeline = pipeline

    @property
    def count(self) -> int:
        return len(self._pipeline._documents)


class RagPipeline:
    """Ingest documents, then query them for relevant passages."""

    def __init__(self, model_name: str | None = None) -> None:
        self.model_name = model_name or EMBEDDING_MODEL_NAME
        self._documents: dict[str, dict[str, Any]] = {}
        self.store = _Store(self)

    @property
    def mode(self) -> str:
        return get_mode()

    def ingest(self, docs: list[dict[str, Any]]) -> None:
        """Ingest documents with ``id``, ``text`` and optional metadata."""
        _ensure_model_compatible()
        for doc in docs:
            doc_id = str(doc.get("id", ""))
            text = str(doc.get("text", ""))
            if not doc_id or not text:
                continue
            vec, mode = embed_with_mode(text, DEFAULT_EMBEDDING_DIM)
            self._documents[doc_id] = {
                "passage": text,
                "vector": vec,
                "mode": mode,
                "metadata": {
                    "id": doc_id,
                    "source": doc.get("source", "unknown"),
                    "page": doc.get("page", "?"),
                    "chapter": doc.get("chapter", ""),
                },
            }

    def query(self, text: str, k: int = 5) -> list[dict[str, Any]]:
        """Return up to *k* results ``{passage, similarity, metadata, mode}``.

        Results with a non-positive cosine score are dropped — an unrelated
        document is not a match, even when the store is non-empty.
        """
        if not self._documents or k <= 0:
            return []
        qvec, mode = embed_with_mode(text, DEFAULT_EMBEDDING_DIM)
        scored: list[dict[str, Any]] = []
        for doc in self._documents.values():
            score = cosine(qvec, doc["vector"])
            if score <= 0.0:
                continue
            scored.append(
                {
                    "passage": doc["passage"],
                    "similarity": score,
                    "metadata": dict(doc["metadata"]),
                    "mode": mode,
                }
            )
        scored.sort(key=lambda r: r["similarity"], reverse=True)
        return scored[:k]

    def answer(self, question: str, k: int = 5) -> dict[str, Any]:
        """Answer *question* or return a plain ``"not found"``.

        A result only counts when its similarity clears
        ``SCORE_THRESHOLD``; below that the nearest chunk is junk, not an
        answer.  The ``mode`` field says which embedder answered.
        """
        results = [
            r for r in self.query(question, k=k) if r["similarity"] >= SCORE_THRESHOLD
        ]
        if not results:
            return {
                "answer": "not found",
                "sources": [],
                "mode": self.mode,
                "fallback": self.mode != "embedding",
                "not_found": True,
            }
        return {
            "answer": " ".join(r["passage"] for r in results)[:500],
            "sources": [
                {"source": r["metadata"]["source"], "page": r["metadata"]["page"]}
                for r in results
            ],
            "mode": results[0]["mode"],
            "fallback": results[0]["mode"] != "embedding",
            "not_found": False,
        }


def init_dummy_corpus(pipeline: RagPipeline) -> None:
    """Populate *pipeline* with three small documents used by the evals."""
    pipeline.ingest(
        [
            {
                "id": "trinity_arch",
                "text": (
                    "Trinity AI is an engineering operating system that "
                    "supports CAD, PCB, firmware and simulation engines."
                ),
                "source": "trinity_arch.md",
                "page": 1,
            },
            {
                "id": "design_guide",
                "text": (
                    "Trinity AI supports CAD schematic capture and PCB "
                    "netlist generation with deterministic engines."
                ),
                "source": "design_guide.md",
                "page": 5,
                "chapter": "Intro",
            },
            {
                "id": "fw_guide",
                "text": (
                    "The firmware engine generates C source code for ESP32 "
                    "MCUs and checks pin mappings against the board."
                ),
                "source": "fw_guide.md",
                "page": 3,
                "chapter": "Firmware",
            },
        ]
    )


# ---------------------------------------------------------------------------
# Functional retrieval API (mode labels + "not found")
# ---------------------------------------------------------------------------

def retrieve(
    query: str,
    *,
    top_k: int = 5,
    use_embeddings: bool | None = None,
    index: dict[str, dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """Retrieve chunks for *query*, labelling the mode that answered.

    The vector path scores each chunk with cosine similarity when a usable
    embedding is present; otherwise pure keyword search is used and labelled
    ``"keyword"``.  Scores below ``SCORE_THRESHOLD`` yield ``not_found``.
    """
    import os

    if use_embeddings is None:
        use_embeddings = os.getenv(
            "TRINITY_USE_EMBEDDINGS", "true"
        ).lower() not in ("0", "false", "no")

    source = index if index is not None else _chunks
    results: list[dict[str, Any]] = []
    mode = "hash"

    if use_embeddings and source:
        qvec, mode = embed_with_mode(query, DEFAULT_EMBEDDING_DIM)
        for info in source.values():
            text = str(info.get("passage", info.get("content", "")))
            if not text:
                continue
            score = cosine(qvec, embed(text, DEFAULT_EMBEDDING_DIM))
            if score < SCORE_THRESHOLD:
                continue
            results.append(
                {
                    "source": info.get("source", "unknown"),
                    "page": info.get("page", "?"),
                    "content": text,
                    "score": score,
                    "mode": mode,
                }
            )
        results.sort(key=lambda r: r["score"], reverse=True)
    else:
        mode = "keyword"
        results = keyword_search(query, source)

    return {
        "results": results[:top_k],
        "fallback": mode != "embedding",
        "mode": mode,
        "not_found": not results,
    }


def answer_question(
    question: str,
    *,
    top_k: int = 5,
    use_embeddings: bool | None = None,
    index: dict[str, dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """Answer *question* from the index, or return a plain ``"not found"``."""
    r = retrieve(question, top_k=top_k, use_embeddings=use_embeddings, index=index)
    if r["not_found"] or not r["results"]:
        return {
            "answer": "not found",
            "sources": [],
            "fallback": r["fallback"],
            "mode": r["mode"],
            "not_found": True,
        }
    return {
        "answer": " ".join(str(x["content"]) for x in r["results"])[:500],
        "sources": [{"source": x["source"], "page": x["page"]} for x in r["results"]],
        "fallback": r["fallback"],
        "mode": r["mode"],
        "not_found": False,
    }


def chunk_text(text: str, chunk_size: int = 50, chunk_overlap: int = 10) -> list[str]:
    """Re-export of :func:`src.embeddings.chunk_text` for callers of ``src.rag``."""
    return _chunk_text(text, chunk_size=chunk_size, chunk_overlap=chunk_overlap)
