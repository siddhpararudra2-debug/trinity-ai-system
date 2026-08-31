"""Shared strategy contract for TRINITY specialist engines."""
from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any


class BaseEngine(ABC):
    """Minimal contract implemented by every specialist engine.

    Engines remain deterministic and local by default, while the orchestrator can
    treat every domain strategy uniformly.
    """

    engine_id: str

    @abstractmethod
    async def process(self, *args: Any, **kwargs: Any) -> dict[str, Any]:
        """Process a validated domain request and return structured data."""

    def validate_payload(self, payload: dict[str, Any]) -> dict[str, Any]:
        """Validate and normalize a payload before processing.

        Individual engines may override this method for stricter domain rules.
        """
        if not isinstance(payload, dict):
            raise TypeError("Engine payload must be an object")
        return payload

    def format_output(self, result: dict[str, Any]) -> dict[str, Any]:
        """Ensure every result advertises its originating engine."""
        if not isinstance(result, dict):
            raise TypeError("Engine result must be an object")
        return {**result, "engine": result.get("engine", self.engine_id)}
