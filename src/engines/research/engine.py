"""Research engine (M18.4): Python search fallback.

The native engine searches an in-memory index populated by
``index_document``. On the Linux bundle and native-less CI there is no
native daemon, so this engine gives the advertised ``search`` op a real
corpus and the same result shape:

* an optional ``documents`` array (``title``/``text``, optional
  ``source``/``doc_id``) searches exactly the caller's material;
* without it, the repository's Markdown docs are indexed lazily
  (README, PROCESS, work, opencodeworks, MILESTONES, docs/*.md), and the
  result says ``corpus: "repo_docs"``.

Tokenization, stopwords, scoring, ranking, and snippet extraction mirror
the native engine so a DAG sees the same hit fields. ``index_document``,
``summarize_results``, and index export stay native-only and
unadvertised.
"""

from __future__ import annotations

import math
import uuid
from pathlib import Path
from typing import Any

from src.core.errors import CapabilityUnavailableError, RequestValidationError
from src.engines.base import BaseEngine, EngineResult, ValidationResult

MAX_DOCUMENTS = 10_000
MAX_TITLE_CHARS = 500
MAX_TEXT_CHARS = 200_000
DEFAULT_LIMIT = 5
MAX_LIMIT = 50
SNIPPET_LENGTH = 120
SNIPPET_LEAD = 30

STOPWORDS = frozenset(
    {
        "the",
        "a",
        "an",
        "and",
        "or",
        "of",
        "to",
        "in",
        "on",
        "for",
        "with",
        "is",
        "are",
        "was",
        "were",
        "be",
        "by",
        "at",
        "as",
        "it",
        "its",
        "this",
        "that",
        "from",
        "into",
        "per",
        "via",
        "not",
        "no",
        "we",
        "you",
        "they",
    }
)

_REPO_CORPUS: list[dict[str, Any]] | None = None


def _round4(value: float) -> float:
    # Native uses std::round (halves away from zero); scores are >= 0.
    return math.floor(value * 10_000.0 + 0.5) / 10_000.0


def _tokenize(text: str) -> list[str]:
    tokens: list[str] = []
    current: list[str] = []
    for char in text.lower():
        if char.isalnum():
            current.append(char)
        else:
            token = "".join(current)
            if len(token) >= 2 and token not in STOPWORDS:
                tokens.append(token)
            current = []
    token = "".join(current)
    if len(token) >= 2 and token not in STOPWORDS:
        tokens.append(token)
    return tokens


def _required_string(parameters: dict[str, Any], key: str, operation: str) -> str:
    value = parameters.get(key)
    if not isinstance(value, str):
        raise RequestValidationError(
            f"Operation '{operation}' requires string '{key}'",
            details={"operation": operation, "param": key},
        )
    if not value:
        raise RequestValidationError(
            f"Parameter '{key}' must not be empty", details={"param": key}
        )
    return value


def _bounded_int(
    parameters: dict[str, Any], key: str, fallback: int, lo: int, hi: int
) -> int:
    value = parameters.get(key)
    if value is None:
        return fallback
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise RequestValidationError(
            f"Parameter '{key}' must be numeric", details={"param": key}
        )
    if isinstance(value, float) and not value.is_integer():
        raise RequestValidationError(
            f"Parameter '{key}' must be a whole number", details={"param": key}
        )
    number = int(value)
    if number < lo or number > hi:
        raise RequestValidationError(
            f"Parameter '{key}' must be within [{lo}, {hi}]",
            details={"param": key, "value": number},
        )
    return number


def _make_snippet(text: str, matched: list[str]) -> str:
    lowered = text.lower()
    earliest = -1
    for term in matched:
        position = lowered.find(term)
        if position != -1 and (earliest == -1 or position < earliest):
            earliest = position
    if earliest == -1:
        return text[:100] + ("..." if len(text) > 100 else "")
    start = earliest - SNIPPET_LEAD if earliest > SNIPPET_LEAD else 0
    snippet = ("..." if start > 0 else "") + text[start : start + SNIPPET_LENGTH]
    if start + SNIPPET_LENGTH < len(text):
        snippet += "..."
    return snippet


def _repo_root() -> Path:
    # engine.py -> research -> engines -> src -> repo root (or _MEIPASS
    # in the frozen bundle, where `src` is carried wholesale).
    return Path(__file__).resolve().parents[3]


def _repo_doc_paths(root: Path) -> list[Path]:
    paths = [
        root / name
        for name in ("README.md", "work.md", "opencodeworks.md")
        if (root / name).is_file()
    ]
    docs = root / "docs"
    if docs.is_dir():
        paths.extend(sorted(docs.glob("*.md")))
    return paths


def _title_of(text: str, fallback: str) -> str:
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("# "):
            return stripped[2:].strip()[:MAX_TITLE_CHARS]
    return fallback[:MAX_TITLE_CHARS]


def _load_repo_corpus() -> list[dict[str, Any]]:
    global _REPO_CORPUS
    if _REPO_CORPUS is not None:
        return _REPO_CORPUS
    root = _repo_root()
    corpus: list[dict[str, Any]] = []
    for path in _repo_doc_paths(root):
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        relative = path.relative_to(root).as_posix()
        corpus.append(
            {
                "doc_id": f"repo:{relative}",
                "title": _title_of(text, path.stem),
                "source": relative,
                "text": text[:MAX_TEXT_CHARS],
            }
        )
    _REPO_CORPUS = corpus
    return corpus


