"""
Engine Registry (PRD §24).

A single process-wide registry that engines register themselves into
at import time. This is what eventually lets an LLM tool-caller ask
"what can Trinity do?" and get a structured answer instead of a
hardcoded prompt.
"""

from __future__ import annotations

from typing import Any

from app.core.errors import EngineNotFoundError
from app.engines.base import BaseEngine


class EngineRegistry:
    def __init__(self) -> None:
        self._engines: dict[str, BaseEngine] = {}

    def register(self, engine: BaseEngine) -> None:
        self._engines[engine.name] = engine

    def get(self, name: str) -> BaseEngine:
        try:
            return self._engines[name]
        except KeyError:
            raise EngineNotFoundError(
                f"No engine registered under name '{name}'",
                details={"available": sorted(self._engines)},
            )

    def has(self, name: str) -> bool:
        return name in self._engines

    def list(self) -> list[dict[str, Any]]:
        return [e.describe() for e in self._engines.values()]


registry = EngineRegistry()


def bootstrap_engines() -> None:
    """Import + register every built-in engine. Called once at startup."""
    from app.engines.cad.engine import CADEngine
    from app.engines.math.engine import MathEngine
    from app.engines.scaffold import ScaffoldEngine

    registry.register(MathEngine())
    registry.register(CADEngine())
    registry.register(
        ScaffoldEngine("pcb", ["inspect", "validate", "generate", "export"])
    )
    registry.register(
        ScaffoldEngine("firmware", ["create", "build", "test", "compile"])
    )
    registry.register(
        ScaffoldEngine(
            "vision", ["image_inspect", "ocr", "document_parse", "geometry_extract"]
        )
    )
    registry.register(ScaffoldEngine("research", ["search"]))
    registry.register(ScaffoldEngine("simulation", ["simulate"]))
    registry.register(ScaffoldEngine("robotics", ["kinematics", "trajectory_plan"]))
