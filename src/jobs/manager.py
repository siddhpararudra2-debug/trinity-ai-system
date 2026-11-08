"""Job System (PRD §27).

V1 stays deliberately lightweight: asyncio.to_thread runs each engine
call off the event loop, sqlite tracks state, and there's no queue
broker. Every job goes queued -> running -> completed|failed.
Distributed infra (Redis/Celery) is an explicit non-goal until it's
actually required.

Safe execution (Item 10):
- SAFE_EXECUTION_ENABLED: global toggle
- SAFE_EXECUTION_TIMEOUT_SECONDS: max runtime per job
- SAFE_EXECUTION_MAX_RETRIES: max retry attempts on failure
- SAFE_EXECUTION_PER_USER_LIMIT: max concurrent jobs per user
"""

from __future__ import annotations

import json
import shutil
import uuid
from concurrent.futures import ThreadPoolExecutor
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from src.artifacts.manager import artifact_manager
from src.core import cache
from src.core.config import (
    SAFE_EXECUTION_ENABLED,
    SAFE_EXECUTION_MAX_RETRIES,
    SAFE_EXECUTION_PER_USER_LIMIT,
    SAFE_EXECUTION_TIMEOUT_SECONDS,
)
from src.core.errors import JobNotFoundError, TrinityError
from src.core.logging_config import get_logger
from src.db.database import get_connection
from src.engines.registry import registry
from src.models.schemas import JobOut

log = get_logger("jobs")

# Per-user job count tracking (in-memory; replace with Redis in production)
_user_job_counts: dict[str, int] = {}


def _now() -> str:
    return datetime.now(UTC).isoformat()


def _check_user_limit(user_id: str | None = None) -> bool:
    """Check if the user has not exceeded the per-user job limit."""
    if not SAFE_EXECUTION_ENABLED:
        return True
    if user_id is None:
        return True
    current = _user_job_counts.get(user_id, 0)
    limit = SAFE_EXECUTION_PER_USER_LIMIT
    if current >= limit:
        return False
    _user_job_counts[user_id] = current + 1
    return True


def _release_user_limit(user_id: str | None = None) -> None:
    """Release a slot in the per-user job counter."""
    if not SAFE_EXECUTION_ENABLED:
        return
    if user_id is None:
        return
    current = _user_job_counts.get(user_id, 0)
    if current > 0:
        _user_job_counts[user_id] = current - 1


