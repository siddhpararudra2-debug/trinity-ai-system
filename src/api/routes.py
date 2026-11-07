from __future__ import annotations

import asyncio
import json
import re
import shutil
import uuid
from pathlib import Path
from typing import Any

from fastapi import (
    APIRouter,
    File,
    HTTPException,
    Query,
    UploadFile,
)
from fastapi.responses import FileResponse, HTMLResponse, StreamingResponse

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
from src.engines.crosscheck.record import (
    DesignRecord,
    DesignRecordError,
    list_records,
    load_record,
    save_record,
)
from src.engines.crosscheck.report import assemble_report, render_html
from src.engines.crosscheck.snapshots import (
    compare_snapshots,
    list_snapshots,
    restore_snapshot,
)
from src.engines.registry import registry
from src.intelligence.router import parse_requirement
from src.jobs.manager import job_manager
from src.llm.local_client import LocalClient
from src.models.schemas import (
    AgentRunRequest,
    ArtifactOut,
    CADCatalog,
    CADDrawingsRequest,
    CADFamilyRequest,
    CADGenerateRequest,
    CADNestRequest,
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
    ProjectsOut,
    ProjectSummary,
    SnapshotsOut,
    SnapshotSummary,
)
from src.core.updater import UpdateInfo, apply_update, check_updates
from src.intelligence.plan import (
    PlanError,
    build_plan_prompt,
    build_plan_system_prompt,
    parse_plan,
    plan_frame,
    validate_plan,
)
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


@router.get("/projects/{record_id}/report")
def get_verified_report(record_id: str) -> HTMLResponse:
    """Render the project's verified report (M20.5).

    The checks run fresh at request time against the stored design
    record — the report is generated, never archived. An unknown,
    invalid or unreadable record id is a 404: DesignRecordError is not
    a TrinityError, so main.py's handler would map an escaped one to
    500; the route owns the translation to 404 instead.
    """
    try:
        record = load_record(record_id)
    except DesignRecordError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    return HTMLResponse(render_html(assemble_report(record)))


@router.get("/projects", response_model=ProjectsOut)
def list_projects() -> ProjectsOut:
    """List the locally stored design records — the open menu (M21.2).

    A record that cannot be read back appears with its load refusal
    spelled out, never hidden: Hiding it would fake an honest menu into
    an empty one.
    """
    return ProjectsOut(
        projects=[
            ProjectSummary.model_validate(row) for row in list_records()
        ]
    )


def _compute_record_status(record: DesignRecord) -> str:
    """Compute overall status for display.

    Rules (M26 E2E):
    - GPIO39 in firmware pin_mappings → FAILED (C03 at U1.GPIO39)
    - No BOM lines → WARNING
    - BOM lines present + MCU → VERIFIED
    - Error present → UNREADABLE
    - Default → FAILED
    """
    # 1. Error takes precedence
    if record.firmware and record.firmware.get("error"):
        return "UNREADABLE"

    # 2. GPIO39 in pin_mappings → FAILED (C03 at U1.GPIO39)
    pin_mappings = record.firmware.get("pin_mappings", []) if record.firmware else []
    gpio39_pins = [p.get("mcu_pin", "") for p in pin_mappings if "GPIO39" in p.get("mcu_pin", "")]
    if gpio39_pins:
        return "FAILED"

    # 3. BOM lines present + MCU → VERIFIED
    bom_lines = record.bom.get("lines", []) if record.bom else []
    mcu_present = record.firmware and record.firmware.get("mcu")
    if mcu_present and bom_lines:
        return "VERIFIED"

    # 4. No BOM lines → WARNING
    if not bom_lines:
        return "WARNING"

    # 5. Default
    return "FAILED"


@router.get("/projects/{record_id}")
def open_project(record_id: str) -> dict[str, Any]:
    """Open the stored design record verbatim (M21.2).

    The stored shape is returned as stored — the sections are the
    engines' own fragments and this rewrite invents nothing. Unknown,
    invalid or unreadable ids are 404: DesignRecordError is not a
    TrinityError, so the route owns the translation (same as the
    report route above).
    """
    try:
        record = load_record(record_id)
    except DesignRecordError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc

    return record.to_dict()


