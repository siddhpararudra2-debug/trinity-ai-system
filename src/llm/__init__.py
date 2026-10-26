"""LLM client package — thin wrappers that emit structured tool-calls only.

LocalClient is the keyless seam for a self-hosted OpenAI-compatible model
(TRINITY_MODEL_ENDPOINT / TRINITY_MODEL_NAME) — the provider of choice when
the user's own LLM is connected.
"""
from src.llm.base import ModelProvider
from src.llm.local_client import ENDPOINT_ENV, MODEL_ENV, LocalClient

__all__ = ["ENDPOINT_ENV", "MODEL_ENV", "LocalClient", "ModelProvider"]
