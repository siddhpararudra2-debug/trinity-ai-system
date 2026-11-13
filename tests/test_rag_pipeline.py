"""A5 Literature RAG — retrieval eval tests.

Tests the RagPipeline's ingest + query round-trip with the dummy corpus
and additional test documents. Verifies that:

- Ingestion stores documents with metadata (source, page, chapter).
- Query returns the top‑k most relevant passages with similarity scores.
- Empty store returns an empty list.
- Similarity values are in [-1, 1].
- Each result carries passage text and metadata.
"""

import sys
sys.path.insert(0, r"C:\Users\rudra\Desktop\trinity ai\trinity-ai-system")

from src.rag import RagPipeline, init_dummy_corpus, embed, chunk_text


class TestRagPipelineIngest:
    """Test that documents are ingested correctly."""

    def test_ingest_dummy_corpus(self):
        """Init_dummy_corpus populates the pipeline with 3 documents."""
        pipeline = RagPipeline()
        init_dummy_corpus(pipeline)
        assert pipeline.store.count == 3

    def test_ingest_custom_documents(self):
        """Custom documents are stored with their metadata."""
        pipeline = RagPipeline()
        docs = [
            {
                "id": "doc-001",
                "text": "Trinity AI supports CAD schematic capture and PCB netlist generation.",
                "source": "design_guide.md",
                "page": 5,
                "chapter": "Intro",
            },
            {
                "id": "doc-002",
                "text": "The firmware engine generates C source code for ESP32 MCUs.",
                "source": "fw_guide.md",
                "page": 3,
                "chapter": "Firmware",
            },
        ]
        pipeline = RagPipeline()
        pipeline.ingest(docs)
        assert pipeline.store.count == 2


class TestRagPipelineQuery:
    """Test that queries return relevant passages."""

    def test_query_with_dummy_corpus(self):
        """Query against the dummy corpus returns relevant passages."""
        pipeline = RagPipeline()
        init_dummy_corpus(pipeline)
        results = pipeline.query("What does Trinity AI support?", k=2)
        assert len(results) > 0
        # The trinity_arch doc should match
        ids = {r["metadata"].get("id") for r in results}
        assert "trinity_arch" in ids or "design_guide" in str(ids)

    def test_query_returns_passage_and_metadata(self):
        """Each result carries passage text and metadata fields."""
        pipeline = RagPipeline()
        init_dummy_corpus(pipeline)
        results = pipeline.query("ESP32 firmware", k=1)
        assert len(results) == 1
        r = results[0]
        assert "passage" in r
        assert "similarity" in r
        assert "metadata" in r
        # passage should not be empty
        assert len(r["passage"]) > 0
        # metadata should contain source/page
        assert "source" in r["metadata"]
        assert "page" in r["metadata"]

    def test_query_empty_store(self):
        """Query against an empty store returns an empty list."""
        pipeline = RagPipeline()
        results = pipeline.query("anything", k=5)
        assert results == []

    def test_similarity_range(self):
        """Similarity scores are within [-1, 1]."""
        pipeline = RagPipeline()
        init_dummy_corpus(pipeline)
        results = pipeline.query("Trinity", k=3)
        for r in results:
            sim = r["similarity"]
            assert -1.0 <= sim <= 1.0, f"Similarity {sim} out of range"


class TestRagPipelineChunking:
    """Test that chunk_text splits text correctly."""

    def test_chunk_text_basic(self):
        """Basic chunking produces overlapping chunks."""
        text = " ".join([f"word{i}" for i in range(200)])
        chunks = chunk_text(text, chunk_size=50, chunk_overlap=10)
        assert len(chunks) > 1
        # First chunk should have ~50 words
        first_words = chunks[0].split()
        assert 40 <= len(first_words) <= 55

    def test_chunk_text_empty(self):
        """Empty text produces no chunks."""
        assert chunk_text("", chunk_size=50, chunk_overlap=10) == []

    def test_chunk_text_single_word(self):
        """Single word produces one chunk."""
        assert chunk_text("hello", chunk_size=50, chunk_overlap=10) == ["hello"]


class TestRagPipelineIntegration:
    """Integration-style tests for the full pipeline."""

    def test_end_to_end_ingest_query(self):
        """Ingest then query: the queried passage should match the injected text."""
        pipeline = RagPipeline()
        docs = [
            {
                "id": "eval-parts",
                "text": "The enclosure part has parameters length, width, height, and wall_thickness.",
                "source": "parts_guide.md",
                "page": 2,
            },
            {
                "id": "eval-embed",
                "text": "The embedding model uses a hash-based deterministic vector.",
                "source": "embeddings_guide.md",
                "page": 1,
            },
        ]
        pipeline = RagPipeline()
        pipeline.ingest(docs)
        # Query with a phrase from the first doc
        results = pipeline.query("enclosure part has parameters", k=1)
        assert len(results) > 0
        # The first result should be the injected doc
        assert "enclosure part" in results[0]["passage"]

    def test_query_kmatches_k(self):
        """Requesting k results returns approximately k results (may be less if store is small)."""
        pipeline = RagPipeline()
        init_dummy_corpus(pipeline)
        results = pipeline.query("Trinity", k=5)
        assert len(results) <= 5
        assert len(results) >= 1


class TestRagPipelineEmbeddings:
    """Test the embedding function directly."""

    def test_embed_returns_numpy_array(self):
        """embed() returns a numpy ndarray of the expected dimension."""
        vec = embed("test text")
        import numpy as np
        assert isinstance(vec, np.ndarray)
        assert vec.dtype == np.float32
        assert len(vec) == 384  # DEFAULT_EMBEDDING_DIM

    def test_embed_deterministic(self):
        """Same text always produces the same vector."""
        vec1 = embed("deterministic text")
        vec2 = embed("deterministic text")
        import numpy as np
        assert np.allclose(vec1, vec2)

    def test_embed_different_text_different_vector(self):
        """Different texts produce different vectors."""
        vec1 = embed("alpha")
        vec2 = embed("beta")
        import numpy as np
        assert not np.allclose(vec1, vec2)


class TestRagPipelineChunkingIntegration:
    """Integration of chunk_text with the pipeline."""

    def test_chunk_text_feeds_pipeline(self):
        """Chunked text can be ingested and queried."""
        pipeline = RagPipeline()
        # chunk_text returns a list of strings
        text = " ".join([f"concept{i}" for i in range(100)])
        chunks = chunk_text(text, chunk_size=30, chunk_overlap=5)
        pipeline.ingest(
            [{"id": "chunk-test", "text": " ".join(chunks), "source": "test.md", "page": 1}]
        )
        # Query should find something
        results = pipeline.query("concept", k=3)
        assert len(results) > 0