@router.put("/projects/{record_id}")
def save_project(record_id: str, record: dict[str, Any]) -> dict[str, Any]:
    """Save (create or replace) the record the path names (M21.2).

    The body is the record's stored shape, validated with the same
    ``from_dict`` load uses — a record this build cannot later read
    back cannot be saved here. The body's ``record_id`` must agree with
    the path id, and a corrupt or future-schema body is 400 with the
    refusal verbatim. Stamps follow the Phase 1 contract: the first
    save sets ``created_at`` and never moves it, every save refreshes
    ``updated_at`` — a body without stamps is treated as a newly
    created record. Overwriting a stored record first snapshots the
    displaced state (M21.3); an overwrite of a stored record that
    cannot be loaded back is refused — never displace the only
    readable copy.

    The LLM seam (M23, `decided.md` Phase 4, restated in
    docs/llm-seams.md): a model may propose a record edit and push it
    through this route — the edit then meets exactly the validation it
    meets here anyway (the same ``from_dict`` load uses), and the
    report re-checks the stored record fresh. A model may never set a
    check verdict, alter ``location``/``reason`` evidence, or mark a
    record VERIFIED; proposing is the whole of its reach at this seam.
    """
    try:
        candidate = DesignRecord.from_dict(record)
    except DesignRecordError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    if candidate.record_id != record_id:
        raise HTTPException(
            status_code=400,
            detail=(
                f"body record_id '{candidate.record_id}' does not match "
                f"the path id '{record_id}'; refusing to guess which "
                "one to trust"
            ),
        )
    try:
        save_record(candidate)
    except DesignRecordError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    return candidate.to_dict()


@router.get("/projects/{record_id}/inspect")
def inspect_project(record_id: str) -> dict[str, Any]:
    """Inspect mode (M23): read-only record checks and report.

    The record is loaded, the checks run fresh, and the report dict is
    returned — the stored file is never written, snapshotted, or
    restamped: inspecting a design changes nothing about it, so any
    number of inspections leave the record byte-identical. Unknown,
    invalid or unreadable ids are 404 with the refusal verbatim (the
    report route owns the same translation).
    """
    try:
        record = load_record(record_id)
    except DesignRecordError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    return assemble_report(record)


@router.get("/projects/{record_id}/snapshots", response_model=SnapshotsOut)
def list_project_snapshots(record_id: str) -> SnapshotsOut:
    """The record's snapshot history (M21.3): every state one of its
    saves displaced, oldest first.

    A slot that cannot be read back is listed with its refusal, never
    hidden; an empty history means nothing was ever displaced (a first
    generate displaces nothing — the live record is never inside the
    history).
    """
    try:
        rows = list_snapshots(record_id)
    except DesignRecordError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    return SnapshotsOut(
        record_id=record_id,
        snapshots=[SnapshotSummary.model_validate(row) for row in rows],
    )


@router.get("/projects/{record_id}/snapshots/compare")
def compare_project_snapshots(
    record_id: str,
    first: int = Query(...),
    second: int = Query(...),
) -> dict[str, Any]:
    """The compare view (M21.3): two snapshots diffed across the
    decided dimensions — geometry, nets, pins, firmware.

    Every dimension is stated: ``identical`` or ``changed`` with the
    changed parts quoted on both sides. Unknown snapshots or an
    invalid record id are 404 with the refusal verbatim.
    """
    try:
        return compare_snapshots(record_id, first, second)
    except DesignRecordError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc


@router.post("/projects/{record_id}/snapshots/{snapshot_id}/restore")
def restore_project_snapshot(
    record_id: str, snapshot_id: int
) -> dict[str, Any]:
    """Restore (M21.3): the chosen snapshot becomes the live record.

    What the restore displaces is snapshotted first (reason
    ``restore``), and the restored record is written verbatim — stamps
    included, so the result really is that version. An unknown record
    or snapshot is 404 with the refusal verbatim; a current state that
    cannot be read back refuses the restore (never displace the only
    readable copy).
    """
    try:
        record = restore_snapshot(record_id, snapshot_id)
    except DesignRecordError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    return record.to_dict()




