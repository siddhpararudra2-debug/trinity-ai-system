"""Extension routes: pipelines, BOM sourcing, project sharing, previews."""
from __future__ import annotations

import base64

from fastapi import APIRouter, Depends, HTTPException, Request
from pydantic import BaseModel, Field
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_current_user, get_optional_user
from app.database import get_db
from app.models import User
from app.pipelines.paper_to_code import PaperToCodePipeline
from app.pipelines.whiteboard_pcb import WhiteboardPcbPipeline
from app.pipeline.executor import PipelineExecutor
from app.sharing.projects import ProjectSharingService
from app.sourcing.bom_service import BomSourcingService
from app.workflows import build_workflow_plan, plan_to_dict

router = APIRouter(tags=["extensions"])
_sharing = ProjectSharingService()
_bom = BomSourcingService()
_paper = PaperToCodePipeline()
_whiteboard = WhiteboardPcbPipeline()
_executor = PipelineExecutor()


class PaperToCodeRequest(BaseModel):
    paper_text: str = Field(min_length=10, max_length=200_000)
    equation_hint: str | None = Field(default=None, max_length=4000)


class WhiteboardPcbRequest(BaseModel):
    image_base64: str = Field(min_length=20)
    objective: str = Field(default="", max_length=4000)


class BomFromComponentsRequest(BaseModel):
    components: list[dict] = Field(min_length=1)
    preferences: dict | None = None


class BomFromCsvRequest(BaseModel):
    csv_text: str = Field(min_length=3)
    preferences: dict | None = None


class ShareProjectRequest(BaseModel):
    title: str = Field(min_length=3, max_length=255)
    manifest: dict
    public: bool = True


class PipelineExecuteRequest(BaseModel):
    objective: str = Field(min_length=3, max_length=4000)
    context: dict | None = None


@router.post("/pipelines/paper-to-code")
async def paper_to_code(request: PaperToCodeRequest):
    return await _paper.run(request.paper_text, request.equation_hint)


@router.post("/pipelines/whiteboard-to-pcb")
async def whiteboard_to_pcb(request: WhiteboardPcbRequest):
    try:
        image_data = base64.b64decode(request.image_base64.split(",", 1)[-1], validate=False)
    except Exception as exc:
        raise HTTPException(status_code=400, detail=f"Invalid image_base64: {exc}") from exc
    return await _whiteboard.run(image_data, request.objective)


@router.post("/pipelines/execute")
async def execute_pipeline(
    request: PipelineExecuteRequest,
    user: User = Depends(get_current_user),
):
    plan = plan_to_dict(build_workflow_plan(request.objective))
    if len(plan.get("steps", [])) < 2:
        raise HTTPException(status_code=400, detail="Objective does not produce a multi-engine pipeline")
    return await _executor.execute_plan(plan, owner_id=user.id, context=request.context)


@router.post("/sourcing/bom/components")
async def bom_from_components(request: BomFromComponentsRequest):
    return await _bom.from_components(request.components, request.preferences)


@router.post("/sourcing/bom/csv")
async def bom_from_csv(request: BomFromCsvRequest):
    return await _bom.from_csv(request.csv_text, request.preferences)


@router.post("/projects/share", status_code=201)
async def share_project(
    request: ShareProjectRequest,
    http_request: Request,
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    project = await _sharing.create_share(db, user.id, request.title, request.manifest, request.public)
    base = str(http_request.base_url).rstrip("/")
    return ProjectSharingService.serialize(project, base)


@router.get("/projects/shared/{token}")
async def get_shared_project(
    token: str,
    http_request: Request,
    db: AsyncSession = Depends(get_db),
    user: User | None = Depends(get_optional_user),
):
    project = await _sharing.get_by_token(db, token)
    if project is None:
        raise HTTPException(status_code=404, detail="Shared project not found")
    if not project.public and (user is None or user.id != project.owner_id):
        raise HTTPException(status_code=403, detail="This project is private")
    base = str(http_request.base_url).rstrip("/")
    return ProjectSharingService.serialize(project, base)


@router.post("/projects/shared/{token}/fork", status_code=201)
async def fork_shared_project(
    token: str,
    http_request: Request,
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    source = await _sharing.get_by_token(db, token)
    if source is None:
        raise HTTPException(status_code=404, detail="Shared project not found")
    if not source.public and user.id != source.owner_id:
        raise HTTPException(status_code=403, detail="Cannot fork a private project you do not own")
    fork = await _sharing.fork(db, source, user.id)
    base = str(http_request.base_url).rstrip("/")
    return ProjectSharingService.serialize(fork, base)


@router.get("/preview/pcb-svg")
async def pcb_svg_preview(job_id: str = "demo"):
    """Return a lightweight SVG schematic preview for in-browser verification."""
    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 420 280" width="420" height="280">
  <rect width="420" height="280" fill="#0b1220"/>
  <text x="20" y="30" fill="#7dd3fc" font-family="monospace" font-size="14">Trinity PCB Preview · {job_id}</text>
  <rect x="40" y="60" width="120" height="80" fill="none" stroke="#38bdf8" stroke-width="2"/>
  <text x="55" y="105" fill="#e2e8f0" font-family="monospace" font-size="12">U1 MCU</text>
  <line x1="160" y1="100" x2="240" y2="100" stroke="#94a3b8" stroke-width="2"/>
  <rect x="240" y="70" width="80" height="60" fill="none" stroke="#fbbf24" stroke-width="2"/>
  <text x="255" y="105" fill="#fde68a" font-family="monospace" font-size="12">R1</text>
  <line x1="100" y1="140" x2="100" y2="210" stroke="#94a3b8" stroke-width="2"/>
  <text x="85" y="230" fill="#fca5a5" font-family="monospace" font-size="12">GND</text>
</svg>"""
    return {"job_id": job_id, "svg": svg, "format": "svg"}
