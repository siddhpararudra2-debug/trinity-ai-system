"""Typed contracts for the universal firmware generation engine."""
from __future__ import annotations

from datetime import datetime
from enum import StrEnum
from typing import Any

from pydantic import BaseModel, Field


class FirmwareStatus(StrEnum):
    generated = "generated"
    needs_input = "needs_input"
    needs_review = "needs_review"
    failed = "failed"


class TargetKind(StrEnum):
    mcu = "mcu"
    flight_controller = "flight_controller"
    fpga = "fpga"
    linux_board = "linux_board"
    generic = "generic"


class FirmwareTarget(BaseModel):
    id: str
    name: str
    kind: TargetKind
    vendor: str
    board: str
    mcu: str
    framework: str
    language: str
    build_system: str
    build_command: str
    flash_command: str
    architecture: str | None = None
    flash_bytes: int | None = None
    ram_bytes: int | None = None
    clock_hz: int | None = None
    pin_count: int | None = None
    operating_voltage: str | None = None
    supported_peripherals: list[str] = Field(default_factory=list)
    peripheral_details: dict[str, Any] = Field(default_factory=dict)
    aliases: list[str] = Field(default_factory=list)
    errata: list[str] = Field(default_factory=list)
    notes: list[str] = Field(default_factory=list)


class PinAssignment(BaseModel):
    name: str
    pin: str
    function: str
    active_high: bool = True


class PeripheralSpec(BaseModel):
    name: str
    kind: str
    bus: str | None = None
    pins: list[str] = Field(default_factory=list)
    options: dict[str, Any] = Field(default_factory=dict)


class FirmwareSpec(BaseModel):
    target_id: str
    project_name: str = "trinity_firmware"
    description: str = Field(min_length=3, max_length=4000)
    language: str | None = None
    framework: str | None = None
    features: list[str] = Field(default_factory=list)
    pins: list[PinAssignment] = Field(default_factory=list)
    peripherals: list[PeripheralSpec] = Field(default_factory=list)
    include_tests: bool = True
    safety_mode: str = "development"
    previous_code: str | None = None
    requested_files: list[str] = Field(default_factory=list)


class FirmwareFile(BaseModel):
    path: str
    content: str
    language: str
    kind: str = "source"
    line_count: int = 0


class FirmwareArtifact(BaseModel):
    id: str
    filename: str
    kind: str
    mime_type: str
    size_bytes: int
    sha256: str
    download_url: str


class FirmwareCheck(BaseModel):
    name: str
    status: str
    message: str
    details: dict[str, Any] = Field(default_factory=dict)
    line: int | None = None
    severity: str | None = None


class FirmwareValidation(BaseModel):
    status: str
    checks: list[FirmwareCheck] = Field(default_factory=list)
    toolchain: str | None = None


class ResourceEstimate(BaseModel):
    flash_bytes: int = 0
    ram_bytes: int = 0
    cpu_percent: float = 0.0
    flash_percent: float | None = None
    ram_percent: float | None = None
    confidence: float = 0.0
    assumptions: list[str] = Field(default_factory=list)


class DependencyStatus(BaseModel):
    name: str
    verified: bool
    reason: str


class SecurityFinding(BaseModel):
    rule: str
    severity: str
    message: str
    line: int | None = None
    remediation: str | None = None


class FirmwareJob(BaseModel):
    job_id: str
    owner_id: int | None = None
    engine: str = "firmware"
    target: FirmwareTarget | None = None
    spec: FirmwareSpec
    status: FirmwareStatus
    files: list[FirmwareFile] = Field(default_factory=list)
    artifacts: list[FirmwareArtifact] = Field(default_factory=list)
    validation: FirmwareValidation
    resource_estimate: ResourceEstimate | None = None
    dependencies: list[DependencyStatus] = Field(default_factory=list)
    security_findings: list[SecurityFinding] = Field(default_factory=list)
    rendering_hint: str = "firmware-code"
    detected_language: str | None = None
    confidence_score: float = 0.0
    archive_artifact_id: str | None = None
    questions: list[str] = Field(default_factory=list)
    assumptions: list[str] = Field(default_factory=list)
    error: str | None = None
    created_at: datetime
    updated_at: datetime


class FirmwareRequest(BaseModel):
    description: str = Field(min_length=3, max_length=4000)
    target_id: str | None = None
    project_name: str = "trinity_firmware"
    framework: str | None = None
    language: str | None = None
    features: list[str] = Field(default_factory=list)
    pins: list[PinAssignment] = Field(default_factory=list)
    peripherals: list[PeripheralSpec] = Field(default_factory=list)
    include_tests: bool = True
    safety_mode: str = "development"
    previous_code: str | None = None
    requested_files: list[str] = Field(default_factory=list)


class FirmwareJobResponse(BaseModel):
    job: FirmwareJob


class TargetListResponse(BaseModel):
    targets: list[FirmwareTarget]
