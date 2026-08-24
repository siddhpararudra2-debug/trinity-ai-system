"""Firmware generation job service."""
from __future__ import annotations

import hashlib
import json
import re
import tempfile
import uuid
import zipfile
from datetime import datetime, timezone
from io import BytesIO
from pathlib import Path
from typing import Any

from app.designs.artifacts import ArtifactStore
from app.firmware.builds import run_firmware_build
from app.firmware.generator import FirmwareGenerator
from app.firmware.models import FirmwareArtifact, FirmwareJob, FirmwareRequest, FirmwareSpec, FirmwareStatus, FirmwareValidation
from app.firmware.registry import TARGETS, find_target
from app.firmware.validator import validate_firmware


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
    return datetime.now(timezone.utc)


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

    async def create(self, request: FirmwareRequest) -> FirmwareJob:
        target = find_target(request.target_id, request.description)
        target_id = target.id if target else (request.target_id or "unresolved")
        spec = FirmwareSpec(
            target_id=target_id,
            project_name=request.project_name,
            description=request.description,
            language=target.language if target else None,
            framework=target.framework if target else None,
            features=request.features or self._infer_features(request.description),
            pins=request.pins,
            peripherals=request.peripherals,
            include_tests=request.include_tests,
            safety_mode=request.safety_mode,
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
            generated = self.generator.generate(target, spec)
            validation = validate_firmware(target, spec, generated.files)
            job.validation = validation
            job.assumptions.extend(generated.assumptions)
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
            job.artifacts.append(_artifact_from_design_artifact(self._bundle(job_id, spec.project_name, generated.files)))
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

    def _infer_features(self, description: str) -> list[str]:
        text = description.lower()
        known = ["gpio", "uart", "i2c", "spi", "adc", "pwm", "wifi", "bluetooth", "ble", "usb", "can", "imu", "gps", "barometer", "motor", "telemetry"]
        return [feature for feature in known if feature in text] or ["heartbeat"]
