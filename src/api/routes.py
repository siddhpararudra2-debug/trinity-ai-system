from __future__ import annotations

import asyncio
import shutil
import uuid
from pathlib import Path
from typing import Any

from fastapi import APIRouter, File, Query, UploadFile
from fastapi.responses import FileResponse

from src.artifacts.manager import artifact_manager
from src.core.config import settings
from src.core.errors import ArtifactNotFoundError, RequestValidationError
from src.engines.cad.engine import ALWAYS_SUPPORTED_FORMATS, DEFAULT_OUTPUTS
from src.engines.cad.step import kernel_info
from src.engines.cad.parts import get_part, list_supported_types
from src.engines.registry import registry
from src.intelligence.router import parse_requirement
from src.jobs.manager import job_manager
from src.models.schemas import (
    ArtifactOut,
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


# Extensions the native vision engine can actually decode. Kept in step with
# VisionEngine::supportedFormats() in app/native -- it returns png, jpg, jpeg
# and bmp only. Accepting anything else meant storing a file that then failed
# inside OpenCascade with a decode error instead of being refused here.
UPLOAD_IMAGE_EXTENSIONS = frozenset({"png", "jpg", "jpeg", "bmp"})


@router.post("/uploads", response_model=ArtifactOut)
async def upload_image(file: UploadFile = File(...)) -> dict:
    """Store an uploaded image as an artifact and return its reference.

    Uploads go through ArtifactManager like any engine output, so the file
    gets a SHA-256 checksum and a database row. The returned artifact_id is
    what the vision engine takes; the bridge resolves it to a concrete path
    before handing it to the native process.
    """
    suffix = Path(file.filename or "").suffix.lower().lstrip(".")
    if suffix not in UPLOAD_IMAGE_EXTENSIONS:
        raise RequestValidationError(
            "Unsupported image type",
            details={
                "filename": file.filename,
                "supported": sorted(UPLOAD_IMAGE_EXTENSIONS),
            },
        )

    # Never trust the client filename on disk: write to a generated name in a
    # temp dir and let ArtifactManager own the final location.
    staged_dir = settings.temp_dir / f"upload_{uuid.uuid4().hex}"
    staged_dir.mkdir(parents=True, exist_ok=True)
    staged = staged_dir / f"upload.{suffix}"

    size = 0
    limit = settings.max_upload_bytes
    try:
        with staged.open("wb") as fh:
            while chunk := await file.read(1 << 20):
                size += len(chunk)
                if size > limit:
                    raise RequestValidationError(
                        f"Upload exceeds the {limit // (1024 * 1024)} MB limit",
                        details={"limit_bytes": limit},
                    )
                fh.write(chunk)
        if size == 0:
            raise RequestValidationError("Uploaded file is empty")
        ref = await asyncio.to_thread(
            artifact_manager.store_file,
            staged,
            artifact_type="upload",
        )
    finally:
        shutil.rmtree(staged_dir, ignore_errors=True)

    return {
        "artifact_id": ref.artifact_id,
        "type": ref.type,
        "path": ref.path,
        "size_bytes": ref.size_bytes,
        "checksum": ref.checksum,
    }


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
    availability: dict[str, dict[str, Any]] = {}
    for fmt in (*ALWAYS_SUPPORTED_FORMATS, "step"):
        availability[fmt] = (
            {"available": True}
            if fmt in ALWAYS_SUPPORTED_FORMATS
            else dict(kernel_info())
        )

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
    domain = parsed["domain"]
    # CAD is the only domain whose engine takes a {type, parameters, outputs}
    # envelope; the bridged engines take their own parameter contract flat.
    # Assuming otherwise was why routing to them produced nonsense requests.
    if domain == "cad":
        parameters: dict[str, Any] = {
            "type": parsed["object"],
            "parameters": parsed["parameters"],
            "outputs": ["stl", "glb", "json"],
        }
    else:
        parameters = parsed["parameters"]
    return await asyncio.to_thread(
        job_manager.run_sync,
        domain,
        parsed["operation"],
        parameters,
    )
