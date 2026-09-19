"""Thin Anthropic wrapper — emits tool-calls for the job manager, never executes engines."""
from __future__ import annotations

import os
from typing import Any


class ClaudeClient:
    def __init__(self, model: str = "claude-3-5-sonnet-20241022", temperature: float = 0.2, top_p: float = 0.9):
        self.model = os.getenv("ANTHROPIC_MODEL", model)
        self.temperature = temperature
        self.top_p = top_p
        self.api_key = os.getenv("ANTHROPIC_API_KEY", "")

    def generate(self, prompt: str) -> str:
        raise NotImplementedError("Wire to anthropic SDK with config/model_config.yaml; keep deterministic path by default.")

    def stream(self, prompt: str):
        raise NotImplementedError

    def tool_call(self, request: dict[str, Any]) -> dict[str, Any]:
        return {
            "engine": request.get("engine", "cad"),
            "operation": request.get("operation", "generate"),
            "parameters": request.get("parameters", {}),
        }
