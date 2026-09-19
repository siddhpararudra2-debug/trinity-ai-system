"""
Artifact Manager (PRD §9).

Large files (STL, STEP, GLB, ...) live on the filesystem; only metadata
(id, type, path, size, checksum) goes into SQLite. This module is the
only thing allowed to write into storage/artifacts/.
"""

from __future__ import annotations

import hashlib
import shutil
import uuid
from datetime import datetime, timezone
from pathlib import Path

from src.core.config import settings
from src.core.errors import ArtifactNotFoundError
from src.db.database import get_connection
from src.engines.base import ArtifactRef


def _now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _checksum(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


class ArtifactManager:
    def store_file(
        self, src_path: Path, *, artifact_type: str, job_id: str | None = None
    ) -> ArtifactRef:
        """Move/copy a file produced by an engine into permanent artifact storage."""
        artifact_id = str(uuid.uuid4())
        dest_dir = settings.artifacts_dir / artifact_id
        dest_dir.mkdir(parents=True, exist_ok=True)
        dest_path = dest_dir / src_path.name

        shutil.copyfile(src_path, dest_path)
        size_bytes = dest_path.stat().st_size
        checksum = _checksum(dest_path)

        with get_connection() as conn:
            conn.execute(
                """INSERT INTO artifacts
                       (artifact_id, job_id, type, path, size_bytes, checksum, created_at)
                   VALUES (?, ?, ?, ?, ?, ?, ?)""",
                (
                    artifact_id,
                    job_id,
                    artifact_type,
                    str(dest_path),
                    size_bytes,
                    checksum,
                    _now(),
                ),
            )
            conn.commit()

        return ArtifactRef(
            artifact_id=artifact_id,
            type=artifact_type,
            path=str(dest_path),
            size_bytes=size_bytes,
            checksum=checksum,
        )

    def get(self, artifact_id: str) -> ArtifactRef:
        with get_connection() as conn:
            row = conn.execute(
                "SELECT * FROM artifacts WHERE artifact_id = ?", (artifact_id,)
            ).fetchone()
        if row is None:
            raise ArtifactNotFoundError(f"No artifact with id '{artifact_id}'")
        return ArtifactRef(
            artifact_id=row["artifact_id"],
            type=row["type"],
            path=row["path"],
            size_bytes=row["size_bytes"],
            checksum=row["checksum"],
        )

    def list_for_job(self, job_id: str) -> list[ArtifactRef]:
        with get_connection() as conn:
            rows = conn.execute(
                "SELECT * FROM artifacts WHERE job_id = ? ORDER BY created_at",
                (job_id,),
            ).fetchall()
        return [
            ArtifactRef(
                artifact_id=r["artifact_id"],
                type=r["type"],
                path=r["path"],
                size_bytes=r["size_bytes"],
                checksum=r["checksum"],
            )
            for r in rows
        ]


artifact_manager = ArtifactManager()
