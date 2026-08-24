"""Durable SQLite-backed queue primitives used by workflows and external workers."""
from __future__ import annotations

import json
import uuid
from datetime import datetime, timedelta, timezone
from typing import Any

from sqlalchemy import or_, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models import DurableJob

JOB_STATUSES = {"queued", "running", "succeeded", "failed", "cancelled"}


def _now() -> datetime:
    # SQLite stores these values without timezone information in the current schema.
    return datetime.now(timezone.utc).replace(tzinfo=None)


def serialize_job(job: DurableJob) -> dict[str, Any]:
    return {
        "id": job.id,
        "owner_id": job.owner_id,
        "kind": job.kind,
        "payload": json.loads(job.payload_json),
        "status": job.status,
        "attempts": job.attempts,
        "max_attempts": job.max_attempts,
        "worker_id": job.worker_id,
        "lease_expires_at": job.lease_expires_at.isoformat() if job.lease_expires_at else None,
        "available_at": job.available_at.isoformat() if job.available_at else None,
        "started_at": job.started_at.isoformat() if job.started_at else None,
        "finished_at": job.finished_at.isoformat() if job.finished_at else None,
        "error": job.error,
        "created_at": job.created_at.isoformat() if job.created_at else None,
        "updated_at": job.updated_at.isoformat() if job.updated_at else None,
    }


async def enqueue(
    db: AsyncSession,
    kind: str,
    payload: dict[str, Any],
    owner_id: int | None = None,
    max_attempts: int = 3,
) -> DurableJob:
    if not kind or len(kind) > 80:
        raise ValueError("job kind is required and must be at most 80 characters")
    if max_attempts < 1 or max_attempts > 20:
        raise ValueError("max_attempts must be between 1 and 20")
    job = DurableJob(
        id=f"job_{uuid.uuid4().hex}",
        owner_id=owner_id,
        kind=kind,
        payload_json=json.dumps(payload, sort_keys=True, default=str),
        status="queued",
        attempts=0,
        max_attempts=max_attempts,
        available_at=_now(),
    )
    db.add(job)
    await db.commit()
    await db.refresh(job)
    return job


async def get_job(db: AsyncSession, job_id: str, owner_id: int | None = None) -> DurableJob | None:
    query = select(DurableJob).where(DurableJob.id == job_id)
    if owner_id is not None:
        query = query.where(DurableJob.owner_id == owner_id)
    result = await db.execute(query)
    return result.scalar_one_or_none()


async def claim(
    db: AsyncSession,
    worker_id: str,
    lease_seconds: int = 300,
) -> DurableJob | None:
    if not worker_id or len(worker_id) > 120:
        raise ValueError("worker_id is required and must be at most 120 characters")
    if lease_seconds < 10 or lease_seconds > 86_400:
        raise ValueError("lease_seconds must be between 10 and 86400")
    now = _now()
    result = await db.execute(
        select(DurableJob)
        .where(
            or_(
                (DurableJob.status == "queued") & (DurableJob.available_at <= now),
                (DurableJob.status == "running") & (DurableJob.lease_expires_at < now),
            ),
            DurableJob.attempts < DurableJob.max_attempts,
        )
        .order_by(DurableJob.created_at.asc())
        .limit(1)
    )
    job = result.scalar_one_or_none()
    if job is None:
        return None
    job.status = "running"
    job.worker_id = worker_id
    job.attempts += 1
    job.started_at = job.started_at or now
    job.lease_expires_at = now + timedelta(seconds=lease_seconds)
    job.error = None
    await db.commit()
    await db.refresh(job)
    return job


async def complete(db: AsyncSession, job_id: str, worker_id: str, success: bool, error: str | None = None) -> DurableJob | None:
    job = await get_job(db, job_id)
    if job is None or job.worker_id != worker_id or job.status != "running":
        return None
    now = _now()
    job.lease_expires_at = None
    job.finished_at = now if success or job.attempts >= job.max_attempts else None
    job.status = "succeeded" if success else ("failed" if job.attempts >= job.max_attempts else "queued")
    job.available_at = now + timedelta(seconds=min(60 * job.attempts, 900))
    job.error = None if success else (error or "Worker failed without an error message")
    await db.commit()
    await db.refresh(job)
    return job


async def cancel(db: AsyncSession, job_id: str, owner_id: int | None = None) -> DurableJob | None:
    job = await get_job(db, job_id, owner_id=owner_id)
    if job is None or job.status in {"succeeded", "failed", "cancelled"}:
        return job
    job.status = "cancelled"
    job.lease_expires_at = None
    job.finished_at = _now()
    job.error = "Cancelled by user"
    await db.commit()
    await db.refresh(job)
    return job
