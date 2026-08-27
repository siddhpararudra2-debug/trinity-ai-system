from __future__ import annotations

import json
import uuid

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_current_user
from app.database import get_db
from app.job_queue import cancel, enqueue, get_job, serialize_job
from app.models import DurableJob, User, WorkflowRun
from app.workflows import build_workflow_plan, plan_to_dict

router = APIRouter(tags=["workflows"])


class WorkflowPlanRequest(BaseModel):
    objective: str = Field(min_length=3, max_length=4000)


class WorkflowExecuteRequest(WorkflowPlanRequest):
    require_approval: bool = True


def _run_payload(run: WorkflowRun, queue_job: DurableJob | None = None) -> dict:
    return {
        "run_id": run.id,
        "owner_id": run.owner_id,
        "objective": run.objective,
        "plan": json.loads(run.plan_json),
        "status": run.status,
        "queue_job_id": run.queue_job_id,
        "queue": serialize_job(queue_job) if queue_job else None,
        "created_at": run.created_at.isoformat() if run.created_at else None,
        "updated_at": run.updated_at.isoformat() if run.updated_at else None,
    }


@router.post("/workflows/plan")
async def create_workflow_plan(request: WorkflowPlanRequest):
    return plan_to_dict(build_workflow_plan(request.objective))


@router.post("/workflows/execute", status_code=202)
async def execute_workflow(
    request: WorkflowExecuteRequest,
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    plan = plan_to_dict(build_workflow_plan(request.objective))
    run = WorkflowRun(
        id=f"run_{uuid.uuid4().hex}",
        owner_id=user.id,
        objective=request.objective,
        plan_json=json.dumps(plan, sort_keys=True),
        status="awaiting_approval" if request.require_approval else "queued",
    )
    db.add(run)
    await db.commit()
    await db.refresh(run)
    queue_job = None
    if not request.require_approval:
        queue_job = await enqueue(db, "workflow", {"run_id": run.id, "plan": plan, "approved": True}, user.id)
        run.queue_job_id = queue_job.id
        await db.commit()
        await db.refresh(run)
    return _run_payload(run, queue_job)


@router.post("/workflows/runs/{run_id}/approve")
async def approve_workflow(
    run_id: str,
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    query = select(WorkflowRun).where(WorkflowRun.id == run_id)
    query = query.where(WorkflowRun.owner_id == user.id)
    result = await db.execute(query)
    run = result.scalar_one_or_none()
    if run is None:
        raise HTTPException(status_code=404, detail="Workflow run not found")
    if run.status not in {"awaiting_approval", "queued"}:
        raise HTTPException(status_code=409, detail="Workflow run is not awaiting approval")
    if run.queue_job_id:
        queue_job = await get_job(db, run.queue_job_id, user.id)
    else:
        plan = json.loads(run.plan_json)
        queue_job = await enqueue(db, "workflow", {"run_id": run.id, "plan": plan, "approved": True}, user.id)
        run.queue_job_id = queue_job.id
    if queue_job is None:
        raise HTTPException(status_code=409, detail="Workflow queue job is unavailable")
    queue_job.status = "queued"
    payload = json.loads(queue_job.payload_json)
    payload["approved"] = True
    queue_job.payload_json = json.dumps(payload, sort_keys=True)
    run.status = "queued"
    await db.commit()
    await db.refresh(run)
    await db.refresh(queue_job)
    return _run_payload(run, queue_job)


@router.get("/workflows/runs/{run_id}")
async def get_workflow_run(
    run_id: str,
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    query = select(WorkflowRun).where(WorkflowRun.id == run_id)
    query = query.where(WorkflowRun.owner_id == user.id)
    result = await db.execute(query)
    run = result.scalar_one_or_none()
    if run is None:
        raise HTTPException(status_code=404, detail="Workflow run not found")
    queue_job = await get_job(db, run.queue_job_id, user.id) if run.queue_job_id else None
    return _run_payload(run, queue_job)


@router.post("/workflows/runs/{run_id}/cancel")
async def cancel_workflow_run(
    run_id: str,
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    query = select(WorkflowRun).where(WorkflowRun.id == run_id)
    query = query.where(WorkflowRun.owner_id == user.id)
    result = await db.execute(query)
    run = result.scalar_one_or_none()
    if run is None:
        raise HTTPException(status_code=404, detail="Workflow run not found")
    queue_job = await cancel(db, run.queue_job_id, user.id) if run.queue_job_id else None
    run.status = "cancelled"
    await db.commit()
    await db.refresh(run)
    return _run_payload(run, queue_job)
