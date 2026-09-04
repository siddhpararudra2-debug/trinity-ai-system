"""Small database-backed worker for durable jobs."""
from __future__ import annotations

import argparse
import json
import asyncio
import os

from app.database import AsyncSessionLocal, init_db
from app.job_queue import claim, complete
from app.models import WorkflowRun
from app.pipeline.executor import PipelineExecutor, plan_from_payload

UNSUPPORTED_AUTOMATION = {"cad", "pcb", "fusion", "kicad", "firmware", "ocr"}


async def run_once(worker_id: str) -> dict | None:
    async with AsyncSessionLocal() as db:
        job = await claim(db, worker_id)
        if job is None:
            return None
        if job.kind == "workflow":
            payload = json.loads(job.payload_json)
            approved = payload.get("approved") is True
            if not approved:
                updated = await complete(db, job.id, worker_id, False, "Workflow approval is required before execution")
            else:
                from sqlalchemy import select
                run_id = payload.get("run_id")
                result = await db.execute(select(WorkflowRun).where(WorkflowRun.id == run_id))
                run = result.scalar_one_or_none()
                try:
                    plan = plan_from_payload(payload.get("plan") or {})
                    execution = await PipelineExecutor().execute_plan(
                        plan,
                        owner_id=job.owner_id,
                        context=payload.get("context"),
                    )
                    if run is not None:
                        run.status = "completed"
                        await db.commit()
                    updated = await complete(db, job.id, worker_id, True)
                except Exception as exc:
                    if run is not None:
                        run.status = "failed"
                        await db.commit()
                    updated = await complete(db, job.id, worker_id, False, str(exc))
        elif job.kind in UNSUPPORTED_AUTOMATION:
            message = (
                f"No executable handler is installed for '{job.kind}'. "
                "Attach the corresponding trusted worker/toolchain before running this job."
            )
            updated = await complete(db, job.id, worker_id, False, message)
        else:
            updated = await complete(db, job.id, worker_id, True)
        return {"id": updated.id, "status": updated.status, "error": updated.error} if updated else None


async def main() -> None:
    parser = argparse.ArgumentParser(description="Run one durable Trinity job or poll continuously")
    parser.add_argument("--worker-id", default=os.getenv("TRINITY_WORKER_ID", "trinity-worker"))
    parser.add_argument("--once", action="store_true", help="claim and process one job, then exit")
    args = parser.parse_args()
    await init_db()
    if args.once:
        print(await run_once(args.worker_id))
        return
    while True:
        result = await run_once(args.worker_id)
        if result is None:
            await asyncio.sleep(2)


if __name__ == "__main__":
    asyncio.run(main())
