"""Firmware generation job service."""
from __future__ import annotations

import hashlib
import json
import re
import asyncio
import os
import tempfile
import time
import uuid
import zipfile
from collections import deque
from datetime import datetime, UTC
from io import BytesIO
from pathlib import Path
from typing import Any

from app.designs.artifacts import ArtifactStore
from app.firmware.builds import run_firmware_build
from app.firmware.analysis import analysis_checks, estimate_resources
from app.firmware.generator import FirmwareGenerator
from app.firmware.models import FirmwareArtifact, FirmwareJob, FirmwareRequest, FirmwareSpec, FirmwareStatus, FirmwareValidation
from app.firmware.registry import TARGETS, find_target
from app.firmware.universal import FirmwareEngine
from app.firmware.validator import validate_firmware


class FirmwareRateLimitError(ValueError):
    """Raised when a user exceeds the firmware generation budget."""


class FirmwareJobStore:
    def __init__(self, artifact_store: ArtifactStore | None = None) -> None:
        self.artifacts = artifact_store or ArtifactStore()
        self.jobs_dir = self.artifacts.root / "firmware-jobs"
        self.jobs_dir.mkdir(parents=True, exist_ok=True)

    def save(self, job: FirmwareJob) -> None:
        (self.jobs_dir / f"{job.job_id}.json").write_text(job.model_dump_json(indent=2), encoding="utf-8")

    def get(self, job_id: str) -> FirmwareJob | None:
        if not re.fullmatch(r"fw_[a-f0-9]{32}", job_id):
            return None
        path = self.jobs_dir / f"{job_id}.json"
        if not path.is_file():
            return None
        try:
            return FirmwareJob.model_validate_json(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return None


def _now() -> datetime:
    return datetime.now(UTC)


def _hash(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, default=str).encode("utf-8")).hexdigest()


def _artifact_from_design_artifact(value) -> FirmwareArtifact:
    return FirmwareArtifact(
        id=value.id,
        filename=value.filename,
        kind=value.kind,
        mime_type=value.mime_type,
        size_bytes=value.size_bytes,
        sha256=value.sha256,
        download_url=value.download_url,
    )


