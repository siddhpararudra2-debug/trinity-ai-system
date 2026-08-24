"""Filesystem-backed artifact storage for generated designs."""
from __future__ import annotations

import hashlib
import json
import mimetypes
import re
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from app.config import get_settings
from app.designs.models import Artifact, ArtifactManifest, ValidationReport


_SAFE_NAME = re.compile(r"[^A-Za-z0-9._-]+")


def _safe_filename(filename: str) -> str:
    cleaned = _SAFE_NAME.sub("_", Path(filename).name).strip("._")
    return cleaned or "artifact.bin"


class ArtifactStore:
    def __init__(self, root: str | Path | None = None) -> None:
        self.root = Path(root or get_settings().artifact_dir).expanduser().resolve()
        self.root.mkdir(parents=True, exist_ok=True)

    def job_dir(self, job_id: str) -> Path:
        safe_job = _SAFE_NAME.sub("_", job_id)
        path = self.root / safe_job
        path.mkdir(parents=True, exist_ok=True)
        return path

    def write(self, job_id: str, filename: str, content: str | bytes, kind: str, download_base: str = "/api/artifacts") -> Artifact:
        safe_name = _safe_filename(filename)
        data = content.encode("utf-8") if isinstance(content, str) else content
        digest = hashlib.sha256(data).hexdigest()
        artifact_id = f"artifact_{uuid.uuid4().hex}"
        path = self.job_dir(job_id) / f"{artifact_id}_{safe_name}"
        path.write_bytes(data)
        return Artifact(
            id=artifact_id,
            kind=kind,
            filename=safe_name,
            mime_type=mimetypes.guess_type(safe_name)[0] or "application/octet-stream",
            size_bytes=len(data),
            sha256=digest,
            download_url=f"{download_base}/{artifact_id}",
        )

    def register_manifest(self, job_id: str, engine: str, artifacts: list[Artifact], validation: ValidationReport) -> Path:
        manifest = ArtifactManifest(job_id=job_id, engine=engine, generated_at=datetime.now(timezone.utc), files=artifacts, validation=validation)
        path = self.job_dir(job_id) / "manifest.json"
        path.write_text(manifest.model_dump_json(indent=2), encoding="utf-8")
        return path

    def locate(self, artifact_id: str) -> Path | None:
        if not re.fullmatch(r"artifact_[a-f0-9]{32}", artifact_id):
            return None
        for path in self.root.glob(f"*/{artifact_id}_*"):
            if path.is_file():
                return path
        return None

    def read(self, artifact_id: str) -> tuple[Path, bytes] | None:
        path = self.locate(artifact_id)
        return (path, path.read_bytes()) if path else None
