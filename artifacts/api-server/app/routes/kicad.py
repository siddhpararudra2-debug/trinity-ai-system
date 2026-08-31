from __future__ import annotations

import hmac
import os
from datetime import datetime, UTC
from typing import Any, Literal

from fastapi import APIRouter, File, Form, Header, HTTPException, UploadFile
from pydantic import BaseModel, Field

from app.designs.jobs import DesignJobStore
from app.designs.models import Artifact, JobStatus, ValidationCheck, ValidationReport, ValidationStatus

router = APIRouter(tags=["kicad-worker"])
_store = DesignJobStore()
_MAX_BYTES = 250 * 1024 * 1024
_ALLOWED_KINDS = {"kicad_erc_report", "kicad_drc_report", "gerber", "drill", "bom", "kicad_3d_preview", "kicad_worker_result"}


class KicadCompleteRequest(BaseModel):
    status: Literal["ready", "needs_review", "failed"]
    validation_status: Literal["passed", "warnings", "failed"]
    artifact_ids: list[str] = Field(default_factory=list)
    checks: list[dict[str, Any]] = Field(default_factory=list)
    kicad_version: str | None = None
    error: str | None = None


def _authenticate(provided: str | None) -> None:
    configured = os.getenv("TRINITY_KICAD_WORKER_SECRET", "")
    if len(configured) < 32:
        raise HTTPException(status_code=503, detail="KiCad worker integration is not configured")
    if not provided or not hmac.compare_digest(provided, configured):
        raise HTTPException(status_code=401, detail="Invalid KiCad worker credentials")


def _job_or_404(job_id: str):
    job = _store.get(job_id)
    if job is None or job.engine != "maker_pcb":
        raise HTTPException(status_code=404, detail="PCB job not found")
    return job


@router.post("/kicad/jobs/{job_id}/artifacts")
async def upload_kicad_artifact(
    job_id: str,
    file: UploadFile = File(...),
    kind: str = Form(...),
    worker_secret: str | None = Header(default=None, alias="X-Trinity-KiCad-Secret"),
):
    _authenticate(worker_secret)
    _job_or_404(job_id)
    if kind not in _ALLOWED_KINDS:
        raise HTTPException(status_code=400, detail=f"Unsupported KiCad artifact kind: {kind}")
    data = await file.read(_MAX_BYTES + 1)
    if len(data) > _MAX_BYTES:
        raise HTTPException(status_code=413, detail="KiCad artifact exceeds the 250 MB limit")
    if not data:
        raise HTTPException(status_code=400, detail="KiCad artifact is empty")
    filename = os.path.basename(file.filename or "kicad-artifact.bin")
    artifact = _store.artifacts.write(job_id, filename, data, kind, download_base="/api/artifacts")
    return {"artifact": artifact}


@router.post("/kicad/jobs/{job_id}/complete")
async def complete_kicad_job(job_id: str, request: KicadCompleteRequest, worker_secret: str | None = Header(default=None, alias="X-Trinity-KiCad-Secret")):
    _authenticate(worker_secret)
    job = _job_or_404(job_id)
    checks: list[ValidationCheck] = []
    for raw in request.checks:
        raw_status = raw.get("status", "warning")
        status = raw_status if raw_status in {"passed", "warning", "failed", "skipped"} else "warning"
        checks.append(ValidationCheck(name=str(raw.get("name", "kicad-worker")), status=status, message=str(raw.get("message", "KiCad worker reported a result")), details=raw.get("details", {}) if isinstance(raw.get("details", {}), dict) else {}))
    if request.error:
        checks.append(ValidationCheck(name="kicad-worker", status="failed", message=request.error))
    job.validation = ValidationReport(status=ValidationStatus(request.validation_status), checks=checks, tool="kicad-cli-worker", tool_version=request.kicad_version)
    for artifact_id in request.artifact_ids:
        stored = _store.artifacts.read(artifact_id)
        if stored is None:
            raise HTTPException(status_code=400, detail=f"Artifact {artifact_id} does not exist")
        path, digest = stored
        if path.parent.name != job_id:
            raise HTTPException(status_code=400, detail=f"Artifact {artifact_id} is not owned by this job")
        if not any(existing.id == artifact_id for existing in job.artifacts):
            job.artifacts.append(Artifact(id=artifact_id, kind="kicad_manufacturing_output", filename=path.name.removeprefix(f"{artifact_id}_"), mime_type="application/octet-stream", size_bytes=path.stat().st_size, sha256=digest, download_url=f"/api/artifacts/{artifact_id}"))
    job.status = JobStatus(request.status)
    job.error = request.error
    job.assumptions.append("Completed by authenticated KiCad CLI worker.")
    job.updated_at = datetime.now(UTC)
    _store.save(job)
    _store.artifacts.register_manifest(job.job_id, job.engine, job.artifacts, job.validation)
    return {"job": job}