class FirmwareJobService:
    def __init__(self, store: FirmwareJobStore | None = None) -> None:
        self.store = store or FirmwareJobStore()
        self.generator = FirmwareGenerator()
        self.engine = FirmwareEngine(self.generator)
        self._cache: dict[str, tuple[Any, dict[str, Any]]] = {}
        self._requests: dict[int, deque[float]] = {}
        self._rate_lock = asyncio.Lock()

    async def create(self, request: FirmwareRequest, owner_id: int | None = None, conversation_key: str | None = None) -> FirmwareJob:
        await self._enforce_rate_limit(owner_id)
        target = find_target(request.target_id, request.description, request.framework, request.language)
        target_id = target.id if target else (request.target_id or "unresolved")
        spec = FirmwareSpec(
            target_id=target_id,
            project_name=request.project_name,
            description=request.description,
            language=request.language or (target.language if target else None),
            framework=request.framework or (target.framework if target else None),
            features=request.features or self._infer_features(request.description),
            pins=request.pins,
            peripherals=request.peripherals,
            include_tests=request.include_tests,
            safety_mode=request.safety_mode,
            previous_code=request.previous_code,
            requested_files=request.requested_files,
        )
        job_id = f"fw_{uuid.uuid4().hex}"
        now = _now()
        questions = [] if target else [
            "Which exact board/MCU target should be used?",
            "Choose one of the registered target IDs: " + ", ".join(item.id for item in TARGETS),
        ]
        assumptions = ["Only registered target profiles can generate code; unsupported boards are not guessed."]
        job = FirmwareJob(
            job_id=job_id,
            owner_id=owner_id,
            target=target,
            spec=spec,
            status=FirmwareStatus.needs_input if target is None else FirmwareStatus.generated,
            validation=FirmwareValidation(status="not_run"),
            questions=questions,
            assumptions=assumptions,
            created_at=now,
            updated_at=now,
        )
        self.store.save(job)
        if target is None:
            return job

        try:
            cache_key = _hash({"request": request.model_dump(mode="json"), "target": target.id})
            cached = None if self.engine.llm.config.enabled else self._cache.get(cache_key)
            if cached:
                generated, metadata = cached
            else:
                generated, metadata = await self.engine.generate(request, target, spec, conversation_key=conversation_key or (f"owner:{owner_id}" if owner_id is not None else None))
                if not self.engine.llm.config.enabled:
                    self._cache[cache_key] = (generated, metadata)
            validation = validate_firmware(target, spec, generated.files)
            extra_checks, security_findings, dependency_items = analysis_checks(spec, target, generated.files)
            validation.checks.extend(extra_checks)
            if any(check.status == "failed" for check in extra_checks):
                validation.status = "failed"
            elif validation.status == "passed" and any(check.status == "warning" for check in extra_checks):
                validation.status = "warnings"
            job.validation = validation
            job.dependencies = dependency_items
            job.security_findings = security_findings
            job.resource_estimate = estimate_resources(generated.files, target, spec)
            job.detected_language = target.language
            job.confidence_score = metadata.get("confidence_score", 0.5)
            job.rendering_hint = metadata.get("rendering_hint", "firmware-code")
            job.assumptions.extend(generated.assumptions)
            if any(item.severity == "critical" for item in security_findings):
                job.status = FirmwareStatus.failed
                job.error = "Critical security findings blocked firmware delivery. Review validation checks and remediate the generated request."
                job.updated_at = _now()
                self.store.save(job)
                return job
            job.files = metadata["files"]
            with tempfile.TemporaryDirectory(prefix=f"{job_id}_") as workspace:
                workspace_path = Path(workspace)
                for filename, content in generated.files.items():
                    destination = workspace_path / filename
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_text(content, encoding="utf-8")
                build_check, build_log = run_firmware_build(workspace_path, target)
                job.validation.checks.append(build_check)
                if build_check.status == "failed":
                    job.validation.status = "failed"
                elif build_check.status == "warning" and job.validation.status == "passed":
                    job.validation.status = "warnings"
                if build_log:
                    build_artifact = self.store.artifacts.write(job_id, "build.log", build_log, "firmware_build_log", download_base="/api/artifacts")
                    job.artifacts.append(_artifact_from_design_artifact(build_artifact))
            for filename, content in generated.files.items():
                kind = "firmware_bundle_metadata" if filename.endswith(".json") else "firmware_documentation" if filename.endswith(".md") else "firmware_source"
                stored = self.store.artifacts.write(job_id, filename, content, kind, download_base="/api/artifacts")
                job.artifacts.append(_artifact_from_design_artifact(stored))
            bundle = self._bundle(job_id, spec.project_name, generated.files)
            job.artifacts.append(_artifact_from_design_artifact(bundle))
            job.archive_artifact_id = bundle.id
            job.status = FirmwareStatus.needs_review if validation.status != "passed" or target.kind.value == "flight_controller" else FirmwareStatus.generated
        except Exception as exc:
            job.status = FirmwareStatus.failed
            job.error = str(exc)
            job.validation = FirmwareValidation(status="failed", checks=[])
        job.updated_at = _now()
        self.store.save(job)
        return job

    def _bundle(self, job_id: str, project_name: str, files: dict[str, str]):
        buffer = BytesIO()
        with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for filename, content in files.items():
                archive.writestr(f"{project_name}/{filename}", content)
        return self.store.artifacts.write(job_id, f"{project_name}.zip", buffer.getvalue(), "firmware_project_bundle", download_base="/api/artifacts")

    async def _enforce_rate_limit(self, owner_id: int | None) -> None:
        if owner_id is None:
            return
        limit = max(1, int(os.getenv("TRINITY_FIRMWARE_RATE_LIMIT_PER_MINUTE", "12")))
        now = time.monotonic()
        async with self._rate_lock:
            history = self._requests.setdefault(owner_id, deque())
            while history and history[0] <= now - 60:
                history.popleft()
            if len(history) >= limit:
                raise FirmwareRateLimitError("Firmware generation rate limit exceeded; try again later.")
            history.append(now)

    def _infer_features(self, description: str) -> list[str]:
        text = description.lower()
        known = ["gpio", "uart", "i2c", "spi", "adc", "pwm", "wifi", "bluetooth", "ble", "usb", "can", "imu", "gps", "barometer", "motor", "telemetry"]
        return [feature for feature in known if feature in text] or ["heartbeat"]
