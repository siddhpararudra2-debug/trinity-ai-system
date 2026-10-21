"""Vector store: deterministic embeddings, cosine ranking, SQLite persistence."""

from __future__ import annotations

import pytest

from src.utils.vector_store import VectorStore, cosine, embed


def test_embedding_is_deterministic_and_normalized():
    first = embed("quadcopter frame generator", dim=128)
    second = embed("quadcopter frame generator", dim=128)
    assert first == second
    assert len(first) == 128
    norm = sum(v * v for v in first) ** 0.5
    assert norm == pytest.approx(1.0)


def test_embedding_different_text_differs():
    assert embed("bold bracket") != embed("quiet tendon")


def test_embed_empty_text_is_zero_vector():
    vec = embed("", dim=64)
    assert all(v == 0.0 for v in vec)


def test_embed_rejects_tiny_dim():
    with pytest.raises(ValueError):
        embed("text", dim=4)


def test_cosine_bounds_and_degenerate_inputs():
    assert cosine([1.0, 0.0], [1.0, 0.0]) == pytest.approx(1.0)
    assert cosine([1.0, 0.0], [0.0, 1.0]) == pytest.approx(0.0)
    assert cosine([1.0, 0.0], [-1.0, 0.0]) == pytest.approx(-1.0)
    assert cosine([0.0, 0.0], [1.0, 0.0]) == 0.0
    assert cosine([0.0, 0.0], [0.0, 0.0]) == 0.0


def test_add_and_search_exact_match_first(tmp_path):
    store = VectorStore(persist_dir=tmp_path, dim=96)
    store.add_text("cad", "quadcopter frame CAD generation", {"engine": "cad"})
    store.add_text("pcb", "PCB board routing ERC checks", {"engine": "pcb"})
    store.add_text("math", "symbolic algebra solver", {"engine": "math"})

    results = store.search("quadcopter frame CAD generation", k=3)
    assert len(results) == 3
    assert results[0]["id"] == "cad"
    assert results[0]["metadata"] == {"engine": "cad"}
    assert results[0]["score"] == pytest.approx(1.0)
    # Ranking order: unrelated docs score below the exact match.
    assert results[0]["score"] > results[1]["score"]

    # A partial query still ranks the nearest doc first, below 1.0.
    partial = store.search("quadcopter frame CAD", k=3)
    assert partial[0]["id"] == "cad"
    assert 0.0 < partial[0]["score"] < 1.0


def test_metadata_round_trip_and_upsert(tmp_path):
    store = VectorStore(persist_dir=tmp_path)
    store.add_text("spec-a", "heatshield manifold", {"job": "j1", "v": 2})
    # Upsert: same id replaces (count stays 1) with fresh metadata.
    store.add_text("spec-a", "heatshield manifold", {"job": "j2"})
    assert store.count() == 1
    results = store.search("heatshield manifold", k=1)
    assert results[0]["metadata"] == {"job": "j2"}


def test_delete_count_clear(tmp_path):
    store = VectorStore(persist_dir=tmp_path)
    store.add("one", embed("alpha beta", 64))
    store.add("two", embed("gamma delta", 64))
    assert store.count() == 2
    assert store.delete("one") is True
    assert store.delete("one") is False
    assert store.count() == 1
    store.clear()
    assert store.count() == 0


def test_empty_store_queries_cleanly(tmp_path):
    store = VectorStore(persist_dir=tmp_path)
    # A zero vector queries an empty store without a crash.
    assert store.search("anything", k=3) == []


def test_query_k_bounded_and_others_ranked(tmp_path):
    store = VectorStore(persist_dir=tmp_path, dim=64)
    for i, phrase in enumerate(
        ["stepper motor torque", "stepper motor speed", "belt tension", "imu fusion"]
    ):
        store.add_text(f"doc-{i}", phrase)
    results = store.query(embed("stepper motor", 64), k=2)
    assert len(results) == 2
    scores = [item["score"] for item in results]
    assert scores == sorted(scores, reverse=True)


def test_add_rejects_bad_inputs(tmp_path):
    store = VectorStore(persist_dir=tmp_path)
    with pytest.raises(ValueError):
        store.add("", [1.0])
    with pytest.raises(ValueError):
        store.add("id", [])
    with pytest.raises(ValueError):
        store.query([])
    with pytest.raises(ValueError):
        store.query([1.0], k=0)


def test_explicit_vectors_coexist_with_text_vectors(tmp_path):
    store = VectorStore(persist_dir=tmp_path)
    store.add_text("text-doc", "lidar calibration")
    store.add("manual-doc", [1.0, 0.0, 0.0], {"source": "manual"})
    assert store.count() == 2
    # Mixed-dim rows coexist; the store does not pretend they are comparable.
    hit = store.query([1.0, 0.0, 0.0], k=2)
    assert hit[0]["id"] == "manual-doc"
    assert hit[0]["score"] == pytest.approx(1.0)


def test_persistence_survives_reopen(tmp_path):
    live = VectorStore(persist_dir=tmp_path)
    live.add_text("persistent", "regenerative accumulator", {"n": 7})
    live.close()

    reopened = VectorStore(persist_dir=tmp_path)
    assert reopened.count() == 1
    results = reopened.search("regenerative accumulator", k=1)
    assert results[0]["id"] == "persistent"
    reopened.close()