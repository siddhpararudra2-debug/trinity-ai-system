"""Optional local toolchain build validation for generated firmware projects."""
from __future__ import annotations

import os
import shlex
import shutil
import subprocess
from pathlib import Path

from app.firmware.models import FirmwareCheck, FirmwareTarget


_ALLOWED_BINARIES = {"cmake", "idf.py", "pio", "west", "make", "st-flash", "dfu-util"}


def run_firmware_build(project_dir: Path, target: FirmwareTarget) -> tuple[FirmwareCheck, str]:
    if os.getenv("TRINITY_ENABLE_FIRMWARE_BUILDS", "0") != "1":
        return FirmwareCheck(name="toolchain-build", status="skipped", message="Firmware builds are disabled; set TRINITY_ENABLE_FIRMWARE_BUILDS=1 in a trusted build worker."), ""
    try:
        command = shlex.split(target.build_command)
    except ValueError as exc:
        return FirmwareCheck(name="toolchain-build", status="failed", message=f"Invalid registered build command: {exc}"), ""
    if not command or any(token in {"&&", ";", "|", "||"} for token in command):
        return FirmwareCheck(name="toolchain-build", status="skipped", message="This target requires a framework-specific multi-step build runner and was not executed by the generic worker."), ""
    executable = command[0]
    if executable.startswith("./"):
        if not (project_dir / executable).is_file():
            return FirmwareCheck(name="toolchain-build", status="skipped", message=f"Registered build entrypoint {executable} is not included in the generated project."), ""
    elif executable not in _ALLOWED_BINARIES:
        return FirmwareCheck(name="toolchain-build", status="failed", message=f"Build executable {executable!r} is not on the firmware allowlist."), ""
    elif shutil.which(executable) is None:
        return FirmwareCheck(name="toolchain-build", status="skipped", message=f"Required build executable {executable!r} is not installed in this worker."), ""
    try:
        result = subprocess.run(command, cwd=project_dir, capture_output=True, text=True, timeout=300, check=False, env={"PATH": os.environ.get("PATH", "")})
    except (OSError, subprocess.TimeoutExpired) as exc:
        return FirmwareCheck(name="toolchain-build", status="failed", message=f"Firmware build could not complete: {exc}"), str(exc)
    log = f"$ {' '.join(command)}\n\nSTDOUT\n{result.stdout}\n\nSTDERR\n{result.stderr}\n"
    status = "passed" if result.returncode == 0 else "failed"
    message = "Target toolchain build completed." if result.returncode == 0 else f"Target toolchain returned exit code {result.returncode}."
    return FirmwareCheck(name="toolchain-build", status=status, message=message, details={"returncode": result.returncode}), log
