"""General AI / Code — LLM fallback and orchestration assistance layer."""
from __future__ import annotations

from typing import Any

from app.engines.base import BaseEngine
from app.llm.client import TrinityLLMClient

SYSTEM_PROMPT = """You are Trinity AI, a unified engineering and research assistant.
You help with general questions, coding tasks, and explaining how to use Trinity's
specialist engines (math, quantum, CAD, PCB, literature, vision, firmware).
Prefer concise, accurate answers. When a task belongs to a specialist engine, say which
engine the user should invoke and why. Do not invent hardware specs or claim you executed
domain engines you did not run."""


class GeneralAIEngine(BaseEngine):
    engine_id = "general_ai"

    def __init__(self) -> None:
        self._llm = TrinityLLMClient()

    async def process(
        self,
        query: str,
        history: list[dict[str, str]] | None = None,
        **_: Any,
    ) -> dict[str, Any]:
        answer = await self._llm.chat(SYSTEM_PROMPT, query, history=history)
        if answer:
            return {
                "engine": self.engine_id,
                "query": query,
                "content": answer,
                "source": "llm",
                "rendering_hint": "markdown",
            }
        return {
            "engine": self.engine_id,
            "query": query,
            "content": self._fallback_help(query),
            "source": "fallback",
            "rendering_hint": "markdown",
        }

    @staticmethod
    def _fallback_help(query: str) -> str:
        return (
            "Trinity General AI is running in offline mode (no LLM API key configured).\n\n"
            f"Your request: {query[:500]}\n\n"
            "Set `TRINITY_OPENAI_API_KEY` or `TRINITY_LLM_ENABLED=1` with a valid key to "
            "enable natural-language assistance. Specialist engines (math, quantum, CAD, PCB, "
            "literature, vision, firmware) remain available without an LLM."
        )
