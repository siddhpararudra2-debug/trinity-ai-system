"""Engine Registry (PRD §24).

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
    from src.engines.pcb import PcbEngine
    from src.engines.firmware import FirmwareEngine
    from src.engines.verification import VerificationEngine
    from src.engines.crosscheck import CrossCheckEngine
    from src.engines.research.engine import ResearchEngine
    from src.engines.robotics.engine import RoboticsEngine
    from src.engines.simulation.engine import SimulationEngine
    from src.engines.vision.engine import VisionEngine

    registry.register(MathEngine())
    registry.register(CADEngine())
    bridged = NATIVE_ENGINES if _native_available() else {}
    for name, capabilities in (
        ("pcb", ["inspect", "validate", "generate", "export"]),
        ("firmware", ["create", "build", "test", "compile"]),
        ("verification", ["verify"]),
        (
            "vision",
            ["image_inspect", "ocr", "document_parse", "geometry_extract"],
        ),
        ("research", ["search"]),
        ("simulation", ["simulate"]),
        ("robotics", ["kinematics", "trajectory_plan"]),
        ("crosscheck", ["cross_check"]),
    ):
        if name in bridged:
            registry.register(NativeBridgeEngine(name, NATIVE_ENGINES[name]))
        elif name == "pcb":
            registry.register(PcbEngine())
        elif name == "firmware":
            registry.register(FirmwareEngine())
        elif name == "verification":
            registry.register(VerificationEngine())
        elif name == "crosscheck":
            registry.register(CrossCheckEngine())
        elif name == "simulation":
            # M18.1: a real Python fallback where the native daemon cannot
            # run (Linux bundle, native-less CI). Advertises exactly the
            # ops that engine implements and tests.
            registry.register(SimulationEngine())
        elif name == "robotics":
            # M18.2: same rule — only the advertised FK/trajectory ops.
            registry.register(RoboticsEngine())
        elif name == "vision":
            # M18.3: image_inspect + document_parse only; OCR and contour
            # extraction stay native-only and unadvertised.
            registry.register(VisionEngine())
        elif name == "research":
            # M18.4: search over request documents or the bundled repo docs.
            registry.register(ResearchEngine())
        else:
            # This branch is kept for safety; all named engines above
            # should have been handled before we reach here.
            from src.engines.scaffold import ScaffoldEngine
            registry.register(ScaffoldEngine(name, capabilities))


def _native_available() -> bool:
    from src.engines.native_bridge import native_exe_path

    return native_exe_path() is not None