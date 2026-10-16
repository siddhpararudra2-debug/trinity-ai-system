"""
Engine Registry (PRD §24).

A single process-wide registry that engines register themselves into
at import time. This is what eventually lets an LLM tool-caller ask
"what can Trinity do?" and get a structured answer instead of a
hardcoded prompt.
"""

from __future__ import annotations

from typing import Any

from src.core.errors import EngineNotFoundError
from src.engines.base import BaseEngine


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
    from src.engines.cad.engine import CADEngine
    from src.engines.math.engine import MathEngine
    from src.engines.native_bridge import NATIVE_ENGINES, NativeBridgeEngine
    from src.engines.scaffold import ScaffoldEngine

    registry.register(MathEngine())
    registry.register(CADEngine())
    bridged = NATIVE_ENGINES if _native_available() else {}
    for name, capabilities in (
        ("pcb", ["inspect", "validate", "generate", "export"]),
        ("firmware", ["create", "build", "test", "compile"]),
        (
            "vision",
            ["image_inspect", "ocr", "document_parse", "geometry_extract"],
        ),
        ("research", ["search"]),
        ("simulation", ["simulate"]),
        ("robotics", ["kinematics", "trajectory_plan"]),
    ):
        if name in bridged:
            registry.register(NativeBridgeEngine(name, NATIVE_ENGINES[name]))
        else:
            registry.register(ScaffoldEngine(name, capabilities))


def _native_available() -> bool:
    from src.engines.native_bridge import native_exe_path

    return native_exe_path() is not None