def _request_corpus(raw: Any) -> list[dict[str, Any]]:
    if not isinstance(raw, list):
        raise RequestValidationError(
            "documents must be an array of {title, text} objects",
            details={"param": "documents"},
        )
    if len(raw) > MAX_DOCUMENTS:
        raise RequestValidationError(
            f"documents exceeds the {MAX_DOCUMENTS}-document limit",
            details={"limit": MAX_DOCUMENTS},
        )
    corpus: list[dict[str, Any]] = []
    seen: set[str] = set()
    for index, entry in enumerate(raw):
        if not isinstance(entry, dict):
            raise RequestValidationError(
                f"documents[{index}] must be an object",
                details={"param": "documents"},
            )
        title = _required_string(entry, "title", "search")
        text = _required_string(entry, "text", "search")
        if len(title) > MAX_TITLE_CHARS:
            raise RequestValidationError(
                f"title exceeds {MAX_TITLE_CHARS} characters",
                details={"param": "title"},
            )
        if len(text) > MAX_TEXT_CHARS:
            raise RequestValidationError(
                f"text exceeds {MAX_TEXT_CHARS} characters",
                details={"param": "text"},
            )
        doc_id = entry.get("doc_id") or uuid.uuid4().hex
        if not isinstance(doc_id, str):
            raise RequestValidationError(
                "doc_id must be a string", details={"param": "doc_id"}
            )
        if doc_id in seen:
            raise RequestValidationError(
                f"doc_id '{doc_id}' already exists", details={"doc_id": doc_id}
            )
        seen.add(doc_id)
        source = entry.get("source", "")
        corpus.append(
            {
                "doc_id": doc_id,
                "title": title,
                "source": source if isinstance(source, str) else "",
                "text": text,
            }
        )
    return corpus


def _search_hits(
    query: str, corpus: list[dict[str, Any]], limit: int
) -> tuple[list[str], list[dict[str, Any]]]:
    terms: list[str] = []
    for token in _tokenize(query):
        if token not in terms:
            terms.append(token)
    if not terms:
        return [], []

    scored: list[dict[str, Any]] = []
    for document in corpus:
        title_tokens = set(_tokenize(document["title"]))
        body_tokens = _tokenize(document["text"])
        frequencies: dict[str, int] = {}
        for token in body_tokens:
            frequencies[token] = frequencies.get(token, 0) + 1
        matched: list[str] = []
        occurrences = 0
        matched_in_title = 0
        for term in terms:
            body_count = frequencies.get(term, 0)
            title_count = 1 if term in title_tokens else 0
            if body_count > 0 or title_count > 0:
                matched.append(term)
                occurrences += body_count + title_count
                if title_count:
                    matched_in_title += 1
        if not matched:
            continue
        score = _round4(
            len(matched) / len(terms) + 0.25 * matched_in_title / len(terms)
        )
        scored.append(
            {
                "document": document,
                "score": score,
                "matched": matched,
                "occurrences": occurrences,
            }
        )

    scored.sort(
        key=lambda entry: (
            -entry["score"],
            -entry["occurrences"],
            entry["document"]["doc_id"],
        )
    )
    hits: list[dict[str, Any]] = []
    for rank, entry in enumerate(scored[:limit], start=1):
        document = entry["document"]
        hits.append(
            {
                "rank": rank,
                "doc_id": document["doc_id"],
                "title": document["title"],
                "source": document["source"],
                "score": entry["score"],
                "matched_terms": entry["matched"],
                "occurrences": entry["occurrences"],
                "snippet": _make_snippet(document["text"], entry["matched"]),
            }
        )
    return terms, hits


class ResearchEngine(BaseEngine):
    """Python implementation of the advertised research op."""

    name = "research"
    version = "0.1"
    capabilities = ("search",)

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        if operation != "search":
            raise CapabilityUnavailableError(
                f"The research engine has no Python implementation for "
                f"'{operation}' yet",
                details={
                    "engine": self.name,
                    "operation": operation,
                    "status": "python",
                },
            )
        return self._search(parameters)

    def _search(self, parameters: dict[str, Any]) -> EngineResult:
        query = _required_string(parameters, "query", "search")
        limit = _bounded_int(parameters, "limit", DEFAULT_LIMIT, 1, MAX_LIMIT)
        raw_documents = parameters.get("documents")
        if raw_documents is None:
            corpus = _load_repo_corpus()
            corpus_name = "repo_docs"
        else:
            corpus = _request_corpus(raw_documents)
            corpus_name = "request"

        terms, hits = _search_hits(query, corpus, limit)
        data = {
            "query": query,
            "query_terms": terms,
            "corpus": corpus_name,
            "total_documents": len(corpus),
            "hit_count": len(hits),
            "hits": hits,
        }
        return EngineResult(
            success=True,
            engine=self.name,
            operation="search",
            result=data,
            validation=ValidationResult(
                status="VALIDATED",
                checks={
                    "engine": "research",
                    "operation": "search",
                    "message": "Search completed successfully",
                },
            ),
        )
