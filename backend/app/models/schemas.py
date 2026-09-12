"""
Public API schemas.

Pydantic is used at the edge (PRD section 7); internals prefer
dataclasses. Every tool response follows the common envelope from
PRD section 25.
"""

from __future__ import annotations

from typing import Any, Literal

from pydantic import BaseModel, Field

JobStatus = Literal["queued", "running", "completed", "failed", "cancelled"]
ValidationLevel = Literal["GENERATED", "VALIDATED", "VERIFIED", "FAILED"]


# ---------------------------------------------------------------- common ---


class ArtifactOut(BaseModel):
    artifact_id: str
    type: str
    path: str
    size_bytes: int
    checksum: str


class ValidationOut(BaseModel):
    status: ValidationLevel
    checks: dict[str, Any] = Field(default_factory=dict)


class ToolResponse(BaseModel):
    """The common response envelope every engine call returns (PRD §25)."""

    success: bool
    engine: str
    operation: str
    result: dict[str, Any] = Field(default_factory=dict)
    artifacts: list[ArtifactOut] = Field(default_factory=list)
    validation: ValidationOut | None = None
    errors: list[dict[str, Any]] = Field(default_factory=list)
    job_id: str


class JobOut(BaseModel):
    job_id: str
    engine: str
    operation: str
    status: JobStatus
    progress: float
    created_at: str
    updated_at: str
    result: dict[str, Any] | None = None
    error: dict[str, Any] | None = None


class EngineCapability(BaseModel):
    name: str
    version: str
    capabilities: list[str]


# ----------------------------------------------------------------- math ---


class MathSolveRequest(BaseModel):
    expression: str = Field(
        ..., description="Equation or expression, e.g. 'x**2 - 4 = 0'"
    )
    variables: dict[str, float] = Field(
        default_factory=dict, description="Known variable substitutions"
    )
    solve_for: str | None = Field(
        None, description="Symbol to solve for; inferred if omitted"
    )
    units: dict[str, str] = Field(
        default_factory=dict, description="Optional unit tags per variable"
    )


# ------------------------------------------------------------------ cad ---


class CADGenerateRequest(BaseModel):
    type: Literal["quadcopter_frame"] = "quadcopter_frame"
    parameters: dict[str, Any] = Field(default_factory=dict)
    outputs: list[Literal["stl", "step", "glb", "json"]] = Field(
        default_factory=lambda: ["stl", "json"]
    )


# -------------------------------------------------------------- generic ---


class ExecuteRequest(BaseModel):
    """Generic entry point used by the workflow engine / future LLM tool-calls."""

    engine: str
    operation: str
    parameters: dict[str, Any] = Field(default_factory=dict)


class RequirementRequest(BaseModel):
    text: str = Field(..., min_length=1, max_length=1000)
