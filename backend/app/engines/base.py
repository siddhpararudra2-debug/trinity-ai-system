"""
Engine base interface (PRD §16, generalized beyond just CAD).

Every engine — math, cad, pcb, firmware, vision, research — implements
this contract so the router / workflow engine can treat them
interchangeably, and so a future LLM tool-caller can discover and
invoke them uniformly (PRD §24, §31).
"""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass, field
from typing import Any, Protocol


@dataclass
class ArtifactRef:
    artifact_id: str
    type: str
    path: str
    size_bytes: int
    checksum: str


@dataclass
class ValidationResult:
    status: str  # GENERATED | VALIDATED | VERIFIED | FAILED
    checks: dict[str, Any] = field(default_factory=dict)

    @property
    def passed(self) -> bool:
        return self.status in ("VALIDATED", "VERIFIED")


@dataclass
class EngineResult:
    """Internal result type; converted to ToolResponse at the API boundary.

    `artifacts` holds already-persisted ArtifactRefs (rare — most engines
    don't produce files). `pending_artifacts` holds (temp_path, type) pairs
    for files an engine wrote to a scratch location; the JobManager is
    responsible for handing these to the ArtifactManager so that
    storage/artifacts/ has a single writer (PRD §9).
    """

    success: bool
    engine: str
    operation: str
    result: dict[str, Any] = field(default_factory=dict)
    artifacts: list[ArtifactRef] = field(default_factory=list)
    pending_artifacts: list[tuple[str, str]] = field(default_factory=list)
    validation: ValidationResult | None = None
    errors: list[dict[str, Any]] = field(default_factory=list)


class Engine(Protocol):
    """Structural interface every engine must satisfy."""

    name: str
    version: str
    capabilities: Sequence[str]

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult: ...

    def describe(self) -> dict[str, Any]: ...


class BaseEngine:
    """Convenience base class implementing the boilerplate of `Engine`."""

    name: str = "base"
    version: str = "0.0"
    capabilities: Sequence[str] = ()

    def describe(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "version": self.version,
            "capabilities": list(self.capabilities),
        }

    def execute(
        self, operation: str, parameters: dict[str, Any]
    ) -> EngineResult:  # pragma: no cover
        raise NotImplementedError
