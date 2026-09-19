"""Researcher role — wraps ScaffoldEngine('research', ['search'])."""
from __future__ import annotations

from typing import Any


class ResearcherAgent:
    engine = "research"
    operation = "search"

    def to_toolcall(self, query: str) -> dict[str, Any]:
        return {"engine": self.engine, "operation": self.operation, "parameters": {"query": query}}
