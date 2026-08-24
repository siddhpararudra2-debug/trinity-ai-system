from __future__ import annotations

import mimetypes
from pathlib import Path

from fastapi import APIRouter, Depends, HTTPException
from fastapi.responses import FileResponse

from app.auth import get_optional_user
from app.designs.jobs import DesignJobService
from app.models import User
from app.designs.models import CadDesignRequest, DesignJobResponse, PcbDesignRequest

router = APIRouter(tags=["designs"])
_service = DesignJobService()


def _legacy(job) -> dict:
    """Expose old response fields while clients migrate to artifacts[]."""
    if job.engine == "maker_cad":
        script_artifact = next((a for a in job.artifacts if a.kind == "fusion_script"), None)
        script = ""
        if script_artifact:
            stored = _service.store.artifacts.read(script_artifact.id)
            script = stored[1].decode("utf-8") if stored else ""
        return {
            "description": f"CAD design job {job.job_id}",
            "script": script,
            "filename": script_artifact.filename if script_artifact else "fusion_script.py",
            "instructions": "Run the generated script inside Fusion 360. Exported STEP/STL/F3D files require a connected Fusion worker.",
            "engine": "maker_cad",
        }
    return {
        "description": f"PCB design job {job.job_id}",
        "sch_content": "",
        "pcb_content": "",
        "filename": job.job_id,
        "instructions": "Download the project bundle and run ERC/DRC in KiCad before manufacturing.",
        "engine": "maker_pcb",
    }


@router.post("/designs/cad", response_model=DesignJobResponse, status_code=201)
async def create_cad_design(request: CadDesignRequest, user: User | None = Depends(get_optional_user)) -> DesignJobResponse:
    job = await _service.create_cad(request, owner_id=user.id if user else None)
    return DesignJobResponse(job=job, legacy=_legacy(job))


@router.post("/designs/pcb", response_model=DesignJobResponse, status_code=201)
async def create_pcb_design(request: PcbDesignRequest, user: User | None = Depends(get_optional_user)) -> DesignJobResponse:
    job = await _service.create_pcb(request, owner_id=user.id if user else None)
    return DesignJobResponse(job=job, legacy=_legacy(job))


@router.get("/design-jobs/{job_id}")
async def get_design_job(job_id: str, user: User | None = Depends(get_optional_user)):
    job = _service.store.get(job_id)
    if job is not None and user is not None and job.owner_id != user.id:
        job = None
    if job is None:
        raise HTTPException(status_code=404, detail="Design job not found")
    return job


@router.get("/design-jobs/{job_id}/validation")
async def get_design_job_validation(job_id: str, user: User | None = Depends(get_optional_user)):
    job = _service.store.get(job_id)
    if job is not None and user is not None and job.owner_id != user.id:
        job = None
    if job is None:
        raise HTTPException(status_code=404, detail="Design job not found")
    return job.validation


@router.get("/artifacts/{artifact_id}")
async def download_artifact(artifact_id: str):
    stored = _service.store.artifacts.read(artifact_id)
    if stored is None:
        raise HTTPException(status_code=404, detail="Artifact not found")
    path, _ = stored
    prefix = f"{artifact_id}_"
    filename = path.name[len(prefix):] if path.name.startswith(prefix) else path.name
    media_type = mimetypes.guess_type(filename)[0] or "application/octet-stream"
    return FileResponse(path, media_type=media_type, filename=filename)
