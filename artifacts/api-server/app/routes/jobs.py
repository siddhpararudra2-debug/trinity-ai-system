from __future__ import annotations

from typing import Any

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_optional_user
from app.database import get_db
from app.job_queue import cancel, enqueue, get_job, serialize_job
from app.models import DurableJob, User

router = APIRouter(tags=["jobs"])


class JobCreateRequest(BaseModel):
    kind: str = Field(min_length=1, max_length=80)
    payload: dict[str, Any] = Field(default_factory=dict)
    max_attempts: int = Field(default=3, ge=1, le=20)


class JobCancelRequest(BaseModel):
    reason: str | None = Field(default=None, max_length=500)


@router.post("/jobs", status_code=202)
async def create_job(
    request: JobCreateRequest,
    db: AsyncSession = Depends(get_db),
    user: User | None = Depends(get_optional_user),
):
    try:
        job = await enqueue(db, request.kind, request.payload, user.id if user else None, request.max_attempts)
    except ValueError as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    return serialize_job(job)


@router.get("/jobs")
async def list_jobs(
    db: AsyncSession = Depends(get_db),
    user: User | None = Depends(get_optional_user),
):
    query = select(DurableJob).order_by(DurableJob.created_at.desc()).limit(100)
    if user is not None:
        query = query.where(DurableJob.owner_id == user.id)
    result = await db.execute(query)
    return [serialize_job(job) for job in result.scalars().all()]


@router.get("/jobs/{job_id}")
async def read_job(
    job_id: str,
    db: AsyncSession = Depends(get_db),
    user: User | None = Depends(get_optional_user),
):
    job = await get_job(db, job_id, user.id if user else None)
    if job is None:
        raise HTTPException(status_code=404, detail="Job not found")
    return serialize_job(job)


@router.post("/jobs/{job_id}/cancel")
async def cancel_job(
    job_id: str,
    request: JobCancelRequest | None = None,
    db: AsyncSession = Depends(get_db),
    user: User | None = Depends(get_optional_user),
):
    job = await cancel(db, job_id, user.id if user else None)
    if job is None:
        raise HTTPException(status_code=404, detail="Job not found")
    if request and request.reason:
        job.error = request.reason
        await db.commit()
        await db.refresh(job)
    return serialize_job(job)
