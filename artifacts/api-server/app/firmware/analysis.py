"""Static firmware security, dependency, and resource analysis."""
from __future__ import annotations

import re

from app.firmware.knowledge import validate_pin_assignments
from app.firmware.models import DependencyStatus, FirmwareCheck, FirmwareSpec, FirmwareTarget, ResourceEstimate, SecurityFinding

_ALLOWED_DEPENDENCIES = {"arduino", "freertos", "zephyr", "embedded-hal", "cortex-m", "micropython", "px4", "vendor-hal", "pico-sdk", "esp-idf"}
_DANGEROUS_RULES = (
    (r"(?:private[_ -]?key|api[_ -]?key|password|secret)\s*[:=]\s*['\"][^'\"]+", "critical", "Hardcoded credential or secret detected.", "Inject secrets through a secure provisioning path."),
    (r"\b(strcpy|strcat|sprintf|gets)\s*\(", "high", "Unbounded C string operation detected.", "Use a bounded alternative and validate buffer capacity."),
    (r"__asm__.*(?:cpsid|cli)\b|\b(?:cpsid i|cli)\b", "high", "Interrupts may be disabled without a visible bounded critical section.", "Pair interrupt masking with a guaranteed restore path."),
    (r"\b(?:malloc|calloc|realloc|new)\s*\(", "medium", "Dynamic allocation detected in firmware output.", "Prefer static allocation or document a bounded allocator policy."),
    (r"while\s*\(\s*1\s*\)|for\s*\(\s*;\s*;\s*\)", "medium", "Unbounded loop detected; watchdog servicing was not proven.", "Document watchdog feeding or a bounded exit condition."),
    (r"(?:default[_ -]?password|debug[_ -]?backdoor|disable[_ -]?mpu|no[_ -]?stack[_ -]?canary)", "critical", "Safety or security hardening appears disabled.", "Remove the bypass and enable the platform security mechanism."),
)


def scan_security(files: dict[str, str]) -> list[SecurityFinding]:
    findings: list[SecurityFinding] = []
    for path, content in files.items():
        for line_number, line in enumerate(content.splitlines(), start=1):
            for pattern, severity, message, remediation in _DANGEROUS_RULES:
                if re.search(pattern, line, re.IGNORECASE):
                    findings.append(SecurityFinding(rule=pattern, severity=severity, message=f"{path}: {message}", line=line_number, remediation=remediation))
    return findings


def dependency_status(files: dict[str, str]) -> list[DependencyStatus]:
    text = "\n".join(files.values()).lower()
    observed = set()
    for candidate in _ALLOWED_DEPENDENCIES:
        if candidate.lower() in text:
            observed.add(candidate)
    result = [DependencyStatus(name=name, verified=True, reason="Recognized in the curated firmware dependency allowlist.") for name in sorted(observed)]
    unknown = sorted(set(re.findall(r"(?:#include\s*[<\"]([^>\"]+)|import\s+([A-Za-z0-9_-]+)|use\s+([A-Za-z0-9_-]+))", text)))
    if unknown and not result:
        result.append(DependencyStatus(name="unresolved-dependency", verified=False, reason="The generated project references dependencies that require manual verification."))
    return result


def estimate_resources(files: dict[str, str], target: FirmwareTarget, spec: FirmwareSpec) -> ResourceEstimate:
    source = "\n".join(files.values())
    code_bytes = len(source.encode("utf-8"))
    flash = int(code_bytes * 1.35 + len(spec.features) * 512 + len(spec.peripherals) * 768)
    ram = int(sum(max(1, len(line) // 24) for line in source.splitlines() if "static" in line or "global" in line) * 16 + len(spec.peripherals) * 256 + 1024)
    interrupt_count = len(re.findall(r"\b(?:ISR|interrupt|IRQ|handler|task|thread)\b", source, re.IGNORECASE))
    cpu = min(100.0, round(2.0 + interrupt_count * 1.5 + len(spec.peripherals) * 0.8, 1))
    return ResourceEstimate(
        flash_bytes=flash,
        ram_bytes=ram,
        cpu_percent=cpu,
        flash_percent=round(flash / target.flash_bytes * 100, 2) if target.flash_bytes else None,
        ram_percent=round(ram / target.ram_bytes * 100, 2) if target.ram_bytes else None,
        confidence=0.55,
        assumptions=["Estimate is based on generated source size and conservative feature/peripheral weights; compile with the target toolchain for actual usage."],
    )


def analysis_checks(spec: FirmwareSpec, target: FirmwareTarget, files: dict[str, str]) -> tuple[list[FirmwareCheck], list[SecurityFinding], list[DependencyStatus]]:
    checks: list[FirmwareCheck] = []
    findings = scan_security(files)
    dependencies = dependency_status(files)
    pin_findings = validate_pin_assignments(spec.pins, target.model_dump(mode="json"))
    checks.append(FirmwareCheck(name="security-hardening", status="failed" if any(item.severity == "critical" for item in findings) else "warning" if findings else "passed", message="No critical firmware security patterns detected." if not findings else "Security review findings require remediation before deployment.", details={"findings": [item.model_dump(mode="json") for item in findings]}, severity="critical" if any(item.severity == "critical" for item in findings) else "warning" if findings else "info"))
    checks.append(FirmwareCheck(name="dependency-allowlist", status="warning" if any(not item.verified for item in dependencies) else "passed", message="Dependencies are recognized or require manual verification.", details={"dependencies": [item.model_dump(mode="json") for item in dependencies]}))
    checks.append(FirmwareCheck(name="pin-map-validation", status="failed" if pin_findings else "passed", message="Pin assignments do not conflict with the known target profile." if not pin_findings else "Pin assignments contain conflicts or out-of-range pins.", details={"findings": pin_findings}))
    return checks, findings, dependencies
