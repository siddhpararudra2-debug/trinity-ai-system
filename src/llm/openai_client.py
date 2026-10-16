"""Thin OpenAI wrapper — emits tool-calls for the job manager, never executes engines.

`tool_call` stays deterministic (no network) by design: it only reshapes a
structured request. `generate`/`stream` need OPENAI_API_KEY; without it they
refuse with capability_unavailable instead of faking a model response.
Model params come from config/model_config.yaml with constructor fallbacks.
"""

from __future__ import annotations

import os
from collections.abc import Iterator
from typing import Any

from src.core.errors import CapabilityUnavailableError, EngineExecutionError
from src.core.model_config import load_model_config


def _configured(section: str) -> dict[str, Any]:
    try:
        providers = load_model_config().get("providers", {})
        if isinstance(providers, dict):
            section_cfg = providers.get(section, {})
            if isinstance(section_cfg, dict):
                return section_cfg
    except Exception:  # noqa: BLE001 — config is best-effort; constructor wins
        pass
    return {}


class OpenAIClient:
    def __init__(self, model: str | None = None, temperature: float = 0.2, top_p: float = 0.9):
        cfg = _configured("openai")
        self.model = os.getenv("OPENAI_MODEL", model or cfg.get("model_id", "gpt-4o-mini"))
        self.temperature = temperature
        self.top_p = top_p
        self.max_tokens = int(cfg.get("max_tokens", 2048))
        self.api_key = os.getenv("OPENAI_API_KEY", "")

    def _sdk(self):
        if not self.api_key:
            raise CapabilityUnavailableError(
                "OPENAI_API_KEY is not set; model reasoning is unavailable",
                details={"provider": "openai", "env_var": "OPENAI_API_KEY"},
            )
        try:
            from openai import OpenAI
        except ImportError as exc:
            raise CapabilityUnavailableError(
                "openai SDK is not installed",
                details={"provider": "openai", "hint": "pip install openai"},
            ) from exc
        return OpenAI(api_key=self.api_key)

    def generate(self, prompt: str) -> str:
        client = self._sdk()
        try:
            response = client.chat.completions.create(
                model=self.model,
                messages=[{"role": "user", "content": prompt}],
                temperature=self.temperature,
                top_p=self.top_p,
                max_tokens=self.max_tokens,
            )
        except CapabilityUnavailableError:
            raise
        except Exception as exc:
            raise EngineExecutionError(
                f"OpenAI request failed: {exc}",
                details={"provider": "openai", "model": self.model},
            ) from exc
        try:
            return response.choices[0].message.content or ""
        except (AttributeError, IndexError, TypeError) as exc:
            raise EngineExecutionError(
                "OpenAI returned an unparseable response",
                details={"provider": "openai"},
            ) from exc

    def stream(self, prompt: str) -> Iterator[str]:
        client = self._sdk()
        try:
            chunks = client.chat.completions.create(
                model=self.model,
                messages=[{"role": "user", "content": prompt}],
                temperature=self.temperature,
                top_p=self.top_p,
                max_tokens=self.max_tokens,
                stream=True,
            )
        except CapabilityUnavailableError:
            raise
        except Exception as exc:
            raise EngineExecutionError(
                f"OpenAI stream failed: {exc}",
                details={"provider": "openai", "model": self.model},
            ) from exc
        for chunk in chunks:
            try:
                text = chunk.choices[0].delta.content
            except (AttributeError, IndexError, TypeError):
                continue
            if text:
                yield text

    def tool_call(self, request: dict[str, Any]) -> dict[str, Any]:
        """Return a structured {engine, operation, parameters} dict for POST /api/execute."""
        return {
            "engine": request.get("engine", "cad"),
            "operation": request.get("operation", "generate"),
            "parameters": request.get("parameters", {}),
        }
