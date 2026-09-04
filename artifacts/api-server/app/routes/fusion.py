from __future__ import annotations

import hmac
import os
from datetime import datetime, UTC
from typing import Any, Literal

from fastapi import APIRouter, File, Form, Header, HTTPException, UploadFile
from pydantic import BaseModel, Field

from app.designs.jobs import DesignJobStore
from app.designs.models import Artifact, JobStatus, ValidationCheck, ValidationReport, ValidationStatus
from app.fusion_worker import FusionAuthError, make_worker_token, script_hash, verify_worker_token

router = APIRouter(tags=["fusion-worker"])
_store = DesignJobStore()
_MAX_EXPORT_BYTES = 250 * 1024 * 1024


class FusionClaimRequest(BaseModel):
    worker_id: str = Field(min_length=3, max_length=96, pattern=r"^[A-Za-z0-9_-]+$")
    expires_in_seconds: int = Field(default=300, ge=60, le=900)


class FusionCompleteRequest(BaseModel):
    token: str
    status: Literal["ready", "needs_review", "failed"]
    artifact_ids: list[str] = Field(default_factory=list)
    validation_status: Literal["passed", "warnings", "failed"] = "passed"
    checks: list[dict[str, Any]] = Field(default_factory=list)
    fusion_version: str | None = None
    addin_version: str | None = None
    error: str | None = None


def _script_artifact(job):
    return next((artifact for artifact in job.artifacts if artifact.kind == "fusion_script"), None)


def _auth_or_503(provided_secret: str | None):
    configured = os.getenv("TRINITY_FUSION_WORKER_SECRET", "")
    if len(configured) < 32:
        raise HTTPException(status_code=503, detail="Fusion worker integration is not configured")
    if not provided_secret or not hmac.compare_digest(provided_secret, configured):
        raise HTTPException(status_code=401, detail="Invalid Fusion worker credentials")


@router.post("/fusion/jobs/{job_id}/claim")
async def claim_fusion_job(job_id: str, request: FusionClaimRequest, worker_secret: str | None = Header(default=None, alias="X-Trinity-Worker-Secret")):
    _auth_or_503(worker_secret)
    job = _store.get(job_id)
    if job is None or job.engine != "maker_cad":
        raise HTTPException(status_code=404, detail="CAD job not found")
    artifact = _script_artifact(job)
    if artifact is None:
        raise HTTPException(status_code=409, detail="CAD job has no Fusion script artifact")
    stored = _store.artifacts.read(artifact.id)
    if stored is None:
        raise HTTPException(status_code=409, detail="Fusion script artifact is unavailable")
    script_bytes = stored[1]
    expires_at = int(datetime.now(UTC).timestamp()) + request.expires_in_seconds
    token = make_worker_token(job_id, request.worker_id, script_hash(script_bytes), expires_at)
    job.status = JobStatus.generating
    job.updated_at = datetime.now(UTC)
    _store.save(job)
    return {
        "job_id": job_id,
        "token": token,
        "expires_at": expires_at,
        "worker_id": request.worker_id,
        "script": {"artifact_id": artifact.id, "filename": artifact.filename, "download_url": artifact.download_url, "sha256": artifact.sha256},
        "spec": job.spec.model_dump(mode="json") if job.spec else None,
    }


@router.post("/fusion/jobs/{job_id}/artifacts")
async def upload_fusion_artifact(
    job_id: str,
    file: UploadFile = File(...),
    token: str = Form(...),
    kind: str = Form("fusion_export"),
    worker_secret: str | None = Header(default=None, alias="X-Trinity-Worker-Secret"),
):
    _auth_or_503(worker_secret)
    job = _store.get(job_id)
    if job is None or job.engine != "maker_cad":
        raise HTTPException(status_code=404, detail="CAD job not found")
    script = _script_artifact(job)
    if script is None:
        raise HTTPException(status_code=409, detail="CAD job has no Fusion script artifact")
    try:
        worker_id = verify_worker_token(token, job_id, script.sha256)
    except FusionAuthError as exc:
        raise HTTPException(status_code=401, detail=str(exc)) from exc
    filename = os.path.basename(file.filename or "fusion_export.bin")
    data = await file.read(_MAX_EXPORT_BYTES + 1)
    if len(data) > _MAX_EXPORT_BYTES:
        raise HTTPException(status_code=413, detail="Fusion artifact exceeds the 250 MB limit")
    if not data:
        raise HTTPException(status_code=400, detail="Fusion artifact is empty")
    artifact = _store.artifacts.write(job_id, filename, data, kind, download_base="/api/artifacts")
    return {"worker_id": worker_id, "artifact": artifact}


@router.post("/fusion/jobs/{job_id}/complete")
async def complete_fusion_job(job_id: str, request: FusionCompleteRequest, worker_secret: str | None = Header(default=None, alias="X-Trinity-Worker-Secret")):
    _auth_or_503(worker_secret)
    job = _store.get(job_id)
    if job is None or job.engine != "maker_cad":
        raise HTTPException(status_code=404, detail="CAD job not found")
    script = _script_artifact(job)
    if script is None:
        raise HTTPException(status_code=409, detail="CAD job has no Fusion script artifact")
    try:
        worker_id = verify_worker_token(request.token, job_id, script.sha256)
    except FusionAuthError as exc:
        raise HTTPException(status_code=401, detail=str(exc)) from exc
    checks = []
    for raw in request.checks:
        raw_status = raw.get("status", "warning")
        status = raw_status if raw_status in {"passed", "warning", "failed", "skipped"} else "warning"
        checks.append(ValidationCheck(
            name=str(raw.get("name", "fusion-worker")),
            status=status,
            message=str(raw.get("message", "Fusion worker reported a result")),
            details=raw.get("details", {}) if isinstance(raw.get("details", {}), dict) else {},
        ))
    if request.error:
        checks.append(ValidationCheck(name="fusion-worker", status="failed", message=request.error))
    job.validation = ValidationReport(
        status=ValidationStatus(request.validation_status),
        checks=checks,
        tool="fusion-worker",
        tool_version=request.fusion_version,
    )
    for artifact_id in request.artifact_ids:
        stored = _store.artifacts.read(artifact_id)
        if stored is None:
            raise HTTPException(status_code=400, detail=f"Artifact {artifact_id} is not owned by this job")
        path, digest = stored
        if path.parent.name != job_id:
            raise HTTPException(status_code=400, detail=f"Artifact {artifact_id} is not owned by this job")
        if not any(existing.id == artifact_id for existing in job.artifacts):
            job.artifacts.append(Artifact(
                id=artifact_id,
                kind="fusion_export",
                filename=path.name.removeprefix(f"{artifact_id}_"),
                mime_type="application/octet-stream",
                size_bytes=path.stat().st_size,
                sha256=digest,
                download_url=f"/api/artifacts/{artifact_id}",
            ))
    job.status = JobStatus(request.status)
    job.error = request.error
    job.assumptions.append(f"Completed by Fusion worker {worker_id}.")
    job.updated_at = datetime.now(UTC)
    _store.save(job)
    _store.artifacts.register_manifest(job.job_id, job.engine, job.artifacts, job.validation)
    return {"job": job}
