"""LLM client package — thin wrappers that emit structured tool-calls only.

LocalClient is the keyless seam for a self-hosted OpenAI-compatible model
(TRINITY_MODEL_ENDPOINT / TRINITY_MODEL_NAME) — the provider of choice when
the user's own LLM is connected.

The LLM adapter (Item 8) marks every call with "LLM-LINK:" and supports
mock mode via LLM_MOCK=1 for development without a real model endpoint.

From this package you can import:
- LLMAdapter, get_adapter, chat, chat_stream from src.llm.adapters
- LocalClient, ENDPOINT_ENV, MODEL_ENV from src.llm.local_client
- ModelProvider from src.llm.base
"""

from src.llm.base import ModelProvider
from src.llm.local_client import ENDPOINT_ENV, MODEL_ENV, LocalClient
from src.llm.adapters import LLMAdapter, get_adapter, chat, chat_stream

__all__ = [
    "ENDPOINT_ENV",
    "MODEL_ENV",
    "LLMAdapter",
    "LocalClient",
    "ModelProvider",
    "chat",
    "chat_stream",
    "get_adapter",
]
