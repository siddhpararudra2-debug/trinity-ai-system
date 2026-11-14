"""
Public API schemas.

Pydantic is used at the edge (PRD section 7); internals prefer
dataclasses. Every tool response follows the common envelope from
PRD section 25.
"""

from __future__ import annotations

from typing import Any, Literal

from pydantic import BaseModel, Field, field_validator
from pydantic_core import PydanticCustomError

from src.engines.cad.parts import list_supported_types

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
    # Scaffold engines report status="scaffolded" from describe();
    # real engines omit it (None == live). Without this field FastAPI's
    # response_model silently stripped the status key, making all 8
    # engines look identically real on GET /api/engines.
    status: str | None = None


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
    type: str = "quadcopter_frame"
    parameters: dict[str, Any] = Field(default_factory=dict)
    outputs: list[Literal["stl", "step", "glb", "json"]] = Field(
        default_factory=lambda: ["stl", "json"]
    )

    @field_validator("type")
    @classmethod
    def _validate_type(cls, value: str) -> str:
        # Checked at the schema edge (not only in the engine) so an unknown
        # type gets a 422 here; engine-time failures are HTTP 200 success:false.
        # PydanticCustomError (not ValueError) keeps errors()["ctx"] JSON
        # serializable, which main.py's 422 handler passes to json.dumps.
        if value not in list_supported_types():
            raise PydanticCustomError(
                "unsupported_cad_type",
                "Unsupported CAD type '{name}'",
                {"name": value},
            )
        return value


class CADFamilySelector(BaseModel):
    parameter: str
    values: list[float] = Field(..., min_length=1, max_length=24)


class CADFamilyRequest(BaseModel):
    type: str = "quadcopter_frame"
    parameters: dict[str, Any] = Field(default_factory=dict)
    family: CADFamilySelector

    @field_validator("type")
    @classmethod
    def _validate_family_type(cls, value: str) -> str:
        # Same schema-edge rule as generate: an unknown part type is a 422,
        # never an engine-time 200/success:false.
        if value not in list_supported_types():
            raise PydanticCustomError(
                "unsupported_cad_type",
                "Unsupported CAD type '{name}'",
                {"name": value},
            )
        return value


class CADNestPartRequest(BaseModel):
    type: str = "mounting_plate"
    parameters: dict[str, Any] = Field(default_factory=dict)
    quantity: int = Field(default=1, ge=1, le=50)

    @field_validator("type")
    @classmethod
    def _validate_nest_type(cls, value: str) -> str:
        if value not in list_supported_types():
            raise PydanticCustomError(
                "unsupported_cad_type",
                "Unsupported CAD type '{name}'",
                {"name": value},
            )
        return value


class CADNestRequest(BaseModel):
    parts: list[CADNestPartRequest] = Field(..., min_length=1, max_length=12)
    stock_width_mm: float = Field(..., gt=0)
    stock_height_mm: float = Field(..., gt=0)
    spacing_mm: float = Field(default=2.0, ge=0)
    allow_rotation: bool = True


CADDrawingView = Literal["top", "front", "side", "iso"]


class CADDrawingsRequest(BaseModel):
    type: str = "quadcopter_frame"
    parameters: dict[str, Any] = Field(default_factory=dict)
    views: list[CADDrawingView] = Field(
        default_factory=lambda: ["top", "front", "side", "iso"]
    )

    @field_validator("type")
    @classmethod
    def _validate_drawings_type(cls, value: str) -> str:
        # Same schema-edge rule as generate/families/nest: an unknown part
        # type is a 422, never an engine-time 200/success:false envelope.
        if value not in list_supported_types():
            raise PydanticCustomError(
                "unsupported_cad_type",
                "Unsupported CAD type '{name}'",
                {"name": value},
            )
        return value


class CADCatalogParameter(BaseModel):
    """One parameter of one part, as enforced by the engine (PRD §12)."""

    name: str
    label: str
    unit: str
    default: float | int
    min: float | int
    description: str
    min_exclusive: bool = False
    max: float | int | None = None
    integer: bool = False
    allowed_values: tuple[float | int, ...] | None = None


