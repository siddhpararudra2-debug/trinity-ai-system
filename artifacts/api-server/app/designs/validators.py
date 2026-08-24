"""Validation helpers for generated design artifacts.

The server performs safe static validation by default. KiCad CLI validation is
opt-in and only runs when an administrator explicitly enables it.
"""
from __future__ import annotations

import ast
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from app.designs.models import CadDesignSpec, PcbDesignSpec, ValidationCheck, ValidationReport, ValidationStatus


def _report(checks: list[ValidationCheck], tool: str | None = None, version: str | None = None) -> ValidationReport:
    if any(check.status == "failed" for check in checks):
        status = ValidationStatus.failed
    elif any(check.status == "warning" for check in checks):
        status = ValidationStatus.warnings
    else:
        status = ValidationStatus.passed
    return ValidationReport(status=status, checks=checks, tool=tool, tool_version=version)


def validate_cad_spec(spec: CadDesignSpec) -> ValidationReport:
    checks: list[ValidationCheck] = []
    dims = spec.dimensions_mm
    for name, value in dims.items():
        checks.append(ValidationCheck(name=f"dimension:{name}", status="passed" if value > 0 else "failed", message=f"{name}={value} mm"))
    if spec.part_type == "l_bracket":
        arm1 = float(dims.get("arm1", 0))
        arm2 = float(dims.get("arm2", 0))
        ok = arm1 > spec.material_thickness_mm and arm2 > spec.material_thickness_mm
        checks.append(ValidationCheck(name="bracket-thickness", status="passed" if ok else "failed", message="Arm dimensions exceed material thickness." if ok else "Both bracket arms must exceed material thickness."))
    if spec.part_type == "housing":
        width = float(dims.get("width", 0))
        depth = float(dims.get("depth", 0))
        height = float(dims.get("height", 0))
        ok = min(width, depth, height) > 2 * spec.material_thickness_mm
        checks.append(ValidationCheck(name="housing-wall", status="passed" if ok else "failed", message="Housing dimensions leave room for walls." if ok else "Housing dimensions must exceed twice the wall thickness."))
    for index, hole in enumerate(spec.holes):
        max_x = float(dims.get("arm1", dims.get("width", 0)))
        max_y = float(dims.get("arm2", dims.get("depth", 0)))
        inside = 0 <= hole.x_mm <= max_x and 0 <= hole.y_mm <= max_y and hole.diameter_mm < spec.material_thickness_mm * 4
        checks.append(ValidationCheck(name=f"hole:{index + 1}", status="passed" if inside else "failed", message="Hole is within the supplied envelope." if inside else "Hole position or diameter is outside the supplied envelope."))
    if not checks:
        checks.append(ValidationCheck(name="cad-spec", status="warning", message="No explicit dimensions were supplied; defaults require user review."))
    return _report(checks, tool="trinity-static-cad-validator", version="1.0")


def validate_fusion_script(script: str) -> ValidationReport:
    checks: list[ValidationCheck] = []
    try:
        ast.parse(script)
        checks.append(ValidationCheck(name="python-ast", status="passed", message="Fusion script parses as valid Python."))
    except SyntaxError as exc:
        checks.append(ValidationCheck(name="python-ast", status="failed", message=f"Fusion script has a syntax error: {exc}"))
        return _report(checks, tool="python-ast", version="3")
    forbidden = ["subprocess", "os.system", "eval(", "exec(", "__import__"]
    found = [token for token in forbidden if token in script]
    checks.append(ValidationCheck(name="script-policy", status="failed" if found else "passed", message="Generated script contains forbidden execution primitives." if found else "Generated script contains no server-side execution primitives.", details={"matches": found}))
    checks.append(ValidationCheck(name="fusion-entrypoint", status="passed" if "def run(context):" in script else "warning", message="Fusion run(context) entrypoint found." if "def run(context):" in script else "Fusion run(context) entrypoint is missing."))
    return _report(checks, tool="python-ast", version="3")


