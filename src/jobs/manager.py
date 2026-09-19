"""
Job System (PRD §27).

V1 stays deliberately lightweight: asyncio.to_thread runs each engine
call off the event loop, sqlite tracks state, and there's no queue
broker. Every job goes queued -> running -> completed|failed.
Distributed infra (Redis/Celery) is an explicit non-goal until it's
actually required.
"""

from __future__ import annotations

import json
import shutil
import uuid
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from src.artifacts.manager import artifact_manager
from src.core import cache
from src.core.errors import JobNotFoundError, TrinityError
from src.core.logging_config import get_logger
from src.db.database import get_connection
from src.engines.registry import registry
from src.models.schemas import JobOut

log = get_logger("jobs")


def _now() -> str:
    return datetime.now(UTC).isoformat()


class JobManager:
    def _insert_job(
        self, job_id: str, engine: str, operation: str, request: dict[str, Any]
    ) -> None:
        now = _now()
        with get_connection() as conn:
            conn.execute(
                """INSERT INTO jobs
                       (job_id, engine, operation, status, progress, request, result, error, created_at, updated_at)
                   VALUES (?, ?, ?, 'queued', 0.0, ?, NULL, NULL, ?, ?)""",
                (job_id, engine, operation, json.dumps(request), now, now),
            )
            conn.commit()

    def _update_job(self, job_id: str, **fields: Any) -> None:
        fields["updated_at"] = _now()
        set_clause = ", ".join(f"{k} = ?" for k in fields)
        values = list(fields.values()) + [job_id]
        with get_connection() as conn:
            conn.execute(f"UPDATE jobs SET {set_clause} WHERE job_id = ?", values)
            conn.commit()

    def _insert_validation(
        self, job_id: str, engine: str, status: str, checks: dict[str, Any]
    ) -> None:
        with get_connection() as conn:
            conn.execute(
                """INSERT INTO validations (validation_id, job_id, engine, status, checks, created_at)
                   VALUES (?, ?, ?, ?, ?, ?)""",
                (str(uuid.uuid4()), job_id, engine, status, json.dumps(checks), _now()),
            )
            conn.commit()

    def run_sync(
        self, engine_name: str, operation: str, parameters: dict[str, Any]
    ) -> dict[str, Any]:
        """Execute an engine call as a tracked job and return the ToolResponse dict.

        Synchronous by design: V1 engine calls are fast (seconds), so a
        job is really "one traceable unit of work" rather than a
        long-running background task. The same job row still gives us
        status history, timing, and a stable job_id for artifact lookups.
        """
        job_id = str(uuid.uuid4())
        self._insert_job(job_id, engine_name, operation, parameters)
        self._update_job(job_id, status="running", progress=0.1)
        log.info(
            "job started",
            extra={
                "ctx": {"job_id": job_id, "engine": engine_name, "operation": operation}
            },
        )

        try:
            engine = registry.get(engine_name)
        except TrinityError as exc:
            self._update_job(
                job_id, status="failed", progress=1.0, error=json.dumps(exc.to_dict())
            )
            raise

        # Only known pure operations are cacheable. CAD artifacts deliberately
        # are not reused because each artifact must preserve job provenance.
        deterministic = engine_name == "math" or (
            engine_name == "cad" and operation == "validate"
        )
        key = cache.cache_key(engine_name, operation, parameters)
        if deterministic and (cached := cache.get(key)) is not None:
            cached = {
                **cached,
                "job_id": job_id,
                "result": {**cached.get("result", {}), "cache_hit": True},
            }
            self._update_job(
                job_id,
                status="completed",
                progress=1.0,
                result=json.dumps(cached["result"]),
            )
            return cached

        try:
            engine_result = engine.execute(operation, parameters)
        except TrinityError as exc:
            self._update_job(
                job_id, status="failed", progress=1.0, error=json.dumps(exc.to_dict())
            )
            log.info(
                "job failed", extra={"ctx": {"job_id": job_id, "error": exc.to_dict()}}
            )
            return {
                "success": False,
                "engine": engine_name,
                "operation": operation,
                "result": {},
                "artifacts": [],
                "validation": None,
                "errors": [exc.to_dict()],
                "job_id": job_id,
            }
        except Exception as exc:  # noqa: BLE001 — intentional catch-all: mark failed, never stuck running
            err = {"code": "engine_execution_error", "message": str(exc), "details": {}}
            self._update_job(job_id, status="failed", progress=1.0, error=json.dumps(err))
            log.exception("job failed unexpectedly", extra={"ctx": {"job_id": job_id}})
            return {
                "success": False,
                "engine": engine_name,
                "operation": operation,
                "result": {},
                "artifacts": [],
                "validation": None,
                "errors": [err],
                "job_id": job_id,
            }

        # Persist any files the engine wrote to scratch space.
        stored_artifacts = []
        for temp_path_str, artifact_type in engine_result.pending_artifacts:
            ref = artifact_manager.store_file(
                Path(temp_path_str), artifact_type=artifact_type, job_id=job_id
            )
            stored_artifacts.append(ref)
        engine_result.artifacts.extend(stored_artifacts)

        if engine_result.pending_artifacts:
            scratch_dir = Path(engine_result.pending_artifacts[0][0]).parent
            shutil.rmtree(scratch_dir, ignore_errors=True)

        if engine_result.validation is not None:
            self._insert_validation(
                job_id,
                engine_name,
                engine_result.validation.status,
                engine_result.validation.checks,
            )

        response = {
            "success": engine_result.success,
            "engine": engine_result.engine,
            "operation": engine_result.operation,
            "result": engine_result.result,
            "artifacts": [
                {
                    "artifact_id": a.artifact_id,
                    "type": a.type,
                    "path": a.path,
                    "size_bytes": a.size_bytes,
                    "checksum": a.checksum,
                }
                for a in engine_result.artifacts
            ],
            "validation": (
                {
                    "status": engine_result.validation.status,
                    "checks": engine_result.validation.checks,
                }
                if engine_result.validation
                else None
            ),
            "errors": engine_result.errors,
            "job_id": job_id,
        }

        self._update_job(
            job_id,
            status="completed" if engine_result.success else "failed",
            progress=1.0,
            result=json.dumps(response["result"]),
        )
        if deterministic and response["success"]:
            cache.put(key, engine_name, operation, response)
        log.info(
            "job finished",
            extra={"ctx": {"job_id": job_id, "success": engine_result.success}},
        )
        return response

    def get(self, job_id: str) -> JobOut:
        with get_connection() as conn:
            row = conn.execute(
                "SELECT * FROM jobs WHERE job_id = ?", (job_id,)
            ).fetchone()
        if row is None:
            raise JobNotFoundError(f"No job with id '{job_id}'")
        return JobOut(
            job_id=row["job_id"],
            engine=row["engine"],
            operation=row["operation"],
            status=row["status"],
            progress=row["progress"],
            created_at=row["created_at"],
            updated_at=row["updated_at"],
            result=json.loads(row["result"]) if row["result"] else None,
            error=json.loads(row["error"]) if row["error"] else None,
        )

    def list_recent(self, limit: int = 50) -> list[JobOut]:
        with get_connection() as conn:
            rows = conn.execute(
                "SELECT * FROM jobs ORDER BY created_at DESC LIMIT ?", (limit,)
            ).fetchall()
        return [
            JobOut(
                job_id=r["job_id"],
                engine=r["engine"],
                operation=r["operation"],
                status=r["status"],
                progress=r["progress"],
                created_at=r["created_at"],
                updated_at=r["updated_at"],
                result=json.loads(r["result"]) if r["result"] else None,
                error=json.loads(r["error"]) if r["error"] else None,
            )
            for r in rows
        ]


job_manager = JobManager()
