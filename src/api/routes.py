from __future__ import annotations

import asyncio
from pathlib import Path

from fastapi import APIRouter, Query
from fastapi.responses import FileResponse

from src.artifacts.manager import artifact_manager
from src.core.config import settings
from src.core.errors import ArtifactNotFoundError
from src.engines.cad.engine import (
    DEFAULT_OUTPUTS,
    NOT_YET_SUPPORTED_FORMATS,
    STEP_UNAVAILABLE_REASON,
)
from src.engines.cad.parts import get_part, list_supported_types
from src.engines.registry import registry
from src.intelligence.router import parse_requirement
from src.jobs.manager import job_manager
from src.models.schemas import (
    CADCatalog,
    CADGenerateRequest,
    EngineCapability,
    ExecuteRequest,
    JobOut,
    MathSolveRequest,
    RequirementRequest,
    ToolResponse,
)

router = APIRouter(prefix="/api")


@router.get("/health")
def health() -> dict:
    return {
        "status": "ok",
        "service": settings.api_title,
        "version": settings.api_version,
    }


@router.get("/engines", response_model=list[EngineCapability])
def list_engines() -> list[dict]:
    return registry.list()


@router.post("/execute", response_model=ToolResponse)
async def execute(req: ExecuteRequest) -> dict:
    return await asyncio.to_thread(
        job_manager.run_sync, req.engine, req.operation, req.parameters
    )


@router.get("/jobs", response_model=list[JobOut])
def list_jobs(limit: int = Query(default=50, le=200)) -> list[JobOut]:
    return job_manager.list_recent(limit=limit)


@router.get("/jobs/{job_id}", response_model=JobOut)
def get_job(job_id: str) -> JobOut:
    return job_manager.get(job_id)


@router.get("/artifacts/{artifact_id}")
def download_artifact(artifact_id: str) -> FileResponse:
    ref = artifact_manager.get(artifact_id)
    path = Path(ref.path)
    if not path.is_file():
        raise ArtifactNotFoundError(f"No artifact file for id '{artifact_id}'")
    return FileResponse(str(path), filename=path.name)


@router.post("/math/solve", response_model=ToolResponse)
async def math_solve(req: MathSolveRequest) -> dict:
    return await asyncio.to_thread(
        job_manager.run_sync, "math", "solve", req.model_dump()
    )


@router.post("/cad/generate", response_model=ToolResponse)
async def cad_generate(req: CADGenerateRequest) -> dict:
    return await asyncio.to_thread(
        job_manager.run_sync, "cad", "generate", req.model_dump()
    )


def _clean_number(value: float | int) -> float | int:
    """Render whole floats (50.0) as ints (50) so the payload stays tidy."""
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return value


@router.get("/cad/catalog", response_model=CADCatalog)
def cad_catalog() -> dict:
    """One machine-readable description of every registered CAD part.

    Pure registry read: parameters, units, defaults, bounds, cross-field
    rules and output-format availability, mirroring what the engine
    enforces. Nothing is generated, so the call has no side effects.
    """
    availability: dict[str, dict[str, bool | str]] = {}
    for fmt in ("stl", "glb", "json", *sorted(NOT_YET_SUPPORTED_FORMATS)):
        if fmt in NOT_YET_SUPPORTED_FORMATS:
            availability[fmt] = {
                "available": False,
                "reason": STEP_UNAVAILABLE_REASON,
            }
        else:
            availability[fmt] = {"available": True}

    return {
        "parts": [
            {
                "name": part.name,
                "title": part.title,
                "description": part.description,
                "parameters": [
                    {
                        "name": spec.name,
                        "label": spec.label,
                        "unit": spec.unit,
                        "default": _clean_number(spec.default),
                        "min": _clean_number(spec.min),
                        "description": spec.description,
                        "min_exclusive": spec.min_exclusive,
                        "max": (
                            _clean_number(spec.max)
                            if spec.max is not None
                            else None
                        ),
                        "integer": spec.integer,
                        "allowed_values": (
                            [_clean_number(v) for v in spec.allowed_values]
                            if spec.allowed_values is not None
                            else None
                        ),
                    }
                    for spec in part.parameters
                ],
                "rules": list(part.rules),
            }
            for part in (get_part(name) for name in list_supported_types())
        ],
        "default_outputs": list(DEFAULT_OUTPUTS),
        "outputs": availability,
    }


@router.post("/requirements/execute", response_model=ToolResponse)
async def execute_requirement(req: RequirementRequest) -> dict:
    parsed = parse_requirement(req.text)
    if parsed["domain"] == "math":
        return await asyncio.to_thread(
            job_manager.run_sync,
            parsed["domain"],
            parsed["operation"],
            parsed["parameters"],
        )
    return await asyncio.to_thread(
        job_manager.run_sync,
        parsed["domain"],
        parsed["operation"],
        {
            "type": parsed["object"],
            "parameters": parsed["parameters"],
            "outputs": ["stl", "glb", "json"],
        },
    )
