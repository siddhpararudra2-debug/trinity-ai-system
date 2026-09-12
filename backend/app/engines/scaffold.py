"""Truthful placeholders for planned engines; they do not simulate execution."""
from __future__ import annotations
from typing import Any
from app.core.errors import CapabilityUnavailableError
from app.engines.base import BaseEngine, EngineResult

class ScaffoldEngine(BaseEngine):
    def __init__(self, name: str, capabilities: list[str]) -> None:
        self.name, self.capabilities = name, capabilities
        self.version = "0.1"

    def describe(self) -> dict[str, Any]:
        return {**super().describe(), "status": "scaffolded"}

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        raise CapabilityUnavailableError(
            f"The {self.name} engine is scaffolded and cannot execute '{operation}' yet",
            details={"engine": self.name, "operation": operation, "status": "scaffolded"},
        )
