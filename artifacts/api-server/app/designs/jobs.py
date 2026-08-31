"""Design job orchestration for CAD and PCB generation."""
from __future__ import annotations

import hashlib
import json
import re
import uuid
import zipfile
from datetime import datetime, UTC
from io import BytesIO
from typing import Any

from app.designs.artifacts import ArtifactStore
from app.designs.models import (
    Artifact,
    CadDesignRequest,
    DesignJob,
    JobStatus,
    PcbDesignRequest,
    ValidationCheck,
    ValidationReport,
    ValidationStatus,
)
from app.designs.parsers import parse_cad, parse_pcb
from app.designs.validators import (
    run_kicad_cli_validation,
    validate_cad_spec,
    validate_fusion_script,
    validate_kicad_text,
    validate_pcb_spec,
)
from app.engines.maker_cad import MakerCadEngine
from app.engines.maker_pcb import MakerPcbEngine


class DesignJobStore:
    """Persistent JSON job metadata plus immutable files on disk."""

    def __init__(self, artifact_store: ArtifactStore | None = None) -> None:
        self.artifacts = artifact_store or ArtifactStore()
        self.jobs_dir = self.artifacts.root / "jobs"
        self.jobs_dir.mkdir(parents=True, exist_ok=True)

    def save(self, job: DesignJob) -> None:
        path = self.jobs_dir / f"{job.job_id}.json"
        path.write_text(job.model_dump_json(indent=2), encoding="utf-8")

    def get(self, job_id: str) -> DesignJob | None:
        if not re.fullmatch(r"(?:cad|pcb)_[a-f0-9]{32}", job_id):
            return None
        path = self.jobs_dir / f"{job_id}.json"
        if not path.is_file():
            return None
        try:
            return DesignJob.model_validate_json(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return None


def _now() -> datetime:
    return datetime.now(UTC)


def _new_id(prefix: str) -> str:
    return f"{prefix}_{uuid.uuid4().hex}"


def _hash(value: Any) -> str:
    raw = json.dumps(value, sort_keys=True, default=str).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()


def _combine_reports(*reports: ValidationReport) -> ValidationReport:
    checks = [check for report in reports for check in report.checks]
    failed = any(check.status == "failed" for check in checks)
    warnings = any(check.status == "warning" for check in checks)
    return ValidationReport(
        status=ValidationStatus.failed if failed else ValidationStatus.warnings if warnings else ValidationStatus.passed,
        checks=checks,
        tool="trinity-design-pipeline",
        tool_version="1.0",
    )


class DesignJobService:
    def __init__(self, store: DesignJobStore | None = None) -> None:
        self.store = store or DesignJobStore()
        self.cad = MakerCadEngine()
        self.pcb = MakerPcbEngine()

    async def create_cad(self, request: CadDesignRequest, owner_id: int | None = None) -> DesignJob:
        spec, questions, assumptions = parse_cad(request.description, request.parameters)
        if request.output_formats:
            spec.output_formats = request.output_formats
        job_id = _new_id("cad")
        now = _now()
        job = DesignJob(
            job_id=job_id,
            owner_id=owner_id,
            engine="maker_cad",
            status=JobStatus.generating,
            request_hash=_hash(request.model_dump()),
            spec=spec,
            questions=questions,
            assumptions=assumptions,
            created_at=now,
            updated_at=now,
        )
        self.store.save(job)
        try:
            spec_report = validate_cad_spec(spec)
            params = {
                "arm1": spec.dimensions_mm.get("arm1", 50.0),
                "arm2": spec.dimensions_mm.get("arm2", 50.0),
                "thick": spec.material_thickness_mm,
                "wbox": spec.dimensions_mm.get("width", 80.0),
                "dep": spec.dimensions_mm.get("depth", 60.0),
                "ht": spec.dimensions_mm.get("height", 40.0),
                "teeth": int(spec.dimensions_mm.get("teeth", 24)),
                "mod": spec.dimensions_mm.get("module", 1.5),
                "dia": spec.dimensions_mm.get("diameter", 20.0),
                "length": spec.dimensions_mm.get("length", 100.0),
            }
            generated = await self.cad.process(request.description, params)
            script = generated["script"]
            script_report = validate_fusion_script(script)
            job.validation = _combine_reports(spec_report, script_report)
            artifact = self.store.artifacts.write(job_id, generated.get("filename", "fusion_script.py"), script, "fusion_script")
            job.artifacts = [artifact]
            if any(fmt in {"step", "stl", "f3d"} for fmt in spec.output_formats):
                job.assumptions.append("STEP/STL/F3D export requires the Fusion desktop worker; only the Fusion script is available from this server job.")
                job.validation.checks.append(ValidationCheck(name="fusion-export", status="skipped", message="Fusion desktop export worker is not connected."))
                job.validation.status = ValidationStatus.warnings
            job.status = JobStatus.needs_review if questions or job.validation.status != ValidationStatus.passed else JobStatus.ready
            job.updated_at = _now()
            self.store.save(job)
            self.store.artifacts.register_manifest(job_id, job.engine, job.artifacts, job.validation)
            return job
        except Exception as exc:
            job.status = JobStatus.failed
            job.error = str(exc)
            job.validation = ValidationReport(status=ValidationStatus.failed, checks=[ValidationCheck(name="generation", status="failed", message=str(exc))])
            job.updated_at = _now()
            self.store.save(job)
            return job

    async def create_pcb(self, request: PcbDesignRequest, owner_id: int | None = None) -> DesignJob:
        spec, questions, assumptions = parse_pcb(request.description, request.components, request.spec, request.outputs)
        job_id = _new_id("pcb")
        now = _now()
        job = DesignJob(
            job_id=job_id,
            owner_id=owner_id,
            engine="maker_pcb",
            status=JobStatus.generating,
            request_hash=_hash(request.model_dump()),
            spec=spec,
            questions=questions,
            assumptions=assumptions,
            created_at=now,
            updated_at=now,
        )
        self.store.save(job)
        try:
            spec_report = validate_pcb_spec(spec)
            generated = await self.pcb.process(request.description, [component.value for component in spec.components], spec.model_dump(mode="json"))
            schematic = generated["sch_content"]
            pcb = generated["pcb_content"]
            text_report = validate_kicad_text(schematic, pcb)
            job.validation = _combine_reports(spec_report, text_report)
            schematic_artifact = self.store.artifacts.write(job_id, f"{generated.get('filename', 'trinity_board')}.kicad_sch", schematic, "kicad_schematic")
            pcb_artifact = self.store.artifacts.write(job_id, f"{generated.get('filename', 'trinity_board')}.kicad_pcb", pcb, "kicad_pcb")
            manifest_artifact = self.store.artifacts.write(job_id, "design-spec.json", spec.model_dump_json(indent=2), "design_spec", download_base="/api/artifacts")
            job.artifacts = [schematic_artifact, pcb_artifact, manifest_artifact]

            project_artifact = self._write_pcb_bundle(job_id, generated.get("filename", "trinity_board"), schematic, pcb, spec)
            job.artifacts.append(project_artifact)
            cli_report = run_kicad_cli_validation(self.store.artifacts.job_dir(job_id), schematic_artifact.filename, pcb_artifact.filename)
            if cli_report.checks and cli_report.checks[0].status != "skipped":
                job.validation = _combine_reports(job.validation, cli_report)
            if cli_report.status == ValidationStatus.warnings:
                job.assumptions.append("KiCad CLI validation was not available in this environment; static validation only was performed.")
                job.validation.status = ValidationStatus.warnings
            job.status = JobStatus.needs_review if questions or job.validation.status != ValidationStatus.passed else JobStatus.ready
            job.updated_at = _now()
            self.store.save(job)
            self.store.artifacts.register_manifest(job_id, job.engine, job.artifacts, job.validation)
            return job
        except Exception as exc:
            job.status = JobStatus.failed
            job.error = str(exc)
            job.validation = ValidationReport(status=ValidationStatus.failed, checks=[ValidationCheck(name="generation", status="failed", message=str(exc))])
            job.updated_at = _now()
            self.store.save(job)
            return job

    def _write_pcb_bundle(self, job_id: str, stem: str, schematic: str, pcb: str, spec: Any) -> Artifact:
        buffer = BytesIO()
        with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr(f"{stem}/{stem}.kicad_sch", schematic)
            archive.writestr(f"{stem}/{stem}.kicad_pcb", pcb)
            archive.writestr(f"{stem}/design-spec.json", spec.model_dump_json(indent=2))
            archive.writestr(f"{stem}/README.md", "# Trinity PCB project\n\nRun KiCad ERC/DRC before manufacturing. This bundle contains generated templates and validation metadata.\n")
        return self.store.artifacts.write(job_id, f"{stem}.zip", buffer.getvalue(), "kicad_project_bundle", download_base="/api/artifacts")
