"""Native engine bridge (Item 1, Stage 4b).

Routes the six scaffolded engines (pcb, firmware, vision, simulation,
research, robotics) to `Trinity.exe --exec`: one JSON EngineRequest on
stdin, one JSON EngineResult envelope on stdout.

Design (see plan):
- D1: this is a BaseEngine subclass, so job_manager.run_sync() works
  unchanged — job rows, pending_artifacts persistence, validation
  storage, no cache pollution (caching is math/cad-only).
- D2: describe() reports status="bridged" (neither live-local nor stub).
- D3: unmapped ops raise CapabilityUnavailableError, exactly as the
  scaffold does today. No faked coverage.

Activation: TRINITY_NATIVE_EXEC must point at an existing Trinity.exe.
Otherwise registry.py keeps the ScaffoldEngine registrations.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import uuid
from pathlib import Path
from typing import Any

from src.core.errors import CapabilityUnavailableError, EngineExecutionError
from src.engines.base import BaseEngine, EngineResult, ValidationResult

EXEC_ENV_VAR = "TRINITY_NATIVE_EXEC"
EXEC_TIMEOUT_S = 120
EXEC_MAX_STDOUT = 50 * 1024 * 1024
STDERR_TAIL = 2000

# Engines the bridge can serve, with the same capabilities the scaffolds
# advertise so describe() shape is unchanged apart from status.
NATIVE_ENGINES: dict[str, list[str]] = {
    "pcb": ["inspect", "validate", "generate", "export"],
    # The firmware chain the native engine actually implements. `create` is
    # the entry point; the rest are the steps build needs before it can run.
    # The old ["create", "build", "test", "compile"] list advertised build
    # while leaving it unreachable, because build requires an MCU and
    # generated sources that no bridged op could supply.
    "firmware": [
        "create",
        "select_mcu",
        "configure_pin",
        "configure_peripheral",
        "generate",
        "validate",
        "build",
    ],
    "vision": ["image_inspect", "ocr", "document_parse", "geometry_extract"],
    "research": ["search"],
    "simulation": ["simulate"],
    "robotics": ["kinematics", "trajectory_plan"],
}

# Python op -> native op. Scaffolds never had parameter contracts, so each
# mapped op defines a fresh one (mirroring --selftest shapes). Anything not
# listed here stays 501: firmware/test, firmware/compile, pcb/inspect,
# vision/ocr, vision/document_parse, vision/geometry_extract.
OP_MAP: dict[str, dict[str, str]] = {
    "research": {"search": "search"},
    "robotics": {
        "kinematics": "forward_kinematics",
        "trajectory_plan": "plan_trajectory",
    },
    "simulation": {"simulate": "simulate_linear_motion"},
    # Single-shot by design: each step takes the project returned by the
    # previous one and returns it upgraded, so no cross-call state is kept.
    "firmware": {
        "create": "create_project",
        "select_mcu": "select_mcu",
        "configure_pin": "configure_pin",
        "configure_peripheral": "configure_peripheral",
        "generate": "generate_firmware",
        "validate": "validate_project",
        "build": "build",
    },
    "pcb": {
        "validate": "validate_design",
        "generate": "create_board",
        "export": "export",
    },
    "vision": {"image_inspect": "load_image"},
}

# Native ErrorCode names (Error.hpp) -> Python codes (errors.py). NOT
# mechanical: EngineNotFoundError -> "engine_not_found" (no _error suffix),
# so all eight are spelled out.
ERROR_CODE_MAP: dict[str, str] = {
    "TrinityError": "trinity_error",
    "RequestValidationError": "request_validation_error",
    "EngineNotFoundError": "engine_not_found",
    "EngineExecutionError": "engine_execution_error",
    "GeometryValidationError": "geometry_validation_error",
    "JobNotFoundError": "job_not_found",
    "ArtifactNotFoundError": "artifact_not_found",
    "CapabilityUnavailableError": "capability_unavailable",
}

# Native validation statuses -> Python ValidationLevel. Python has no
# INVALID (schemas.py), so it becomes FAILED.
STATUS_MAP: dict[str, str] = {
    "GENERATED": "GENERATED",
    "VALIDATED": "VALIDATED",
    "VERIFIED": "VERIFIED",
    "INVALID": "FAILED",
    "FAILED": "FAILED",
}


def native_exe_path() -> str | None:
    """Resolved Trinity.exe path, or None when the bridge is unavailable."""
    exe = os.getenv(EXEC_ENV_VAR, "")
    if exe and Path(exe).is_file():
        return exe
    return None


def _map_error(entry: dict[str, Any]) -> dict[str, Any]:
    raw_code = str(entry.get("code", ""))
    code = ERROR_CODE_MAP.get(raw_code, "engine_execution_error")
    details = entry.get("details")
    details = dict(details) if isinstance(details, dict) else {}
    if raw_code not in ERROR_CODE_MAP:
        details["native_code"] = raw_code
    return {
        "code": code,
        "message": str(entry.get("message", "native engine failed")),
        "details": details,
    }


def _map_validation(envelope: dict[str, Any]) -> ValidationResult | None:
    raw = envelope.get("validation")
    if not isinstance(raw, dict):
        return None
    status = STATUS_MAP.get(str(raw.get("status", "INVALID")), "FAILED")
    checks = raw.get("checks")
    return ValidationResult(
        status=status, checks=dict(checks) if isinstance(checks, dict) else {}
    )


class NativeBridgeEngine(BaseEngine):
    """Executes one operation via Trinity.exe --exec (stdin JSON -> stdout JSON)."""

    def __init__(self, name: str, capabilities: list[str]) -> None:
        self.name, self.capabilities = name, capabilities
        self.version = "0.1"

    def describe(self) -> dict[str, Any]:
        return {**super().describe(), "status": "bridged"}

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        native_op = OP_MAP.get(self.name, {}).get(operation)
        if native_op is None:
            raise CapabilityUnavailableError(
                f"The {self.name} engine has no bridged implementation for "
                f"'{operation}' yet",
                details={
                    "engine": self.name,
                    "operation": operation,
                    "status": "bridged",
                },
            )
        exe = native_exe_path()
        if exe is None:
            raise EngineExecutionError(
                "Native backend is not available",
                details={"env_var": EXEC_ENV_VAR, "engine": self.name},
            )
        request = {
            "request_id": uuid.uuid4().hex,
            "engine": self.name,
            "operation": native_op,
            "parameters": parameters,
        }
        # Isolated storage root: the child must never touch Python's SQLite
        # files (schema-mismatch risk). Its DB/logs die with this temp dir.
        # No state dir: the bridge is single-shot by design, every op fully
        # parameterized, so successive --exec processes share nothing.
        native_tmp = tempfile.mkdtemp(prefix="trinity_native_")
        try:
            proc = self._invoke(exe, request, native_tmp, operation)
            envelope = self._parse_envelope(proc, operation)
            pending = self._collect_artifacts(envelope, operation)
        finally:
            shutil.rmtree(native_tmp, ignore_errors=True)
        raw_errors = envelope.get("errors")
        errors = (
            [_map_error(e) for e in raw_errors if isinstance(e, dict)]
            if isinstance(raw_errors, list)
            else []
        )
        return EngineResult(
            success=bool(envelope.get("success", False)),
            engine=self.name,
            operation=operation,
            result=(
                dict(envelope["result"])
                if isinstance(envelope.get("result"), dict)
                else {}
            ),
            pending_artifacts=pending,
            validation=_map_validation(envelope),
            errors=errors,
        )

    def _invoke(
        self, exe: str, request: dict[str, Any], native_tmp: str, operation: str
    ):
        """Run the child with an isolated storage root. No shell, timeout, cap."""
        env = {**os.environ, "TRINITY_STORAGE_ROOT": native_tmp}
        try:
            proc = subprocess.run(
                [exe, "--exec"],
                input=json.dumps(request),
                capture_output=True,
                text=True,
                # Native stdout is UTF-8 JSON. Without this, text=True
                # decodes with the Windows locale codec (cp1252), which
                # raises UnicodeDecodeError on the first non-ASCII byte —
                # firmware source generation emits plenty — and the whole
                # envelope is lost. "replace" keeps a bad byte from taking
                # down the call; it can only affect one string value.
                encoding="utf-8",
                errors="replace",
                timeout=EXEC_TIMEOUT_S,
                shell=False,
                env=env,
            )
        except subprocess.TimeoutExpired as exc:
            raise EngineExecutionError(
                f"Native backend timed out after {EXEC_TIMEOUT_S}s",
                details={"engine": self.name, "operation": operation},
            ) from exc
        except OSError as exc:
            raise EngineExecutionError(
                f"Native backend could not be launched: {exc}",
                details={"engine": self.name, "exe": exe},
            ) from exc
        if len(proc.stdout) > EXEC_MAX_STDOUT:
            raise EngineExecutionError(
                "Native backend output exceeded size cap",
                details={"engine": self.name, "bytes": len(proc.stdout)},
            )
        return proc

    def _parse_envelope(self, proc, operation: str) -> dict[str, Any]:
        try:
            envelope = json.loads(proc.stdout)
        except json.JSONDecodeError as exc:
            raise EngineExecutionError(
                "Native backend returned unparseable output",
                details={
                    "engine": self.name,
                    "operation": operation,
                    "exit_code": proc.returncode,
                    "stderr_tail": proc.stderr[-STDERR_TAIL:],
                },
            ) from exc
        if not isinstance(envelope, dict):
            raise EngineExecutionError(
                "Native backend returned a non-object envelope",
                details={"engine": self.name, "operation": operation},
            )
        return envelope

    def _collect_artifacts(
        self, envelope: dict[str, Any], operation: str
    ) -> list[tuple[str, str]]:
        """Copy native files into one bridge scratch dir.

        run_sync() persists pending_artifacts via the artifact manager and
        then removes the first entry's parent dir (manager.py), so a single
        dir here means zero litter with no extra cleanup step.
        """
        scratch = Path(tempfile.mkdtemp(prefix="trinity_bridge_"))
        pending: list[tuple[str, str]] = []
        for key in ("pending_artifacts", "artifacts"):
            items = envelope.get(key)
            if not isinstance(items, list):
                continue
            for item in items:
                if not isinstance(item, dict):
                    continue
                path, artifact_type = str(item.get("path", "")), str(
                    item.get("type", "")
                )
                if not (path and artifact_type):
                    continue
                if not Path(path).is_file():
                    raise EngineExecutionError(
                        "Native backend reported a missing artifact file",
                        details={
                            "engine": self.name,
                            "operation": operation,
                            "path": path,
                        },
                    )
                dest = scratch / Path(path).name
                shutil.copyfile(path, dest)
                pending.append((str(dest), artifact_type))
        return pending
