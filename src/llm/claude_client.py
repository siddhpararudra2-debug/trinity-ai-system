"""Thin Anthropic wrapper — emits tool-calls for the job manager, never executes engines.

Same contract as OpenAIClient: `tool_call` is deterministic (no network);
`generate`/`stream` need ANTHROPIC_API_KEY and refuse truthfully without it.
"""

from __future__ import annotations

import os
from collections.abc import Iterator
from typing import Any

from src.core.errors import CapabilityUnavailableError, EngineExecutionError
from src.llm.openai_client import _configured


class ClaudeClient:
    def __init__(self, model: str | None = None, temperature: float = 0.2, top_p: float = 0.9):
        cfg = _configured("anthropic")
        self.model = os.getenv("ANTHROPIC_MODEL", model or cfg.get("model_id", "claude-3-5-sonnet-20241022"))
        self.temperature = temperature
        self.top_p = top_p
        self.max_tokens = int(cfg.get("max_tokens", 2048))
        self.api_key = os.getenv("ANTHROPIC_API_KEY", "")

    def _sdk(self):
        if not self.api_key:
            raise CapabilityUnavailableError(
                "ANTHROPIC_API_KEY is not set; model reasoning is unavailable",
                details={"provider": "anthropic", "env_var": "ANTHROPIC_API_KEY"},
            )
        try:
            from anthropic import Anthropic
        except ImportError as exc:
            raise CapabilityUnavailableError(
                "anthropic SDK is not installed",
                details={"provider": "anthropic", "hint": "pip install anthropic"},
            ) from exc
        return Anthropic(api_key=self.api_key)

    def generate(self, prompt: str) -> str:
        client = self._sdk()
        try:
            response = client.messages.create(
                model=self.model,
                max_tokens=self.max_tokens,
                messages=[{"role": "user", "content": prompt}],
                temperature=self.temperature,
                top_p=self.top_p,
            )
        except CapabilityUnavailableError:
            raise
        except Exception as exc:
            raise EngineExecutionError(
                f"Anthropic request failed: {exc}",
                details={"provider": "anthropic", "model": self.model},
            ) from exc
        try:
            blocks = response.content
            return "".join(
                getattr(block, "text", "") for block in blocks if getattr(block, "type", "") == "text"
            )
        except (AttributeError, TypeError) as exc:
            raise EngineExecutionError(
                "Anthropic returned an unparseable response",
                details={"provider": "anthropic"},
            ) from exc

    def stream(self, prompt: str) -> Iterator[str]:
        client = self._sdk()
        try:
            events = client.messages.create(
                model=self.model,
                max_tokens=self.max_tokens,
                messages=[{"role": "user", "content": prompt}],
                temperature=self.temperature,
                top_p=self.top_p,
                stream=True,
            )
        except CapabilityUnavailableError:
            raise
        except Exception as exc:
            raise EngineExecutionError(
                f"Anthropic stream failed: {exc}",
                details={"provider": "anthropic", "model": self.model},
            ) from exc
        for event in events:
            text = getattr(getattr(event, "delta", None), "text", None)
            if text:
                yield text

    def tool_call(self, request: dict[str, Any]) -> dict[str, Any]:
        return {
            "engine": request.get("engine", "cad"),
            "operation": request.get("operation", "generate"),
            "parameters": request.get("parameters", {}),
        }
