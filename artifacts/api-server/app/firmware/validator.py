"""Static and opt-in toolchain validation for generated firmware projects."""
from __future__ import annotations

import ast
import re
from pathlib import Path

from app.firmware.models import FirmwareCheck, FirmwareSpec, FirmwareTarget, FirmwareValidation

_C_IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def _syntax_check(path: str, content: str) -> FirmwareCheck:
    suffix = Path(path).suffix.lower()
    if suffix in {".py"}:
        try:
            ast.parse(content, filename=path)
            return FirmwareCheck(name=f"syntax:{path}", status="passed", message="Python syntax parsed successfully.", details={"path": path})
        except SyntaxError as exc:
            return FirmwareCheck(name=f"syntax:{path}", status="failed", message=f"Python syntax error: {exc.msg}", details={"path": path}, line=exc.lineno, severity="high")
    if suffix in {".c", ".h", ".cpp", ".cc", ".hpp", ".rs", ".v", ".sv", ".vhd", ".vhdl", ".s", ".S"}:
        pairs = {"{": "}", "(": ")", "[": "]"}
        stack: list[str] = []
        for line_number, line in enumerate(content.splitlines(), start=1):
            stripped = line.split("//", 1)[0].split("#", 1)[0]
            for char in stripped:
                if char in pairs:
                    stack.append(char)
                elif char in pairs.values():
                    if not stack or pairs[stack.pop()] != char:
                        return FirmwareCheck(name=f"syntax:{path}", status="failed", message=f"Unmatched delimiter in {path}.", details={"path": path}, line=line_number, severity="high")
        if stack:
            return FirmwareCheck(name=f"syntax:{path}", status="failed", message=f"Unclosed delimiter in {path}.", details={"path": path}, severity="high")
        return FirmwareCheck(name=f"syntax:{path}", status="passed", message="Lightweight delimiter syntax check passed.", details={"path": path})
    return FirmwareCheck(name=f"syntax:{path}", status="skipped", message="No local parser is configured for this file type.", details={"path": path})


def validate_firmware(target: FirmwareTarget, spec: FirmwareSpec, files: dict[str, str]) -> FirmwareValidation:
    checks: list[FirmwareCheck] = []
    pin_names = [assignment.name for assignment in spec.pins]
    pin_values = [assignment.pin for assignment in spec.pins]
    duplicate_names = sorted({value for value in pin_names if pin_names.count(value) > 1})
    duplicate_pins = sorted({value for value in pin_values if pin_values.count(value) > 1})
    checks.append(FirmwareCheck(name="pin-uniqueness", status="failed" if duplicate_names or duplicate_pins else "passed", message="Pin names and physical pins are unique." if not duplicate_names and not duplicate_pins else "Duplicate pin names or physical pins were supplied.", details={"duplicate_names": duplicate_names, "duplicate_pins": duplicate_pins}, severity="high" if duplicate_names or duplicate_pins else "info"))

    supported = {value.lower() for value in target.supported_peripherals}
    unsupported = sorted({peripheral.kind.lower() for peripheral in spec.peripherals if peripheral.kind.lower() not in supported})
    checks.append(FirmwareCheck(name="peripheral-compatibility", status="warning" if unsupported else "passed", message="All requested peripherals are listed in the target profile." if not unsupported else "Some peripherals are not listed in the target profile and require manual verification.", details={"unsupported": unsupported}))

    expected = {"README.md", "TARGET.md", "firmware_spec.json"}
    framework = (target.framework or "").lower()
    if any(name in framework for name in ("esp-idf", "pico", "stm32", "bare metal", "linux")):
        expected.add("CMakeLists.txt" if "linux" not in framework else "Makefile")
    elif "arduino" in framework:
        expected.add("platformio.ini")
    elif "zephyr" in framework:
        expected.update({"prj.conf", "CMakeLists.txt"})
    elif "micropython" in framework:
        expected.add("main.py")
    elif "rust" in framework:
        expected.add("Cargo.toml")
    elif "fpga" in framework:
        expected.add("constraints/pins.pcf")
    missing = sorted(expected - files.keys())
    checks.append(FirmwareCheck(name="project-structure", status="failed" if missing else "passed", message="Framework project files are present." if not missing else "Required framework project files are missing.", details={"missing": missing}, severity="high" if missing else "info"))

    invalid_identifiers = sorted(value for value in pin_names if not _C_IDENT.match(value))
    checks.append(FirmwareCheck(name="identifier-safety", status="failed" if invalid_identifiers else "passed", message="Pin identifiers are safe generated identifiers." if not invalid_identifiers else "Pin identifiers contain characters unsafe for generated source.", details={"invalid": invalid_identifiers}, severity="high" if invalid_identifiers else "info"))

    checks.extend(_syntax_check(path, content) for path, content in files.items())

    if target.kind.value == "flight_controller":
        checks.append(FirmwareCheck(name="flight-safety", status="warning", message="Flight-controller output is an extension scaffold; SITL, bench, failsafe, arming, and actuator tests are required before flight.", severity="high"))
    if not spec.pins:
        checks.append(FirmwareCheck(name="explicit-pin-map", status="warning", message="No explicit pin map supplied; generated code intentionally avoids guessing hardware pins."))

    status = "failed" if any(check.status == "failed" for check in checks) else "warnings" if any(check.status == "warning" for check in checks) else "passed"
    return FirmwareValidation(status=status, checks=checks, toolchain=target.build_system)
