from __future__ import annotations

import asyncio
import json
import re
import shutil
import uuid
from pathlib import Path
from typing import Any

from fastapi import APIRouter, File, Query, UploadFile
from fastapi.responses import FileResponse, StreamingResponse

from src.agents.coder import CoderAgent
from src.agents.loop import refusal
from src.agents.researcher import ResearcherAgent
from src.artifacts.manager import artifact_manager
from src.core.config import settings
from src.core.errors import (
    ArtifactNotFoundError,
    EngineExecutionError,
    RequestValidationError,
    TrinityError,
)
from src.engines.cad.engine import ALWAYS_SUPPORTED_FORMATS, DEFAULT_OUTPUTS
from src.engines.cad.step import kernel_info
from src.engines.cad.parts import get_part, list_supported_types
from src.engines.registry import registry
from src.intelligence.router import parse_requirement
from src.jobs.manager import job_manager
from src.llm.local_client import LocalClient
from src.models.schemas import (
    AgentRunRequest,
    ArtifactOut,
    CADCatalog,
    CADFamilyRequest,
    CADGenerateRequest,
    EngineCapability,
    ExecuteRequest,
    JobOut,
    MathSolveRequest,
    RequirementRequest,
    StreamPlanRequest,
    ToolResponse,
    UpdateApplyRequest,
    WorkflowExecuteRequest,
    WorkflowExecuteResponse,
    WorkflowNodeResult,
)
from src.core.updater import UpdateInfo, apply_update, check_updates
from src.workflows.dag import WorkflowNode, ordered as ordered_nodes

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


