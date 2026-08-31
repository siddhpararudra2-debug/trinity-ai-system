"""Typed design-job contracts shared by CAD and PCB workflows."""
from __future__ import annotations

from datetime import datetime
from enum import Enum
from typing import Any, Literal

from pydantic import BaseModel, Field, PositiveFloat, field_validator


class JobStatus(str, Enum):
    queued = "queued"
    parsing = "parsing"
    generating = "generating"
    validating = "validating"
    ready = "ready"
    needs_input = "needs_input"
    needs_review = "needs_review"
    failed = "failed"


class ValidationStatus(str, Enum):
    passed = "passed"
    warnings = "warnings"
    failed = "failed"
    not_run = "not_run"


class ValidationCheck(BaseModel):
    name: str
    status: Literal["passed", "warning", "failed", "skipped"]
    message: str
    details: dict[str, Any] = Field(default_factory=dict)


class ValidationReport(BaseModel):
    status: ValidationStatus = ValidationStatus.not_run
    checks: list[ValidationCheck] = Field(default_factory=list)
    tool: str | None = None
    tool_version: str | None = None


class Artifact(BaseModel):
    id: str
    kind: str
    filename: str
    mime_type: str
    size_bytes: int
    sha256: str
    download_url: str
    preview_url: str | None = None


class HoleSpec(BaseModel):
    x_mm: float
    y_mm: float
    diameter_mm: PositiveFloat


class CadDesignSpec(BaseModel):
    part_type: Literal["l_bracket", "housing", "gear", "shaft"] = "bracket"
    units: Literal["mm", "in"] = "mm"
    dimensions_mm: dict[str, PositiveFloat] = Field(default_factory=dict)
    material_thickness_mm: PositiveFloat = 5.0
    holes: list[HoleSpec] = Field(default_factory=list)
    fillet_radius_mm: float = 0.0
    output_formats: list[Literal["fusion_script", "step", "stl", "f3d"]] = Field(
        default_factory=lambda: ["fusion_script"]
    )

    @field_validator("fillet_radius_mm")
    @classmethod
    def non_negative_fillet(cls, value: float) -> float:
        if value < 0:
            raise ValueError("fillet_radius_mm must be non-negative")
        return value


class ComponentSpec(BaseModel):
    reference: str
    value: str
    symbol: str
    footprint: str
    properties: dict[str, str] = Field(default_factory=dict)


class NetSpec(BaseModel):
    name: str
    connections: list[str] = Field(default_factory=list)


class MountingHoleSpec(BaseModel):
    x_mm: float
    y_mm: float
    diameter_mm: PositiveFloat = 3.2


class PcbDesignSpec(BaseModel):
    board_name: str = "trinity_board"
    width_mm: PositiveFloat = 60.0
    height_mm: PositiveFloat = 40.0
    layers: Literal[2, 4, 6, 8] = 2
    components: list[ComponentSpec] = Field(default_factory=list)
    nets: list[NetSpec] = Field(default_factory=list)
    mounting_holes: list[MountingHoleSpec] = Field(default_factory=list)
    stackup: dict[str, str] = Field(default_factory=dict)
    routing_constraints: dict[str, Any] = Field(default_factory=dict)
    outputs: list[
        Literal[
            "project",
            "schematic",
            "pcb",
            "gerbers",
            "drill",
            "bom",
            "3d_preview",
        ]
    ] = Field(default_factory=lambda: ["project", "schematic", "pcb"])


class DesignJob(BaseModel):
    job_id: str
    owner_id: int | None = None
    engine: Literal["maker_cad", "maker_pcb"]
    status: JobStatus
    request_hash: str
    spec: CadDesignSpec | PcbDesignSpec | None = None
    artifacts: list[Artifact] = Field(default_factory=list)
    validation: ValidationReport = Field(default_factory=ValidationReport)
    questions: list[str] = Field(default_factory=list)
    assumptions: list[str] = Field(default_factory=list)
    error: str | None = None
    created_at: datetime
    updated_at: datetime


class CadDesignRequest(BaseModel):
    description: str = Field(min_length=3, max_length=2000)
    parameters: dict[str, Any] = Field(default_factory=dict)
    output_formats: list[Literal["fusion_script", "step", "stl", "f3d"]] | None = None


class PcbDesignRequest(BaseModel):
    description: str = Field(min_length=3, max_length=2000)
    components: list[str] = Field(default_factory=list)
    spec: PcbDesignSpec | None = None
    outputs: list[
        Literal[
            "project",
            "schematic",
            "pcb",
            "gerbers",
            "drill",
            "bom",
            "3d_preview",
        ]
    ] | None = None


class DesignJobResponse(BaseModel):
    job: DesignJob
    legacy: dict[str, Any] = Field(default_factory=dict)


class ArtifactManifest(BaseModel):
    job_id: str
    engine: str
    generated_at: datetime
    files: list[Artifact]
    validation: ValidationReport
