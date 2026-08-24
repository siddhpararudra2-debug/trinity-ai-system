"""Static and opt-in toolchain validation for generated firmware projects."""
from __future__ import annotations

import re
from pathlib import Path

from app.firmware.models import FirmwareCheck, FirmwareSpec, FirmwareTarget, FirmwareValidation


_C_IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def validate_firmware(target: FirmwareTarget, spec: FirmwareSpec, files: dict[str, str]) -> FirmwareValidation:
    checks: list[FirmwareCheck] = []
    pin_names = [assignment.name for assignment in spec.pins]
    pin_values = [assignment.pin for assignment in spec.pins]
    duplicate_names = sorted({value for value in pin_names if pin_names.count(value) > 1})
    duplicate_pins = sorted({value for value in pin_values if pin_values.count(value) > 1})
    checks.append(FirmwareCheck(
        name="pin-uniqueness",
        status="failed" if duplicate_names or duplicate_pins else "passed",
        message="Pin names and physical pins are unique." if not duplicate_names and not duplicate_pins else "Duplicate pin names or physical pins were supplied.",
        details={"duplicate_names": duplicate_names, "duplicate_pins": duplicate_pins},
    ))

    unsupported = sorted({peripheral.kind.lower() for peripheral in spec.peripherals if peripheral.kind.lower() not in {value.lower() for value in target.supported_peripherals}})
    checks.append(FirmwareCheck(
        name="peripheral-compatibility",
        status="warning" if unsupported else "passed",
        message="All requested peripherals are listed in the target profile." if not unsupported else "Some peripherals are not listed in the target profile and require manual verification.",
        details={"unsupported": unsupported},
    ))

    expected = {"README.md", "TARGET.md", "firmware_spec.json"}
    if target.framework in {"ESP-IDF", "Pico SDK", "STM32 HAL"}:
        expected.add("CMakeLists.txt")
    elif target.framework == "Arduino AVR":
        expected.add("platformio.ini")
    elif target.framework == "Zephyr":
        expected.update({"prj.conf", "CMakeLists.txt"})
    missing = sorted(expected - files.keys())
    checks.append(FirmwareCheck(
        name="project-structure",
        status="failed" if missing else "passed",
        message="Framework project files are present." if not missing else "Required framework project files are missing.",
        details={"missing": missing},
    ))

    invalid_identifiers = sorted(value for value in pin_names if not _C_IDENT.match(value))
    checks.append(FirmwareCheck(
        name="identifier-safety",
        status="failed" if invalid_identifiers else "passed",
        message="Pin identifiers are safe C/C++ identifiers." if not invalid_identifiers else "Pin identifiers contain characters unsafe for generated source.",
        details={"invalid": invalid_identifiers},
    ))

    source_files = [content for path, content in files.items() if path.endswith((".c", ".cpp", ".h"))]
    brace_balance = all(content.count("{") == content.count("}") for content in source_files)
    checks.append(FirmwareCheck(
        name="source-braces",
        status="passed" if brace_balance else "failed",
        message="Generated C/C++ sources have balanced braces." if brace_balance else "Generated C/C++ sources have unbalanced braces.",
    ))

    if target.kind.value == "flight_controller":
        checks.append(FirmwareCheck(
            name="flight-safety",
            status="warning",
            message="Flight-controller output is an extension scaffold; SITL, bench, failsafe, arming, and actuator tests are required before flight.",
        ))
    if not spec.pins:
        checks.append(FirmwareCheck(
            name="explicit-pin-map",
            status="warning",
            message="No explicit pin map supplied; generated code intentionally avoids guessing hardware pins.",
        ))

    status = "failed" if any(check.status == "failed" for check in checks) else "warnings" if any(check.status == "warning" for check in checks) else "passed"
    return FirmwareValidation(status=status, checks=checks, toolchain=target.build_system)
