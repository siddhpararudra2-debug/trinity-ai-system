"""M18.4: Python research engine — search over request or repo documents.

The hit shape mirrors the native engine (rank/doc_id/title/source/score/
matched_terms/occurrences/snippet), so these tests pin scoring, ranking,
stopword handling, and the two corpora.
"""

from __future__ import annotations

import pytest

from src.core.errors import CapabilityUnavailableError, RequestValidationError
from src.engines import registry as registry_module
from src.engines.pcb import PcbEngine
from src.engines.research import engine as engine_module
from src.engines.research.engine import ResearchEngine
from src.engines.scaffold import ScaffoldEngine


@pytest.fixture()
def engine() -> ResearchEngine:
    return ResearchEngine()


@pytest.fixture(autouse=True)
def _fresh_repo_corpus(monkeypatch):
    monkeypatch.setattr(engine_module, "_REPO_CORPUS", None)


def _documents():
    return [
        {"doc_id": "d1", "title": "Spring chain", "text": "spring chain integration"},
        {"doc_id": "d2", "title": "Other", "text": "spring only"},
        {"doc_id": "d3", "title": "Chain", "text": "chain only"},
    ]


def test_describe_advertises_only_search(engine):
    described = engine.describe()
    assert described["name"] == "research"
    assert described["capabilities"] == ["search"]
    assert "status" not in described


def test_search_ranks_request_documents_like_native(engine):
    result = engine.execute(
        "search", {"query": "spring chain", "documents": _documents()}
    )
    data = result.result
    assert result.success is True
    assert data["corpus"] == "request"
    assert data["query_terms"] == ["spring", "chain"]
    assert data["total_documents"] == 3
    assert data["hit_count"] == 3
    assert [hit["doc_id"] for hit in data["hits"]] == ["d1", "d3", "d2"]
    first = data["hits"][0]
    assert first["rank"] == 1
    assert first["title"] == "Spring chain"
    assert first["score"] == 1.25
    assert first["matched_terms"] == ["spring", "chain"]
    assert first["occurrences"] == 4  # 2 body + 2 title
    assert first["snippet"] == "spring chain integration"
    assert data["hits"][1]["score"] == 0.625
    assert data["hits"][2]["score"] == 0.5
    assert result.validation is not None
    assert result.validation.status == "VALIDATED"


def test_limit_truncates_hits(engine):
    data = engine.execute(
        "search", {"query": "spring chain", "documents": _documents(), "limit": 1}
    ).result
    assert data["hit_count"] == 1
    assert len(data["hits"]) == 1
    assert data["hits"][0]["doc_id"] == "d1"


def test_stopwords_and_short_tokens_produce_no_terms(engine):
    data = engine.execute(
        "search", {"query": "the a x", "documents": _documents()}
    ).result
    assert data["query_terms"] == []
    assert data["hit_count"] == 0
    assert data["hits"] == []
    assert data["total_documents"] == 3


def test_search_reports_the_repo_corpus(engine):
    data = engine.execute("search", {"query": "migration seam"}).result
    assert data["corpus"] == "repo_docs"
    assert data["total_documents"] >= 5
    assert data["hit_count"] >= 1
    sources = {hit["source"] for hit in data["hits"]}
    assert any(
        source in ("work.md", "opencodeworks.md", "docs/ROADMAP.md")
        for source in sources
    )


def test_repo_corpus_contains_the_durable_docs():
    corpus = engine_module._load_repo_corpus()
    sources = {doc["source"] for doc in corpus}
    assert "README.md" in sources
    assert "docs/ROADMAP.md" in sources
    assert all(doc["doc_id"].startswith("repo:") for doc in corpus)
    assert all(doc["title"] for doc in corpus)


def test_empty_request_documents_is_an_honest_empty_corpus(engine):
    data = engine.execute("search", {"query": "spring", "documents": []}).result
    assert data["corpus"] == "request"
    assert data["total_documents"] == 0
    assert data["hit_count"] == 0


@pytest.mark.parametrize(
    "parameters, match",
    [
        ({}, "requires string 'query'"),
        ({"query": ""}, "must not be empty"),
        ({"query": 5}, "requires string 'query'"),
        ({"query": "spring", "limit": 0}, r"within \[1, 50\]"),
        ({"query": "spring", "limit": 51}, r"within \[1, 50\]"),
        ({"query": "spring", "limit": 1.5}, "whole number"),
        ({"query": "spring", "limit": "5"}, "must be numeric"),
        ({"query": "spring", "limit": True}, "must be numeric"),
        ({"query": "spring", "documents": "nope"}, "must be an array"),
        ({"query": "spring", "documents": [5]}, "must be an object"),
        ({"query": "spring", "documents": [{"text": "x"}]}, "requires string 'title'"),
        ({"query": "spring", "documents": [{"title": "x"}]}, "requires string 'text'"),
        (
            {"query": "spring", "documents": [{"title": "x" * 501, "text": "x"}]},
            "title exceeds",
        ),
        (
            {"query": "spring", "documents": [{"title": "x", "text": "x" * 200001}]},
            "text exceeds",
        ),
        (
            {
                "query": "spring",
                "documents": [
                    {"doc_id": "same", "title": "a", "text": "spring"},
                    {"doc_id": "same", "title": "b", "text": "spring"},
                ],
            },
            "already exists",
        ),
    ],
)
def test_search_refuses_bad_inputs(engine, parameters, match):
    with pytest.raises(RequestValidationError, match=match):
        engine.execute("search", parameters)


def test_document_limit_is_enforced(engine, monkeypatch):
    monkeypatch.setattr(engine_module, "MAX_DOCUMENTS", 1)
    with pytest.raises(RequestValidationError, match="document limit"):
        engine.execute(
            "search",
            {
                "query": "spring",
                "documents": [
                    {"title": "a", "text": "spring"},
                    {"title": "b", "text": "spring"},
                ],
            },
        )


def test_unknown_operation_refuses_with_capability_code(engine):
    with pytest.raises(CapabilityUnavailableError) as excinfo:
        engine.execute("index_document", {"title": "x", "text": "y"})
    assert excinfo.value.code == "capability_unavailable"


def test_registry_uses_python_fallbacks_without_native(monkeypatch):
    monkeypatch.delenv("TRINITY_NATIVE_EXEC", raising=False)
    monkeypatch.setattr(registry_module.registry, "_engines", {})
    registry_module.bootstrap_engines()

    assert isinstance(registry_module.registry.get("research"), ResearchEngine)
    assert isinstance(registry_module.registry.get("pcb"), PcbEngine)