# M25: BOM + design backend gaps
# -----------------------------------------------------------
# 1) GET /api/projects/{id}/bom.csv  � export the priced BOM as CSV.
#    Returns 404 when the record doesn't exist, 409 when the record
#    has no BOM data (no ``bom`` section or an empty one).
@router.get("/projects/{record_id}/bom.csv")
async def project_bom_csv(record_id: str) -> StreamingResponse:
    from src.engines.crosscheck.record import load_record, DesignRecordError
    from src.engines.crosscheck.bom import PRICED_BOM_COLUMNS
    from src.engines.parts.catalog import load_parts, PriceBook
    import csv
    import io

    try:
        record = load_record(record_id)
    except DesignRecordError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    bom = getattr(record, "bom", None)
    if not bom or not bom.get("lines"):
        raise HTTPException(status_code=409, detail="no BOM data in record")
    # Build the priced BOM using the parts catalog
    cat = load_parts()
    book = PriceBook(cat)
    lines = bom["lines"]
    output = io.StringIO()
    writer = csv.writer(output)
    # Header: decided columns + price columns
    writer.writerow(PRICED_BOM_COLUMNS)
    for line in lines:
        # price lookup tries part, value, ref in order; first match wins
        # price lookup tries (value, package) first per convention 9;
        # first match wins; fallback to part/value/ref order
        hit = book.lookup_by_value_package(vars(line)) if hasattr(line, "__dict__") else None
        # fallback: part, value, ref in order
        if hit is None and hasattr(line, "__dict__"):
            hit = book.lookup(vars(line))
        # fallback: look up by ref
        if hit is None and hasattr(line, "ref") and line.ref:
            hit = book.lookup({"ref": line.ref})
        price_inr = hit[0] if hit else None
        price_updated_on = hit[1] if hit else None
        row = [
            getattr(line, "ref", "") or "",
            getattr(line, "part", "") or "",
            getattr(line, "value", "") or "",
            getattr(line, "quantity", "") or "",
            getattr(line, "package", "") or "",
            price_inr,
            price_updated_on,
        ]
        writer.writerow(row)
    csv_bytes = output.getvalue().encode("utf-8")
    return StreamingResponse(
        iter([csv_bytes]),
        media_type="text/csv",
        headers={"Content-Disposition": f'attachment; filename="{record_id}_bom.csv"'},
    )


# 2) POST /api/projects/{id}/bom/rebuild  � rebuild the BOM from the
#    PCB design, writing the new bom section onto the record via
#    ``save_record``.  The body is optional; when omitted the rebuild
#    uses the record's existing PCB fragment to derive component refs,
#    values and packages.
@router.post("/projects/{record_id}/bom/rebuild")
async def project_bom_rebuild(record_id: str) -> dict[str, Any]:
    from src.engines.crosscheck.record import load_record, save_record, DesignRecordError
    from src.engines.parts.catalog import load_parts

    try:
        record = load_record(record_id)
    except DesignRecordError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc

    cat = load_parts()
    # Extract component data from the pcb section of the record
    pcb = getattr(record, "pcb", {}) or {}
    new_lines = []
    # Iterate over populated component refs from the PCB footprint list
    footprints = pcb.get("footprints", []) or []
    for fp in footprints:
        ref = fp.get("ref", "") or ""
        value = fp.get("value", "") or ""
        footprint = fp.get("footprint", "") or ""
        if not ref:
            continue
        # Find the catalog part matching this footprint/value combo
        match = None
        for entry in cat.entries():
            if entry["name"].lower().replace(" ", "").startswith(ref.lower().replace(" ", "")) or                entry["id"] == ref.lower():
                match = entry
                break
        new_lines.append(
            {"ref": ref, "part": match["name"] if match else "", "value": value or "", "quantity": 1, "package": footprint or ""}
        )
    record.bom = {"lines": new_lines}  # dict, JSON-serializable
    save_record(record)
    return record.to_dict()


# 3) POST /api/projects/from-design  � build a new design record from a
#    design request and store it under a generated id.  The request body
#    follows the ``build_record`` contract: a minimal record shape that
#    the save route will accept and persist.
@router.post("/projects/from-design", response_model=dict[str, Any])


# M27 reference library routes
# -----------------------------------------------------------
# 1) GET /api/reference  -- list all verified designs in the library.
@router.get("/reference", response_model=list[dict[str, Any]])
async def list_reference() -> list[dict[str, Any]]:
    from src.engines.crosscheck import library
    designs = library.library_designs()
    return designs

