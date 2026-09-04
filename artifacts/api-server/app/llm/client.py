"""Shared async LLM client for Trinity general assistance and optional routing."""
from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Any

import httpx

from app.config import get_settings


@dataclass(frozen=True)
class TrinityLLMConfig:
    enabled: bool
    endpoint: str
    api_key: str
    model: str
    temperature: float
    max_tokens: int
    timeout_seconds: float

    @classmethod
    def from_env(cls) -> "TrinityLLMConfig":
        settings = get_settings()
        api_key = (settings.openai_api_key or os.getenv("OPENAI_API_KEY") or "").strip()
        return cls(
            enabled=os.getenv("TRINITY_LLM_ENABLED", "1" if api_key else "0") == "1",
            endpoint=os.getenv("TRINITY_LLM_ENDPOINT", "https://api.openai.com/v1/chat/completions"),
            api_key=api_key,
            model=os.getenv("TRINITY_LLM_MODEL", "gpt-4o-mini"),
            temperature=float(os.getenv("TRINITY_LLM_TEMPERATURE", "0.3")),
            max_tokens=max(256, int(os.getenv("TRINITY_LLM_MAX_TOKENS", "4096"))),
            timeout_seconds=max(5.0, float(os.getenv("TRINITY_LLM_TIMEOUT", "60"))),
        )


class TrinityLLMClient:
    def __init__(self, config: TrinityLLMConfig | None = None) -> None:
        self.config = config or TrinityLLMConfig.from_env()

    @property
    def available(self) -> bool:
        return self.config.enabled and bool(self.config.api_key)

    async def chat(
        self,
        system_prompt: str,
        user_prompt: str,
        history: list[dict[str, str]] | None = None,
    ) -> str | None:
        if not self.available:
            return None
        messages: list[dict[str, str]] = [{"role": "system", "content": system_prompt}]
        for item in history or []:
            role = item.get("role", "")
            content = item.get("content", "")
            if role in {"user", "assistant"} and content:
                messages.append({"role": role, "content": content})
        messages.append({"role": "user", "content": user_prompt})
        payload: dict[str, Any] = {
            "model": self.config.model,
            "temperature": self.config.temperature,
            "max_tokens": self.config.max_tokens,
            "messages": messages,
        }
        headers = {
            "Authorization": f"Bearer {self.config.api_key}",
            "Content-Type": "application/json",
        }
        async with httpx.AsyncClient(timeout=self.config.timeout_seconds) as client:
            response = await client.post(self.config.endpoint, headers=headers, json=payload)
            response.raise_for_status()
            body = response.json()
        content = body.get("choices", [{}])[0].get("message", {}).get("content", "")
        return content.strip() if isinstance(content, str) and content.strip() else None
