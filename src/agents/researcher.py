"""Researcher role — search + summarize through the agent loop.

`to_toolcall` remains the deterministic single-shot mapping; `run` is the
autonomous multi-step loop (see src/agents/loop.py): search the scaffold
engine, inspect the observation, then finish with a grounded summary.
"""
from __future__ import annotations

from collections.abc import Callable
from typing import Any

from src.agents.loop import AGENT_SYSTEM_PROMPT, run_agent_loop

RESEARCHER_SYSTEM = AGENT_SYSTEM_PROMPT + (
    " You are the RESEARCHER agent: use the research engine's search "
    "operation to gather material, then finish with a summary grounded in "
    "the returned observations only."
)


class ResearcherAgent:
    engine = "research"
    operation = "search"

    def to_toolcall(self, query: str) -> dict[str, Any]:
        return {"engine": self.engine, "operation": self.operation, "parameters": {"query": query}}

    def run(
        self,
        query: str,
        llm: Any,
        max_steps: int = 8,
        executor: Callable[..., dict[str, Any]] | None = None,
    ) -> dict[str, Any]:
        return run_agent_loop(query, RESEARCHER_SYSTEM, llm, max_steps=max_steps, executor=executor)
