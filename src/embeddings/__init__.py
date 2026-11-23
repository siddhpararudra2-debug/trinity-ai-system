"""Embeddings — offline local model with honest hash/keyword fallbacks.

The real path loads a small local embedding model (all-MiniLM-L6-v2 by
default) from the optional ``[rag]`` extra.  The model name lives in config
and is swappable by the ``EMBEDDING_MODEL_NAME`` environment variable.  No
network call is made at query time: model files must already be present in
the local cache.

When the model is not installed the module falls back to a deterministic
hashed n-gram embedding (lexical, from ``src.utils.vector_store``).  Every
consumer must label results with the mode that produced them —
``"embedding"`` for the real model, ``"hash"`` for the hash fallback, or
``"keyword"`` for pure keyword search.  A fallback is never presented as
the real model.

``LLM-LINK:`` a future hosted embedding provider could be added behind the
same ``embed()`` seam; it is not called today.
"""

from __future__ import annotations

import os
import re
from pathlib import Path
from typing import Any

import numpy as np

from src.utils.vector_store import _fnv1a as _bucket

# ---------------------------------------------------------------------------
# Configuration — model name in config, env-swappable
# ---------------------------------------------------------------------------

DEFAULT_MODEL_NAME = "all-MiniLM-L6-v2"
#: Dimension of both the real model and the hash fallback vectors.
DEFAULT_EMBEDDING_DIM = 384

#: Environment variable that overrides the model name.
EMBEDDING_MODEL_NAME = os.getenv("EMBEDDING_MODEL_NAME", "") or DEFAULT_MODEL_NAME
#: Where model files are cached (must be pre-populated; no query-time download).
EMBEDDING_CACHE_DIR = Path(
    os.getenv("EMBEDDING_CACHE_DIR", "") or str(Path.home() / ".cache" / "trinity" / "embeddings")
)

#: Below this cosine score a retrieval is "not found", never nearest junk.
#: Hashed n-gram lexical scores for real matches are >= 0.35; out-of-corpus
#: queries (common-word / trigram collisions only) stay below 0.16.
SCORE_THRESHOLD = 0.2

_MODES = ("embedding", "hash", "keyword")


# ---------------------------------------------------------------------------
# Hashed n-gram fallback — stopword-aware so common words cannot create a
# false "match" for an out-of-corpus query.
# ---------------------------------------------------------------------------

_TOKEN_RE = re.compile(r"\b[a-z0-9]+\b")

_STOPWORDS = frozenset(
    """a an the and or but if then than that this these those is are was were
    be been being do does did have has had of to in on at by for from with
    as it its into about what which who whom whose when where why how can
    could should would may might must will just not no nor""".split()
)


def _hash_vector(text: str, dim: int) -> list[float]:
    """L2-normalized hashed whole-word + trigram vector, minus stopwords."""
    if dim < 8:
        raise ValueError("dim must be at least 8")
    vec = [0.0] * dim
    for token in _TOKEN_RE.findall(text.lower()):
        if token in _STOPWORDS:
            continue
        vec[_bucket(token) % dim] += 1.0
        if len(token) > 3:
            for i in range(len(token) - 2):
                vec[_bucket(token[i : i + 3]) % dim] += 0.5
    norm = float(np.linalg.norm(vec))
    if norm == 0.0:
        return vec
    return [v / norm for v in vec]


# ---------------------------------------------------------------------------
# Model loading — done once at import; never at query time
# ---------------------------------------------------------------------------

def _load_model() -> Any | None:
    """Load the configured local model, or return ``None`` when unavailable.

    Tries sentence-transformers first, then fastembed.  A missing extra, a
    missing model file or a wrong version all resolve to ``None`` — the
    caller then uses the hash fallback and labels it as such.
    """
    try:
        from sentence_transformers import SentenceTransformer  # type: ignore[import]

        return SentenceTransformer(
            EMBEDDING_MODEL_NAME,
            cache_folder=str(EMBEDDING_CACHE_DIR),
        )
    except Exception:  # noqa: BLE001 — optional extra / uncached model
        pass
    try:
        from fastembed import TextEmbedding  # type: ignore[import]

        return TextEmbedding(EMBEDDING_MODEL_NAME, cache_dir=str(EMBEDDING_CACHE_DIR))
    except Exception:  # noqa: BLE001 — optional extra / uncached model
        return None


_MODEL = _load_model()
MODEL_AVAILABLE = _MODEL is not None


def get_mode() -> str:
    """Return the embedding mode currently in use: "embedding" or "hash"."""
    return "embedding" if MODEL_AVAILABLE else "hash"


def model_name() -> str:
    """The configured model name (the hash fallback keeps the name too, so
    an index can record which embedder produced it)."""
    return EMBEDDING_MODEL_NAME