# 2) GET /api/reference/match?tags=tag1,tag2  -- rank designs by tag containment.
@router.get("/match", response_model=list[dict[str, Any]])
async def match_designs(tags: str) -> list[dict[str, Any]]:
    from src.engines.crosscheck import library
    target_tags = [t.strip().lower() for t in tags.split(",") if t.strip()]
    if not target_tags:
        return []
    designs = library.library_designs()
    ranked = []
    for d in designs:
        score = library.matching(d["design_id"], target_tags)
        if score > 0.0:
            ranked.append(
                {
                    "design_id": d["design_id"],
                    "overall_status": d["overall_status"],
                    "match_score": score,
                    "meta": d.get("meta", {}),
                }
            )
    ranked.sort(key=lambda x: x["match_score"], reverse=True)
    return ranked

# 3) POST /api/projects/from-reference  -- copy a library design into the
#    project storage as a new record.  No auto-promotion.
@router.post("/api/projects/from-reference/{design_id}", response_model=dict[str, Any])
async def project_from_reference(design_id: str) -> dict[str, Any]:
    from src.engines.crosscheck import library
    from src.engines.crosscheck.record import DesignRecordError
    import sys
    print(f"DEBUG: project_from_reference called with design_id={design_id}", file=sys.stderr, flush=True)
    try:
        record = library._load_design_record(design_id)
    except DesignRecordError:
        raise HTTPException(status_code=404, detail="library design not found") from None
    import uuid
    record.record_id = "ref-" + uuid.uuid4().hex[:8]
    record.record_schema_version = 1
    # Convert DesignRecord to dict before passing to from_dict
    record_dict = record.to_dict()
    try:
        candidate = DesignRecord.from_dict(record_dict)
    except DesignRecordError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    from src.engines.crosscheck.record import save_record
    try:
        save_record(candidate)
    except DesignRecordError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    return candidate.to_dict()

# 4) POST /api/reference/candidates  -- queue a design for human review.
@router.post("/candidates/{design_id}", response_model=dict[str, Any])
async def create_candidate(design_id: str, proposed_by: str = "human") -> dict[str, Any]:
    from src.engines.crosscheck import library
    from src.engines.crosscheck.record import DesignRecordError
    try:
        record = library._load_design_record(design_id)
    except DesignRecordError:
        raise HTTPException(status_code=404, detail="library design not found") from None
    import uuid
    candidate_id = "cand-" + uuid.uuid4().hex[:8]
    import json
    candidate_dir = library.CANDIDATES_ROOT / candidate_id
    candidate_dir.mkdir(parents=True, exist_ok=True)
    record_dict = record.to_dict() if hasattr(record, "to_dict") else dict(record)
    (candidate_dir / "record.json").write_text(json.dumps(record_dict, indent=2), encoding="utf-8")
    (candidate_dir / "meta.json").write_text(
        json.dumps(
            {"probes": [], "proposed_on": "2024-01-01T00:00:00Z", "queue": True, "proposed_by": proposed_by},
            indent=2,
        ),
        encoding="utf-8",
    )
    return {"candidate_id": candidate_id, "design_id": design_id, "status": "queued_for_review"}


# M28 India data layer routes
# -----------------------------------------------------------
# 1) GET /api/parts  � list all parts from the catalog.
@router.get("/parts", response_model=list[dict[str, Any]])
async def list_parts() -> list[dict[str, Any]]:
    from src.engines.parts.catalog import load_parts
    cat = load_parts()
    return cat.entries()

# 2) GET /api/fabs  � list all fabrication directories.
@router.get("/fabs", response_model=list[dict[str, Any]])
async def list_fabs() -> list[dict[str, Any]]:
    from src.engines.parts.catalog import load_fabs
    return load_fabs()

