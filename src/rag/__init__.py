"""Literature RAG (PRD §24, §31).

Ingest, chunk, embed locally (EMBEDDING_MODEL), vector index,
and answers with sources.

The pipeline is entirely driven by ``.env.dummy`` values
(``EMBEDDING_MODEL``, ``VECTOR_STORE_BACKEND``, ``CHUNK_SIZE``,
etc.), so swapping to a real LLM‑backed retriever later requires
no code changes.
"""

from __future__ import annotations

from typing import Any

from src.embeddings import embed
from src.vector_store import VectorStore, ingest_documents


# ------------------------------------------------------------------
# Chunking utilities
# ------------------------------------------------------------------

def chunk_text(
    text: str,
    chunk_size: int = 512,
    chunk_overlap: int = 50,
) -> list[str]:
    """Split *text* into overlapping chunks of *chunk_size* tokens.

    A crude word‑based splitter is used so that no external
    tokeniser is required.  ``chunk_overlap`` ensures context
    at chunk boundaries.

    Args:
        text: Raw text to split.
        chunk_size: Target number of words per chunk.
        chunk_overlap: Number of words to retain at the end of
            one chunk and the start of the next.

    Returns
    -------
    list of chunk strings.
    """
    words = text.split()
    if not words:
        return []
    chunks: list[str] = []
    start = 0
    while start < len(words):
        end = min(start + chunk_size, len(words))
        chunk_words = words[start:end]
        chunks.append(" ".join(chunk_words))
        start += chunk_size - chunk_overlap
    return chunks


# ------------------------------------------------------------------
# RAG pipeline
# ------------------------------------------------------------------

class RagPipeline:
    """End‑to‑end Retrieval‑Augmented Generation pipeline.

    Steps
    -----
    1. ``ingest`` one or more documents.
    2. ``query`` for the most relevant passages given a user question.
    3. Return answered passages with their source metadata.
    """

    def __init__(
        self,
        store: VectorStore | None = None,
        chunk_size: int = 512,
        chunk_overlap: int = 50,
        embedding_dim: int = 384,
    ) -> None:
        self.store = store or VectorStore(dimension=embedding_dim)
        self.chunk_size = chunk_size
        self.chunk_overlap = chunk_overlap
        self.embedding_dim = embedding_dim

    def ingest(self, documents: list[dict[str, Any]]) -> None:
        """Add documents to the pipeline's vector store.

        Each document dict should contain at least a ``text`` field.
        ``id``, ``source``, and ``page`` are example metadata fields.
        """
        ingest_documents(
            self.store,
            documents,
            text_key="text",
            id_key="id",
            meta_keys=["source", "page", "chapter"],
        )

    def query(self, question: str, k: int = 4) -> list[dict[str, Any]]:
        """Return the top‑k most relevant passages for *question*.

        Each result is a dict with:
        - ``passage``: the text chunk
        - ``similarity``: cosine similarity score
        - ``metadata``: source/page/chapter etc.

        If no relevant passage is found, an empty list is returned.
        """
        if self.store.count == 0:
            return []

        # Embed the question
        q_vec = embed(question, self.embedding_dim)

        # Search the store
        hits = self.store.search(q_vec, k=k)

        results: list[dict[str, Any]] = []
        for vid, sim, meta in hits:
            # The passage is stored in the metadata if available,
            # otherwise we reconstruct from the id or return empty.
            passage = meta.get("text", "")
            results.append(
                {
                    "passage": passage,
                    "similarity": round(sim, 4),
                    "metadata": {k: v for k, v in meta.items() if v is not None},
                }
            )
        return results


# ------------------------------------------------------------------
# Convenience: dummy data ingest for quick start
# ------------------------------------------------------------------

# A small dummy corpus so that ``RagPipeline`` works out‑of‑the‑box
# when no external data has been loaded yet.
_DUMMY_CORPUS: list[dict[str, Any]] = [
    {
        "id": "trinity_arch",
        "text": (
            "Trinity is a full‑stack electronic design automation platform "
            "supporting CAD schematic capture, PCB netlist generation, "
            "firmware source creation, and verification reporting."
        ),
        "source": "design_guide.md",
        "page": 1,
    },
    {
        "id": "pcb_intro",
        "text": (
            "The PCB engine generates a board outline, places components "
            "from a netlist, and exports Gerber files for fabrication."
        ),
        "source": "pcb_guide.md",
        "page": 3,
    },
    {
        "id": "firmware_intro",
        "text": (
            "The firmware engine generates C source code for the selected "
            "MCU, including UART, SPI and I2C peripheral initialization.",
        ),
        "source": "fw_guide.md",
        "page": 7,
    },
]


def init_dummy_corpus(pipeline: RagPipeline) -> None:
    """Populate the pipeline with the dummy corpus (useful for demos)."""
    pipeline.ingest(_DUMMY_CORPUS)