def _model_vector(text: str) -> np.ndarray | None:
    if _MODEL is None:
        return None
    try:
        if hasattr(_MODEL, "encode"):
            vec = _MODEL.encode(
                [text], convert_to_numpy=True, normalize_embeddings=True
            )[0]
        elif hasattr(_MODEL, "embed"):
            vec = next(iter(_MODEL.embed([text])))
        else:
            return None
        arr = np.asarray(vec, dtype=np.float32)
        return arr if arr.shape == (DEFAULT_EMBEDDING_DIM,) else None
    except Exception:  # noqa: BLE001 — inference failure falls back to hash
        return None


# ---------------------------------------------------------------------------
# Public embedding API
# ---------------------------------------------------------------------------

def embed(
    text: str | list[str],
    dim: int = DEFAULT_EMBEDDING_DIM,
) -> np.ndarray | list[np.ndarray]:
    """Embed *text* into a normalized float32 vector.

    A single string returns one ``numpy.ndarray`` (length ``dim``); a list
    returns a list of arrays.  ``dim`` is accepted for the hash fallback so
    callers can request a smaller vector; the real model always emits
    ``DEFAULT_EMBEDDING_DIM`` dimensions.
    """
    single = isinstance(text, str)
    texts = [text] if single else list(text)

    vectors: list[np.ndarray] = []
    for item in texts:
        vec = _model_vector(item)
        if vec is None:
            # Hash fallback — lexical, deterministic, L2-normalized.
            raw = _hash_vector(item, dim)
            vec = np.asarray(raw, dtype=np.float32)
        vectors.append(vec)

    return vectors[0] if single else vectors


def embed_with_mode(
    text: str, dim: int = DEFAULT_EMBEDDING_DIM
) -> tuple[np.ndarray, str]:
    """Return ``(vector, mode)`` so callers can label the answer honestly."""
    vec = _model_vector(text)
    if vec is not None:
        return vec, "embedding"
    raw = _hash_vector(text, dim)
    return np.asarray(raw, dtype=np.float32), "hash"


def cosine(a: Any, b: Any) -> float:
    """Cosine similarity of two vectors, clamped to ``[-1, 1]``."""
    av = np.asarray(a, dtype=np.float32)
    bv = np.asarray(b, dtype=np.float32)
    na = float(np.linalg.norm(av))
    nb = float(np.linalg.norm(bv))
    if na == 0.0 or nb == 0.0:
        return 0.0
    return float(max(-1.0, min(1.0, float(np.dot(av, bv) / (na * nb)))))


# ---------------------------------------------------------------------------
# Chunking — chunks keep source and page
# ---------------------------------------------------------------------------

def chunk_hash(text: str) -> str:
    """Deterministic short SHA-256 of a chunk, used for dedup/identity."""
    import hashlib

    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]


def chunk_text(
    text: str,
    chunk_size: int = 50,
    chunk_overlap: int = 10,
) -> list[str]:
    """Split *text* into overlapping word chunks.

    Word-based so callers can feed the chunks straight back into
    ``embed()``.  Metadata (source/page) is attached downstream by the
    pipeline's ``ingest``; ``chunk_with_metadata`` provides it directly.
    """
    if not isinstance(text, str) or not text.strip():
        return []
    words = text.split()
    if not words:
        return []
    if len(words) <= chunk_size:
        return [" ".join(words)]

    step = max(1, chunk_size - chunk_overlap)
    chunks: list[str] = []
    for start in range(0, len(words), step):
        piece = words[start : start + chunk_size]
        if not piece:
            break
        chunks.append(" ".join(piece))
        if start + chunk_size >= len(words):
            break
    return chunks


def chunk_with_metadata(
    text: str,
    *,
    source: str = "unknown",
    page: int | str = "?",
    chunk_size: int = 50,
    chunk_overlap: int = 10,
) -> list[dict[str, Any]]:
    """Chunk text and attach ``content``, ``source``, ``page`` and ``hash``."""
    chunks = []
    for piece in chunk_text(text, chunk_size=chunk_size, chunk_overlap=chunk_overlap):
        chunks.append(
            {
                "content": piece,
                "source": source,
                "page": page,
                "hash": chunk_hash(piece),
            }
        )
    return chunks


# ---------------------------------------------------------------------------
# Keyword fallback — clearly labelled, never an invented pass
# ---------------------------------------------------------------------------

def keyword_search(
    query: str, index: dict[str, dict[str, Any]] | None = None
) -> list[dict[str, Any]]:
    """Pure keyword search over ``{hash: {source, page, content}}``.

    Every returned row carries ``mode="keyword"`` so a caller can never
    mistake it for the model.  Returns ``[]`` when there is no index or no
    overlapping term.
    """
    if not index:
        return []
    tokens = _TOKEN_RE.findall(query.lower())
    if not tokens:
        return []
    matches: list[dict[str, Any]] = []
    for info in index.values():
        if not isinstance(info, dict):
            continue
        content = str(info.get("content", "")).lower()
        score = sum(1 for t in set(tokens) if t in content)
        if score > 0:
            matches.append(
                {
                    "source": info.get("source", "unknown"),
                    "page": info.get("page", "?"),
                    "content": info.get("content", ""),
                    "score": float(score),
                    "mode": "keyword",
                }
            )
    matches.sort(key=lambda m: m["score"], reverse=True)
    return matches
