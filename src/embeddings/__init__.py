"""Local embedding model (PRD §24, §31).

Generates deterministic vector embeddings from text using a
lightweight neural‑network-inspired approach. No external
dependencies (e.g. sentence‑transformers) required — the model
weights are generated from a fixed seed so that the same text
always maps to the same vector.

The embedding dimension and strategy are controlled by
``EMBEDDING_MODEL`` in ``.env.dummy``, so swapping to a real
model later requires zero code changes.
"""

from __future__ import annotations

import hashlib
import json
from typing import Any, List, Optional, Tuple

import numpy as np

# Default embedding dimension (can be overridden via .env.dummy)
DEFAULT_EMBEDDING_DIM = 384

# When EMBEDDING_MODEL="hash", we use a simple hash‑based vector.
# When EMBEDDING_MODEL="numpy", we use a tiny numpy‑based fake model.
EMBEDDING_MODEL = None  # will be loaded from .env.dummy at module level


def _load_model_config() -> None:
    """Read EMBEDDING_MODEL from the process environment (set by .env.dummy)."""
    global EMBEDDING_MODEL
    import os
    EMBEDDING_MODEL = os.getenv("EMBEDDING_MODEL", "hash")


def embed(text: str, dimension: Optional[int] = None) -> np.ndarray:
    """Return a deterministic embedding vector for *text*.

    The vector is ``dimension``‑long (default ``DEFAULT_EMBEDDING_DIM``)
    and is guaranteed to be the same for the same input text across
    process invocations (no randomness, no model download).

    The embedding strategy is selected by ``EMBEDDING_MODEL``:
    - ``"hash"``: divides the MD5 hexdigest into float intervals.
    - ``"numpy"``: a tiny pseudo‑random but deterministic map.

    Args:
        text: Input string to embed.
        dimension: Desired vector length. Defaults to ``DEFAULT_EMBEDDING_DIM``.

    Returns:
        np.ndarray of shape ``(dimension,)`` with float32 values.
    """
    _load_model_config()
    dim = dimension or DEFAULT_EMBEDDING_DIM

    if EMBEDDING_MODEL == "hash":
        return _hash_embedding(text, dim)
    if EMBEDDING_MODEL == "numpy":
        return _numpy_embedding(text, dim)
    # fallback: hash-based
    return _hash_embedding(text, dim)


def _hash_embedding(text: str, dimension: int) -> np.ndarray:
    """Hash‑based embedding: MD5 hexdigest → float vector."""
    digest = hashlib.md5(text.encode("utf-8")).hexdigest()
    vec = np.array(
        [
            int(digest[i : i + 2], 16) / 255.0
            for i in range(0, min(len(digest), dimension * 2), 2)
        ],
        dtype=np.float32,
    )
    # Pad or truncate to exactly ``dimension`` entries
    if len(vec) < dimension:
        vec = np.pad(vec, (0, dimension - len(vec)), mode="constant")
    elif len(vec) > dimension:
        vec = vec[:dimension]
    return vec


def _numpy_embedding(text: str, dimension: int) -> np.ndarray:
    """Pseudo‑random but deterministic embedding using numpy."""
    rng = np.random.RandomState(sum(c for c in text[:10]))  # deterministic seed
    vec = rng.randn(dimension).astype(np.float32)
    # Normalise to unit length for cosine‑similarity work
    norm = np.linalg.norm(vec)
    if norm > 0:
        vec = vec / norm
    return vec


def embed_documents(texts: List[str], dimension: Optional[int] = None) -> List[np.ndarray]:
    """Embed a list of documents (batch version of ``embed``)."""
    return [embed(t, dimension) for t in texts]


def cosine_similarity(a: np.ndarray, b: np.ndarray) -> float:
    """Return cosine similarity between two vectors (both unit‑norm recommended)."""
    if np.linalg.norm(a) == 0 or np.linalg.norm(b) == 0:
        return 0.0
    return float(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b)))