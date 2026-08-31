"""Filesystem-backed artifact storage for generated designs."""
from __future__ import annotations

import hashlib
import os
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
        self.backend = os.getenv("TRINITY_ARTIFACT_BACKEND", "local").strip().lower()
        self.s3 = None
        self.s3_bucket = os.getenv("TRINITY_ARTIFACT_BUCKET", "").strip()
        self.s3_prefix = os.getenv("TRINITY_ARTIFACT_PREFIX", "trinity").strip("/")
        if self.backend == "s3":
            if not self.s3_bucket:
                raise RuntimeError("TRINITY_ARTIFACT_BUCKET is required when TRINITY_ARTIFACT_BACKEND=s3")
            try:
                import boto3
                kwargs = {}
                endpoint = os.getenv("TRINITY_S3_ENDPOINT", "").strip()
                if endpoint:
                    kwargs["endpoint_url"] = endpoint
                self.s3 = boto3.client("s3", **kwargs)
            except ImportError as exc:
                raise RuntimeError("boto3 is required for the S3 artifact backend") from exc

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
        download_url = f"{download_base}/{artifact_id}"
        if self.s3 is not None:
            key = f"{self.s3_prefix}/{safe_job}/{artifact_id}_{safe_name}"
            content_type = mimetypes.guess_type(safe_name)[0] or "application/octet-stream"
            self.s3.put_object(Bucket=self.s3_bucket, Key=key, Body=data, ContentType=content_type)
            download_url = self.s3.generate_presigned_url("get_object", Params={"Bucket": self.s3_bucket, "Key": key}, ExpiresIn=int(os.getenv("TRINITY_ARTIFACT_URL_TTL", "900")))
        return Artifact(
            id=artifact_id,
            kind=kind,
            filename=safe_name,
            mime_type=mimetypes.guess_type(safe_name)[0] or "application/octet-stream",
            size_bytes=len(data),
            sha256=digest,
            download_url=download_url,
        )

    def register_manifest(self, job_id: str, engine: str, artifacts: list[Artifact], validation: ValidationReport) -> Path:
        manifest = ArtifactManifest(job_id=job_id, engine=engine, generated_at=datetime.now(timezone.utc), files=artifacts, validation=validation)
        path = self.job_dir(job_id) / "manifest.json"
        manifest_bytes = manifest.model_dump_json(indent=2).encode("utf-8")
        path.write_bytes(manifest_bytes)
        if self.s3 is not None:
            key = f"{self.s3_prefix}/{_SAFE_NAME.sub('_', job_id)}/manifest.json"
            self.s3.put_object(Bucket=self.s3_bucket, Key=key, Body=manifest_bytes, ContentType="application/json")
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
