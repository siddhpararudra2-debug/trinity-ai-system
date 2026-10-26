"""Keyless OpenAI-compatible client for a self-hosted model endpoint.

Contract: the endpoint (full chat-completions URL) and model name come from
TRINITY_MODEL_ENDPOINT / TRINITY_MODEL_NAME — no API key is required, which
is the shape Ollama / LM Studio / llama.cpp-server / vLLM all speak. With no
endpoint configured, generate/stream/tool_call refuse truthfully with
capability_unavailable; nothing pretends a model is attached. `tool_call`
stays deterministic (no network) — it only reshapes a structured request.
"""

from __future__ import annotations

import json
import os
from collections.abc import Iterator
from typing import Any

from src.core.errors import CapabilityUnavailableError, EngineExecutionError

ENDPOINT_ENV = "TRINITY_MODEL_ENDPOINT"
MODEL_ENV = "TRINITY_MODEL_NAME"


class LocalClient:
    def __init__(
        self,
        endpoint: str | None = None,
        model: str | None = None,
        temperature: float = 0.2,
        top_p: float = 0.9,
        max_tokens: int = 2048,
        api_key: str | None = None,
        timeout: float = 300.0,
    ):
        self.endpoint = os.getenv(ENDPOINT_ENV, "") or (endpoint or "")
        self.model = os.getenv(MODEL_ENV, "") or (model or "local-model")
        self.temperature = temperature
        self.top_p = top_p
        self.max_tokens = max_tokens
        self.timeout = timeout
        # Optional: some self-hosted gateways still demand any non-empty key.
        self.api_key = os.getenv("TRINITY_MODEL_API_KEY", "") or (api_key or "")

    def available(self) -> bool:
        """True only when an endpoint is configured AND reachable is NOT
        claimed here — reachability is a call-time fact."""
        return bool(self.endpoint)

    def _require_endpoint(self) -> None:
        if not self.endpoint:
            raise CapabilityUnavailableError(
                "no local model endpoint configured; set TRINITY_MODEL_ENDPOINT "
                "(OpenAI-compatible chat/completions URL) and optionally "
                "TRINITY_MODEL_NAME",
                details={"provider": "local", "env_var": ENDPOINT_ENV},
            )

    def _post(
        self, body: dict[str, Any], stream: bool
    ) -> Any:
        import httpx

        headers = {"Content-Type": "application/json"}
        if self.api_key:
            headers["Authorization"] = f"Bearer {self.api_key}"
        try:
            return httpx.post(
                self.endpoint,
                json=body,
                headers=headers,
                timeout=self.timeout,
            )
        except Exception as exc:  # noqa: BLE001 — provider boundary
            raise EngineExecutionError(
                f"local model request failed: {exc}",
                details={"provider": "local", "endpoint": self.endpoint},
            ) from exc

    def generate(self, prompt: str, system: str | None = None) -> str:
        self._require_endpoint()
        messages: list[dict[str, Any]] = []
        if system:
            messages.append({"role": "system", "content": system})
        messages.append({"role": "user", "content": prompt})
        response = self._post(
            {
                "model": self.model,
                "messages": messages,
                "temperature": self.temperature,
                "top_p": self.top_p,
                "max_tokens": self.max_tokens,
                "stream": False,
            },
            stream=False,
        )
        try:
            response.raise_for_status()
            data = response.json()
            if "error" in data:
                raise EngineExecutionError(
                    f"local model refused: {data['error']}",
                    details={"provider": "local"},
                )
            return data["choices"][0]["message"]["content"] or ""
        except EngineExecutionError:
            raise
        except (KeyError, IndexError, TypeError, ValueError) as exc:
            raise EngineExecutionError(
                "local model returned an unparseable response",
                details={"provider": "local", "model": self.model},
            ) from exc

    def stream(self, prompt: str, system: str | None = None) -> Iterator[str]:
        """Yield incremental text deltas from the OpenAI-compatible SSE stream."""
        self._require_endpoint()
        messages: list[dict[str, Any]] = []
        if system:
            messages.append({"role": "system", "content": system})
        messages.append({"role": "user", "content": prompt})
        body = {
            "model": self.model,
            "messages": messages,
            "temperature": self.temperature,
            "top_p": self.top_p,
            "max_tokens": self.max_tokens,
            "stream": True,
        }
        headers = {"Content-Type": "application/json"}
        if self.api_key:
            headers["Authorization"] = f"Bearer {self.api_key}"
        try:
            import httpx

            with httpx.stream(
                "POST", self.endpoint, json=body, headers=headers, timeout=self.timeout
            ) as response:
                response.raise_for_status()
                for line in response.iter_lines():
                    if not line.startswith("data:"):
                        continue
                    payload = line[5:].strip()
                    if payload == "[DONE]":
                        break
                    try:
                        event = json.loads(payload)
                    except ValueError:
                        continue  # malformed line: skip, never invent text
                    if "error" in event:
                        raise EngineExecutionError(
                            f"local model stream error: {event['error']}",
                            details={"provider": "local"},
                        )
                    choices = event.get("choices") or []
                    if not choices:
                        continue
                    text = (choices[0].get("delta") or {}).get("content")
                    if text:
                        yield text
        except EngineExecutionError:
            raise
        except Exception as exc:  # noqa: BLE001 — provider boundary
            raise EngineExecutionError(
                f"local model stream failed: {exc}",
                details={"provider": "local", "endpoint": self.endpoint},
            ) from exc

    def tool_call(self, request: dict[str, Any]) -> dict[str, Any]:
        """Return a structured {engine, operation, parameters} dict for POST /api/execute."""
        return {
            "engine": request.get("engine", "cad"),
            "operation": request.get("operation", "generate"),
            "parameters": request.get("parameters", {}),
        }