class CADCatalogPart(BaseModel):
    """One registered part type: metadata, parameters and cross-field rules."""

    name: str
    title: str
    description: str
    parameters: list[CADCatalogParameter]
    rules: list[str]


class CADCatalog(BaseModel):
    """GET /api/cad/catalog: the whole catalog in one payload."""

    parts: list[CADCatalogPart]
    default_outputs: list[str]
    outputs: dict[str, dict[str, Any]]


# -------------------------------------------------------------- generic ---


class ExecuteRequest(BaseModel):
    """Generic entry point used by the workflow engine / future LLM tool-calls."""

    engine: str
    operation: str
    parameters: dict[str, Any] = Field(default_factory=dict)


# ------------------------------------------------------------- accounts ---

# M30 accounts. Request bodies only; responses are plain dicts so the
# inline-token delivery note can evolve without a schema bump.


class SignupRequest(BaseModel):
    email: str
    password: str


class LoginRequest(BaseModel):
    """``username`` accepts the email address the account signed up with."""

    username: str
    password: str


class VerifyRequest(BaseModel):
    token: str


class RefreshRequest(BaseModel):
    refresh_token: str


class PasswordResetRequest(BaseModel):
    email: str


class PasswordResetConfirm(BaseModel):
    token: str
    new_password: str


class RequirementRequest(BaseModel):
    text: str = Field(..., min_length=1, max_length=1000)


class AgentRunRequest(BaseModel):
    """Coder/researcher agent run: natural-language request for the loop."""

    prompt: str = Field(..., min_length=1, max_length=4000)
    max_steps: int = Field(default=8, ge=1, le=32)


class StreamPlanRequest(BaseModel):
    text: str = Field(..., min_length=1, max_length=1000)


class WorkflowNodeModel(BaseModel):
    id: str
    engine: str
    operation: str
    parameters: dict[str, Any] = Field(default_factory=dict)
    dependencies: list[str] = Field(default_factory=list)


class WorkflowExecuteRequest(BaseModel):
    nodes: list[WorkflowNodeModel] = Field(..., min_length=1)


class WorkflowNodeResult(BaseModel):
    id: str
    status: str  # queued | running | completed | failed
    job_id: str = ""
    result: dict[str, Any] = Field(default_factory=dict)
    artifacts: list[Any] = Field(default_factory=list)
    errors: list[dict[str, Any]] = Field(default_factory=list)


class WorkflowExecuteResponse(BaseModel):
    success: bool
    nodes: list[WorkflowNodeResult] = Field(default_factory=list)
    errors: list[dict[str, Any]] = Field(default_factory=list)


class UpdateApplyRequest(BaseModel):
    # Explicit opt-in: the updater never applies without this flag.
    confirm: bool = False


# ------------------------------------------------------ design records ---


class ProjectSummary(BaseModel):
    """One row of the local projects menu (M21.2).

    A row that could not be read back carries its load refusal verbatim
    in ``error`` and no stamps — listed, never silently hidden. A
    readable row carries the save stamps verbatim; statuses are never
    here (the report generates them per request).
    """

    record_id: str
    created_at: str | None = None
    updated_at: str | None = None
    app_version: str | None = None
    error: str | None = None


class ProjectsOut(BaseModel):
    projects: list[ProjectSummary] = Field(default_factory=list)


class SnapshotSummary(BaseModel):
    """One row of a record's snapshot history (M21.3) — a state one of
    its saves displaced. A slot that cannot be read back is listed with
    its refusal; an unreadable history entry is never silently hidden."""

    snapshot_id: int | None = None
    taken_at: str | None = None
    reason: str | None = None
    created_at: str | None = None
    updated_at: str | None = None
    app_version: str | None = None
    error: str | None = None


class SnapshotsOut(BaseModel):
    record_id: str
    snapshots: list[SnapshotSummary] = Field(default_factory=list)