@router.post("/cad/families", response_model=ToolResponse)
async def cad_families(req: CADFamilyRequest) -> dict:
    """Parameter families: one part model, one parameter, many full variants."""
    return await asyncio.to_thread(
        job_manager.run_sync, "cad", "families", req.model_dump()
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


# ------------------------------------------------- Stage 4: intelligence ---


def _local_llm() -> LocalClient:
    """The keyless seam to the user's own (OpenAI-compatible) LLM."""
    return LocalClient()


async def _execute_tool(
    engine: str, operation: str, parameters: dict[str, Any]
) -> dict[str, Any]:
    return await asyncio.to_thread(job_manager.run_sync, engine, operation, parameters)


@router.post("/intelligence/stream-plan")
async def stream_plan(req: StreamPlanRequest):
    """Token-streamed plan over SSE (`text/event-stream`).

    With a model endpoint configured, delta tokens stream through as
    {"type":"delta","text":...} frames and a {"type":"final"} closes the
    stream. Without one, this degrades truthfully: the deterministic router
    either lands an executable plan ({"type":"final","tool":...}) or the
    stream ends with an honest {"type":"error"} event — never a fake token.
    """

    def sse(event: dict[str, Any]) -> str:
        return f"data: {json.dumps(event, default=str)}\n\n"

    async def generate():
        llm = _local_llm()
        if llm.available():
            try:
                for text in llm.stream(req.text):
                    yield sse({"type": "delta", "text": text})
                yield sse({"type": "final"})
                return
            except EngineExecutionError as exc:
                yield sse({"type": "error", "message": exc.to_dict()})
                return
        # No model attached: deterministic-router fallback, stated as such.
        try:
            parsed = parse_requirement(req.text)
            parameters = (
                {
                    "type": parsed["object"],
                    "parameters": parsed["parameters"],
                    "outputs": ["stl", "glb", "json"],
                }
                if parsed["domain"] == "cad"
                else parsed["parameters"]
            )
            envelope = await _execute_tool(
                parsed["domain"], parsed["operation"], parameters
            )
            yield sse(
                {
                    "type": "final",
                    "fallback": "deterministic_router",
                    "tool": {
                        "engine": parsed["domain"],
                        "operation": parsed["operation"],
                        "parameters": parameters,
                    },
                    "job": envelope,
                }
            )
        except Exception as exc:  # noqa: BLE001 — router miss ends as an error event
            if isinstance(exc, TrinityError):
                yield sse({"type": "error", "message": exc.to_dict()})
            else:
                yield sse(
                    {
                        "type": "error",
                        "message": {"code": "routing_error", "message": str(exc)},
                    }
                )

    return StreamingResponse(generate(), media_type="text/event-stream")


@router.post("/agents/{role}/run")
async def run_agent(role: str, req: AgentRunRequest) -> dict:
    """Multi-step agent run; degrades truthfully when no model is attached."""
    if role not in ("coder", "researcher"):
        raise RequestValidationError(
            "Unknown agent role",
            details={"role": role, "supported": ["coder", "researcher"]},
        )
    llm = _local_llm()
    if not llm.available():
        return refusal(role)
    prompt = req.prompt
    if role == "coder":
        result = await asyncio.to_thread(CoderAgent().run, prompt, llm, req.max_steps)
    else:
        result = await asyncio.to_thread(ResearcherAgent().run, prompt, llm, req.max_steps)
    return {"success": result["status"] == "completed", **result}


@router.post("/workflows/execute", response_model=WorkflowExecuteResponse)
async def execute_workflow(req: WorkflowExecuteRequest) -> dict:
    """Execute a multi-node DAG with per-node visible status.

    Nodes run in topological order on the job-manager contract; a failed
    node truthfully short-circuits its dependents (queued, never execute).
    Dependency parameters thread forward: a string parameter of the form
    `{node:<id>}.<dotted.path>` resolves against the upstream node's result
    when it ran successfully.
    """

    nodes = [
        WorkflowNode(
            id=n.id,
            engine=n.engine,
            operation=n.operation,
            parameters=dict(n.parameters),
            dependencies=list(n.dependencies),
        )
        for n in req.nodes
    ]
    try:
        ordered = ordered_nodes(nodes)
    except ValueError as exc:
        raise RequestValidationError(str(exc)) from exc

    reference = re.compile(r"\{node:([^}]+)\}\.([A-Za-z0-9_.]+)")
    results: dict[str, WorkflowNodeResult] = {}

    def lookup(node_id: str, path: str) -> Any:
        if path.startswith("result."):
            path = path[len("result.") :]  # walk inside the node result either way
        upstream = results.get(node_id)
        if upstream is None or upstream.status != "completed":
            raise RequestValidationError(
                f"Parameter references node '{node_id}' which did not complete"
            )
        current: Any = upstream.result
        for part in path.split("."):
            if isinstance(current, dict) and part in current:
                current = current[part]
            else:
                raise RequestValidationError(
                    f"Node '{node_id}' result has no '{path}'"
                )
        return current

    def resolve(value: Any) -> Any:
        """Substitute `{node:<id>}.<path>` references anywhere in a parameter
        value — exact-match strings become the value itself; strings that
        embed a reference inside larger text get string substitution."""
        if not isinstance(value, str):
            return value
        matches = list(reference.finditer(value))
        if not matches:
            return value
        if len(matches) == 1 and matches[0].span() == (0, len(value)):
            return lookup(matches[0].group(1).strip(), matches[0].group(2))
        result: Any = value
        for match in matches:
            found = lookup(match.group(1).strip(), match.group(2))
            result = result.replace(match.group(0), str(found))
        return result

    done: set[str] = set()
    output: list[dict[str, Any]] = []
    overall_success = True
    for node in ordered:
        blocked_deps = [
            dep for dep in node.dependencies if results[dep].status != "completed"
        ]
        if blocked_deps:
            record = WorkflowNodeResult(
                id=node.id,
                status="queued",
                errors=[
                    {
                        "code": "blocked_by_upstream",
                        "message": f"blocked: {', '.join(blocked_deps)} did not complete",
                    }
                ],
            )
            results[node.id] = record
            output.append(record.model_dump())
            continue

        failure: RequestValidationError | None = None
        resolved: dict[str, Any] = {}
        try:
            for key, value in node.parameters.items():
                resolved[key] = resolve(value)
        except RequestValidationError as exc:
            failure = exc

        if failure is not None:
            record = WorkflowNodeResult(
                id=node.id,
                status="failed",
                errors=[{"code": "parameter_error", "message": str(failure)}],
            )
            results[node.id] = record
            output.append(record.model_dump())
            overall_success = False
            continue

        envelope = await asyncio.to_thread(
            job_manager.run_sync, node.engine, node.operation, resolved
        )
        record = WorkflowNodeResult(
            id=node.id,
            status="completed" if envelope.get("success") else "failed",
            job_id=envelope.get("job_id", ""),
            result=envelope.get("result", {}),
            artifacts=list(envelope.get("artifacts", [])),
            errors=list(envelope.get("errors", [])),
        )
        results[node.id] = record
        output.append(record.model_dump())
        if envelope.get("success"):
            done.add(node.id)
        else:
            overall_success = False

    return WorkflowExecuteResponse(success=overall_success, nodes=output).model_dump()


# ------------------------------------------------- M13: self-update ---


@router.get("/update/check")
async def update_check() -> dict:
    """Honest availability answer; never downloads, never auto-applies."""
    info = await asyncio.to_thread(check_updates)
    if isinstance(info, UpdateInfo):
        return {
            "available": True,
            "version": info.version,
            "published_at": info.published_at,
            "size_bytes": info.size_bytes,
            "sha256": info.sha256,
        }
    # dict path: it already carries 'available': False plus a reason.
    return info


@router.post("/update/apply")
async def update_apply(req: UpdateApplyRequest) -> dict:
    """User-initiated only: prepare a hash-verified staged update.

    The swap itself happens at the next launcher boot (the running
    process cannot replace its own files on Windows).
    """
    if not req.confirm:
        return {
            "success": False,
            "errors": [{
                "code": "confirmation_required",
                "message": "POST /api/update/apply requires {\"confirm\": true}",
            }],
        }
    info = await asyncio.to_thread(check_updates)
    if not isinstance(info, UpdateInfo):
        return {
            "success": False,
            "errors": [{
                "code": "no_update_ready",
                "message": str(info.get("reason", "no update available")),
            }],
        }
    try:
        result = await asyncio.to_thread(apply_update, info)
    except ValueError as exc:
        return {"success": False, "errors": [{"code": "update_rejected", "message": str(exc)}]}
    except Exception as exc:  # noqa: BLE001 - network/download failures surface honestly
        return {"success": False, "errors": [{"code": "update_failed", "message": str(exc)}]}
    return {"success": True, **result}