def _with_timeout(
    func, timeout_seconds: int, user_id: str | None = None
) -> Any:
    """Run *func* with a wall-clock timeout.

    The per-user limit is enforced by the caller (run_sync) exactly once;
    this wrapper only bounds the wall clock. Engine calls run in a worker
    thread via asyncio.to_thread, so the timeout must be thread-based:
    asyncio.wait_for cannot join a sync call from inside a plain thread.

    Returns the function result, or raises TrinityError(job_timeout) if it
    exceeds the limit.
    """
    if not SAFE_EXECUTION_ENABLED or not timeout_seconds or timeout_seconds <= 0:
        return func()
    with ThreadPoolExecutor(max_workers=1) as pool:
        future = pool.submit(func)
        try:
            return future.result(timeout=timeout_seconds)
        except TimeoutError as exc:  # concurrent.futures.TimeoutError
            raise TrinityError(
                code="job_timeout",
                message=f"Job timed out after {timeout_seconds}s",
            ) from exc


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

        Safe execution (Item 10): enforces timeout, retry, and per-user limits.
        """

        job_id = str(uuid.uuid4())
        user_id = parameters.get("user_id")

        self._insert_job(job_id, engine_name, operation, parameters)
        self._update_job(job_id, status="running", progress=0.1)
        log.info(
            "job started",
            extra={
                "ctx": {"job_id": job_id, "engine": engine_name, "operation": operation}
            },
        )

        # Unknown engines must surface as HTTP 404, not a 200 failed job:
        # resolve the engine BEFORE the safe-execution retry loop and
        # re-raise registry errors exactly like the V1 contract does.
        try:
            engine = registry.get(engine_name)
        except TrinityError as exc:
            self._update_job(
                job_id, status="failed", progress=1.0, error=json.dumps(exc.to_dict())
            )
            raise

        # Safe execution (Item 10): enforce the per-user concurrent-job limit
        # exactly once, before any attempt runs.
        if not _check_user_limit(user_id):
            err = TrinityError(
                code="user_job_limit_exceeded",
                message=(
                    f"User {user_id} has exceeded the job limit of "
                    f"{SAFE_EXECUTION_PER_USER_LIMIT} concurrent jobs"
                ),
            )
            self._update_job(
                job_id,
                status="failed",
                progress=1.0,
                error=json.dumps(err.to_dict()),
            )
            return {
                "success": False,
                "engine": engine_name,
                "operation": operation,
                "result": {},
                "artifacts": [],
                "validation": None,
                "errors": [err.to_dict()],
                "job_id": job_id,
            }

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
            # Execute with timeout and retry logic (Item 10)
            max_retries = SAFE_EXECUTION_MAX_RETRIES
            timeout = SAFE_EXECUTION_TIMEOUT_SECONDS

            for attempt in range(max_retries + 1):
                try:
                    result = _with_timeout(
                        lambda: engine.execute(operation, parameters),
                        timeout,
                        user_id,
                    )
                    # Success - persist and return
                    stored_artifacts = []
                    for temp_path_str, artifact_type in result.pending_artifacts:
                        ref = artifact_manager.store_file(
                            Path(temp_path_str), artifact_type=artifact_type, job_id=job_id
                        )
                        stored_artifacts.append(ref)
                    result.artifacts.extend(stored_artifacts)

                    if result.pending_artifacts:
                        scratch_dir = Path(result.pending_artifacts[0][0]).parent
                        shutil.rmtree(scratch_dir, ignore_errors=True)

                    if result.validation is not None:
                        self._insert_validation(
                            job_id,
                            engine_name,
                            result.validation.status,
                            result.validation.checks,
                        )

                    response = {
                        "success": result.success,
                        "engine": result.engine,
                        "operation": result.operation,
                        "result": result.result,
                        "artifacts": [
                            {
                                "artifact_id": a.artifact_id,
                                "type": a.type,
                                "path": a.path,
                                "size_bytes": a.size_bytes,
                                "checksum": a.checksum,
                            }
                            for a in result.artifacts
                        ],
                        "validation": (
                            {
                                "status": result.validation.status,
                                "checks": result.validation.checks,
                            }
                            if result.validation
                            else None
                        ),
                        "errors": result.errors,
                        "job_id": job_id,
                    }

                    self._update_job(
                        job_id,
                        status="completed" if result.success else "failed",
                        progress=1.0,
                        result=json.dumps(response["result"]),
                    )
                    if deterministic and response["success"]:
                        cache.put(key, engine_name, operation, response)
                    log.info(
                        "job finished",
                        extra={"ctx": {"job_id": job_id, "success": result.success}},
                    )
                    return response

                except TrinityError as exc:
                    if attempt < max_retries:
                        # Retry
                        continue
                    # Final failure
                    self._update_job(
                        job_id,
                        status="failed",
                        progress=1.0,
                        error=json.dumps(exc.to_dict()),
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
                    if attempt < max_retries:
                        continue
                    err = {"code": "engine_execution_error", "message": str(exc), "details": {}}
                    self._update_job(
                        job_id, status="failed", progress=1.0, error=json.dumps(err)
                    )
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

            # Should not reach here, but just in case
            self._update_job(
                job_id, status="failed", progress=1.0, error=json.dumps({"code": "max_retries_exceeded"})
            )
            return {
                "success": False,
                "engine": engine_name,
                "operation": operation,
                "result": {},
                "artifacts": [],
                "validation": None,
                "errors": [{"code": "max_retries_exceeded", "message": "Max retries exceeded"}],
                "job_id": job_id,
            }

        finally:
            _release_user_limit(user_id)

    def _execute_engine(
        self, engine_name: str, operation: str, parameters: dict[str, Any]
    ) -> Any:
        """Execute a single engine call. Meant to be called within a timeout."""
        engine = registry.get(engine_name)
        return engine.execute(operation, parameters)

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