def validate_pcb_spec(spec: PcbDesignSpec) -> ValidationReport:
    checks: list[ValidationCheck] = []
    checks.append(ValidationCheck(name="board-size", status="passed" if spec.width_mm > 0 and spec.height_mm > 0 else "failed", message=f"Board outline: {spec.width_mm} mm × {spec.height_mm} mm."))
    refs = [component.reference for component in spec.components]
    duplicates = sorted({ref for ref in refs if refs.count(ref) > 1})
    checks.append(ValidationCheck(name="unique-references", status="failed" if duplicates else "passed", message="Component references are unique." if not duplicates else "Duplicate component references found.", details={"duplicates": duplicates}))
    known_refs = set(refs)
    unknown_connections = sorted({connection for net in spec.nets for connection in net.connections if connection not in known_refs and connection not in {"USB", "ESP32"}})
    checks.append(ValidationCheck(name="net-references", status="failed" if unknown_connections else "passed", message="All net endpoints resolve to declared components." if not unknown_connections else "Some net endpoints do not resolve to declared components.", details={"unknown": unknown_connections}))
    for component in spec.components:
        ok = bool(component.symbol and component.footprint)
        checks.append(ValidationCheck(name=f"library:{component.reference}", status="passed" if ok else "failed", message=f"{component.reference} has a symbol and footprint." if ok else f"{component.reference} is missing a symbol or footprint."))
    return _report(checks, tool="trinity-static-pcb-validator", version="1.0")


def validate_kicad_text(schematic: str, pcb: str) -> ValidationReport:
    checks: list[ValidationCheck] = []
    for name, content, root in (("schematic", schematic, "(kicad_sch"), ("pcb", pcb, "(kicad_pcb")):
        balanced = content.count("(") == content.count(")")
        has_root = content.lstrip().startswith(root)
        ok = balanced and has_root
        checks.append(ValidationCheck(name=f"{name}-sexpr", status="passed" if ok else "failed", message=f"{name} has a balanced KiCad S-expression root." if ok else f"{name} is not a balanced KiCad S-expression document."))
    return _report(checks, tool="trinity-static-kicad-validator", version="1.0")


def run_kicad_cli_validation(project_dir: Path, schematic_name: str, pcb_name: str) -> ValidationReport:
    """Run KiCad ERC/DRC when kicad-cli is installed and explicitly enabled."""
    checks: list[ValidationCheck] = []
    executable = shutil.which("kicad-cli")
    if os.getenv("TRINITY_ENABLE_KICAD_CLI", "0") != "1":
        return _report([ValidationCheck(name="kicad-cli", status="skipped", message="KiCad CLI validation is disabled; set TRINITY_ENABLE_KICAD_CLI=1 in a trusted worker.")], tool="kicad-cli", version=None)
    if not executable:
        return _report([ValidationCheck(name="kicad-cli", status="warning", message="KiCad CLI is not installed; static validation was used instead.")], tool="kicad-cli", version=None)

    try:
        version = subprocess.run([executable, "version"], cwd=project_dir, capture_output=True, text=True, timeout=15, check=False).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        version = None
    for command, filename in (([executable, "sch", "erc", "--exit-code-violations", str(project_dir / schematic_name)], "erc"), ([executable, "pcb", "drc", "--exit-code-violations", str(project_dir / pcb_name)], "drc")):
        try:
            result = subprocess.run(command, cwd=project_dir, capture_output=True, text=True, timeout=60, check=False)
            checks.append(ValidationCheck(name=f"kicad-{filename}", status="passed" if result.returncode == 0 else "failed", message=f"KiCad {filename.upper()} completed." if result.returncode == 0 else f"KiCad {filename.upper()} reported violations.", details={"returncode": result.returncode, "stdout": result.stdout[-4000:], "stderr": result.stderr[-4000:]}))
        except (OSError, subprocess.TimeoutExpired) as exc:
            checks.append(ValidationCheck(name=f"kicad-{filename}", status="failed", message=f"KiCad {filename.upper()} could not complete: {exc}"))
    return _report(checks, tool="kicad-cli", version=version)
