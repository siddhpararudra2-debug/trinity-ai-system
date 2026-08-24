"""Typed contracts for the free firmware generation engine."""
from __future__ import annotations

from datetime import datetime
from enum import Enum
from typing import Any, Literal

from pydantic import BaseModel, Field, PositiveInt


class FirmwareStatus(str, Enum):
    generated = "generated"
    needs_input = "needs_input"
    needs_review = "needs_review"
    failed = "failed"


class TargetKind(str, Enum):
    mcu = "mcu"
    flight_controller = "flight_controller"


class FirmwareTarget(BaseModel):
    id: str
    name: str
    kind: TargetKind
    vendor: str
    board: str
    mcu: str
    framework: str
    language: Literal["c", "cpp", "rust"]
    build_system: str
    build_command: str
    flash_command: str
    supported_peripherals: list[str] = Field(default_factory=list)
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
    language: Literal["c", "cpp", "rust"] | None = None
    framework: str | None = None
    features: list[str] = Field(default_factory=list)
    pins: list[PinAssignment] = Field(default_factory=list)
    peripherals: list[PeripheralSpec] = Field(default_factory=list)
    include_tests: bool = True
    safety_mode: Literal["development", "bench", "flight"] = "development"


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
    status: Literal["passed", "warning", "failed", "skipped"]
    message: str
    details: dict[str, Any] = Field(default_factory=dict)


class FirmwareValidation(BaseModel):
    status: Literal["passed", "warnings", "failed", "not_run"]
    checks: list[FirmwareCheck] = Field(default_factory=list)
    toolchain: str | None = None


class FirmwareJob(BaseModel):
    job_id: str
    owner_id: int | None = None
    engine: Literal["firmware"] = "firmware"
    target: FirmwareTarget | None = None
    spec: FirmwareSpec
    status: FirmwareStatus
    artifacts: list[FirmwareArtifact] = Field(default_factory=list)
    validation: FirmwareValidation
    questions: list[str] = Field(default_factory=list)
    assumptions: list[str] = Field(default_factory=list)
    error: str | None = None
    created_at: datetime
    updated_at: datetime


class FirmwareRequest(BaseModel):
    description: str = Field(min_length=3, max_length=4000)
    target_id: str | None = None
    project_name: str = "trinity_firmware"
    features: list[str] = Field(default_factory=list)
    pins: list[PinAssignment] = Field(default_factory=list)
    peripherals: list[PeripheralSpec] = Field(default_factory=list)
    include_tests: bool = True
    safety_mode: Literal["development", "bench", "flight"] = "development"


class FirmwareJobResponse(BaseModel):
    job: FirmwareJob


class TargetListResponse(BaseModel):
    targets: list[FirmwareTarget]
