from __future__ import annotations

import asyncio

from fastapi import APIRouter
from fastapi.responses import FileResponse

from app.artifacts.manager import artifact_manager
from app.core.config import settings
from app.engines.registry import registry
from app.jobs.manager import job_manager
from app.models.schemas import (
    CADGenerateRequest,
    EngineCapability,
    ExecuteRequest,
    JobOut,
    MathSolveRequest,
    RequirementRequest,
    ToolResponse,
)
from app.intelligence.router import parse_requirement

router = APIRouter(prefix="/api")


@router.get("/health")
def health() -> dict:
    return {"status": "ok", "service": settings.api_title, "version": settings.api_version}


@router.get("/engines", response_model=list[EngineCapability])
def list_engines() -> list[dict]:
    return registry.list()


@router.post("/execute", response_model=ToolResponse)
async def execute(req: ExecuteRequest) -> dict:
    return await asyncio.to_thread(job_manager.run_sync, req.engine, req.operation, req.parameters)


@router.get("/jobs", response_model=list[JobOut])
def list_jobs(limit: int = 50) -> list[JobOut]:
    return job_manager.list_recent(limit=limit)


@router.get("/jobs/{job_id}", response_model=JobOut)
def get_job(job_id: str) -> JobOut:
    return job_manager.get(job_id)


@router.get("/artifacts/{artifact_id}")
def download_artifact(artifact_id: str) -> FileResponse:
    ref = artifact_manager.get(artifact_id)
    return FileResponse(ref.path, filename=ref.path.split("/")[-1])


@router.post("/math/solve", response_model=ToolResponse)
async def math_solve(req: MathSolveRequest) -> dict:
    return await asyncio.to_thread(job_manager.run_sync, "math", "solve", req.model_dump())


@router.post("/cad/generate", response_model=ToolResponse)
async def cad_generate(req: CADGenerateRequest) -> dict:
    return await asyncio.to_thread(job_manager.run_sync, "cad", "generate", req.model_dump())


@router.post("/requirements/execute", response_model=ToolResponse)
async def execute_requirement(req: RequirementRequest) -> dict:
    parsed = parse_requirement(req.text)
    return await asyncio.to_thread(
        job_manager.run_sync,
        parsed["domain"],
        parsed["operation"],
        {"type": parsed["object"], "parameters": parsed["parameters"], "outputs": ["stl", "glb", "json"]},
    )