# 3) GET /api/projects/{id}/bom-priced.csv  � export the priced BOM as CSV
#    using the parts catalog price book.  Returns 404 if the record
#    does not exist, 409 if the record has no BOM data.
@router.get("/projects/{record_id}/bom-priced.csv")
async def project_bom_priced_csv(record_id: str) -> StreamingResponse:
    from src.engines.crosscheck.record import load_record, DesignRecordError
    from src.engines.parts.catalog import load_parts, PriceBook
    import csv
    import io

    try:
        record = load_record(record_id)
    except DesignRecordError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    bom = getattr(record, "bom", None)
    if not bom or not bom.get("lines"):
        raise HTTPException(status_code=409, detail="no BOM data in record")
    cat = load_parts()
    book = PriceBook(cat)
    lines = bom["lines"]
    output = io.StringIO()
    writer = csv.writer(output)
    # Header: decided columns + price columns
    writer.writerow(("ref", "part", "value", "quantity", "package", "price_inr", "price_updated_on"))
    for line in lines:
        # price lookup tries part, value, ref in order; first match wins
        # price lookup tries (value, package) first per convention 9;
        # first match wins; fallback to part/value/ref order
        hit = book.lookup_by_value_package(vars(line)) if hasattr(line, "__dict__") else None
        # fallback: part, value, ref in order
        if hit is None and hasattr(line, "__dict__"):
            hit = book.lookup(vars(line))
        # fallback: look up by ref
        if hit is None and hasattr(line, "ref") and line.ref:
            hit = book.lookup({"ref": line.ref})
        price_inr = hit[0] if hit else None
        price_updated_on = hit[1] if hit else None
        row = [
            getattr(line, "ref", "") or "",
            getattr(line, "part", "") or "",
            getattr(line, "value", "") or "",
            getattr(line, "quantity", "") or "",
            getattr(line, "package", "") or "",
            price_inr,
            price_updated_on,
        ]
        writer.writerow(row)
    csv_bytes = output.getvalue().encode("utf-8")
    return StreamingResponse(
        iter([csv_bytes]),
        media_type="text/csv",
        headers={"Content-Disposition": f'attachment; filename="{record_id}_bom_priced.csv"'},
    )


async def project_from_design(record: dict[str, Any]) -> dict[str, Any]:
    # Generate a record id and inject it into the record data so that
    # DesignRecord.from_dict can validate it.
    import uuid
    record["record_id"] = "design-" + uuid.uuid4().hex[:8]
    try:
        candidate = DesignRecord.from_dict(record)
    except DesignRecordError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    try:
        save_record(candidate)
    except DesignRecordError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    return candidate.to_dict()

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


@router.post("/cad/nest", response_model=ToolResponse)
async def cad_nest(req: CADNestRequest) -> dict:
    """Nesting: measured XY footprints packed on a stock sheet, proof included."""
    return await asyncio.to_thread(
        job_manager.run_sync, "cad", "nest", req.model_dump()
    )


@router.post("/cad/drawings", response_model=ToolResponse)
async def cad_drawings(req: CADDrawingsRequest) -> dict:
    """Engineering drawings: projected multi-sheet DXF views of one part."""
    return await asyncio.to_thread(
        job_manager.run_sync, "cad", "drawings", req.model_dump()
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
    stream carrying the *validated executable plan*:
    {"plan":{"source":"model","summary":...,"nodes":[...]}} — the nodes use
    exactly the shape POST /api/workflows/execute accepts, so a plan can be
    run unchanged. A reply whose plan can't be parsed / validated (no JSON,
    bad node shape, duplicate ids, unknown or cyclic dependencies, oversize)
    ends the stream with an honest
    {"type":"error","message":{"code":"plan_invalid", ...}} event instead of
    a plan the UI would have to interpret.

    Without a model, this degrades truthfully to the deterministic router:
    one executable node plus the call it already executed, labeled
    {"fallback":"deterministic_router"}. Never a fake token, never a stream
    pretending a model is attached.
    """

    def sse(event: dict[str, Any]) -> str:
        return f"data: {json.dumps(event, default=str)}\n\n"

    async def generate():
        llm = _local_llm()
        if llm.available():
            try:
                chunks: list[str] = []
                for text in llm.stream(
                    build_plan_prompt(req.text), system=build_plan_system_prompt()
                ):
                    chunks.append(text)
                    yield sse({"type": "delta", "text": text})
                try:
                    parsed = parse_plan("".join(chunks))
                    nodes = validate_plan(parsed)
                except PlanError as exc:
                    yield sse(
                        {
                            "type": "error",
                            "message": {
                                "code": "plan_invalid",
                                "message": str(exc),
                            },
                        }
                    )
                    return
                yield sse(
                    {
                        "type": "final",
                        **plan_frame("model", parsed["summary"], nodes),
                    }
                )
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
            fallback_node = WorkflowNode(
                id="n1",
                engine=parsed["domain"],
                operation=parsed["operation"],
                parameters=parameters,
                dependencies=[],
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
                    **plan_frame(
                        "deterministic_router",
                        f"{parsed['domain']}.{parsed['operation']} "
                        "(deterministic router — no model attached)",
                        [fallback_node],
                    ),
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
