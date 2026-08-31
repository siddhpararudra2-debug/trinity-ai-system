"""Optional async LLM adapter for firmware generation.

The deterministic generator remains the safe default. An LLM is used only when
explicitly enabled and configured through TRINITY-prefixed environment variables.
"""
from __future__ import annotations

import json
import os
from dataclasses import dataclass
from typing import Any

import httpx


@dataclass(frozen=True)
class FirmwareLLMConfig:
    enabled: bool
    endpoint: str
    api_key: str
    model: str
    temperature: float
    max_tokens: int
    timeout_seconds: float

    @classmethod
    def from_env(cls) -> FirmwareLLMConfig:
        return cls(
            enabled=os.getenv("TRINITY_FIRMWARE_LLM_ENABLED", "0") == "1",
            endpoint=os.getenv("TRINITY_FIRMWARE_LLM_ENDPOINT", "https://api.openai.com/v1/chat/completions"),
            api_key=os.getenv("TRINITY_FIRMWARE_LLM_API_KEY", os.getenv("OPENAI_API_KEY", "")),
            model=os.getenv("TRINITY_FIRMWARE_LLM_MODEL", "gpt-4o-mini"),
            temperature=float(os.getenv("TRINITY_FIRMWARE_LLM_TEMPERATURE", "0.1")),
            max_tokens=max(256, int(os.getenv("TRINITY_FIRMWARE_LLM_MAX_TOKENS", "6000"))),
            timeout_seconds=max(5.0, float(os.getenv("TRINITY_FIRMWARE_LLM_TIMEOUT", "45"))),
        )


class FirmwareLLMClient:
    def __init__(self, config: FirmwareLLMConfig | None = None) -> None:
        self.config = config or FirmwareLLMConfig.from_env()

    async def generate(self, system_prompt: str, user_prompt: str) -> dict[str, Any] | None:
        if not self.config.enabled or not self.config.api_key:
            return None
        payload = {
            "model": self.config.model,
            "temperature": self.config.temperature,
            "max_tokens": self.config.max_tokens,
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_prompt},
            ],
        }
        headers = {"Authorization": f"Bearer {self.config.api_key}", "Content-Type": "application/json"}
        async with httpx.AsyncClient(timeout=self.config.timeout_seconds) as client:
            response = await client.post(self.config.endpoint, headers=headers, json=payload)
            response.raise_for_status()
            body = response.json()
        content = body.get("choices", [{}])[0].get("message", {}).get("content", "")
        if not isinstance(content, str) or not content.strip():
            raise ValueError("LLM returned no firmware content")
        return parse_structured_response(content)


def parse_structured_response(content: str) -> dict[str, Any]:
    """Parse JSON or marker-delimited model output without executing model text."""
    stripped = content.strip()
    try:
        parsed = json.loads(stripped)
        if isinstance(parsed, dict) and isinstance(parsed.get("files"), dict):
            return parsed
    except json.JSONDecodeError:
        pass

    files: dict[str, str] = {}
    marker = None
    filename = None
    buffer: list[str] = []
    dependencies: list[str] = []
    for line in stripped.splitlines():
        if line.startswith("FILE:"):
            if filename is not None:
                files[filename] = "\n".join(buffer).strip() + "\n"
            filename = line.removeprefix("FILE:").strip()
            buffer = []
            marker = "file"
        elif line.startswith("DEPENDENCIES:"):
            dependencies = [item.strip() for item in line.removeprefix("DEPENDENCIES:").split(",") if item.strip()]
            marker = "dependencies"
        elif marker == "file" and filename is not None:
            buffer.append(line)
    if filename is not None:
        files[filename] = "\n".join(buffer).strip() + "\n"
    if not files:
        raise ValueError("LLM output did not contain structured FILE markers or JSON files")
    return {"files": files, "dependencies": dependencies}
