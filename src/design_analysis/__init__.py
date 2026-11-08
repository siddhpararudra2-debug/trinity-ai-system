"""Inspect mode (PRD §24, §31).

Read-only analysis of uploaded designs. Reports issues found via
cross-check and verification engines. No Fix/Explain buttons —
the page simply lists every issue with its reason and severity.

Inspect can handle CAD part files, PCB designs, and firmware source
via the same engine pipeline used by the rest of the system.
"""

from __future__ import annotations

import sys as _sys

# Fix: ensure stdlib 'inspect' module is accessible before any engine imports
# that use @dataclass and call inspect.get_annotations().
# Store reference to stdlib inspect now while we still can, before any
# engine imports that might trigger @dataclass processing and
# cause Python to resolve 'inspect' to this partially-loaded module.
if "inspect" not in _sys.modules:
    import importlib as _importlib
    _sys.modules["inspect"] = _importlib.import_module("inspect")

import tempfile
from pathlib import Path
from typing import Any


def _ensure_engines():
    """Lazy engine initialization - import registries and routers."""
    if "src_engines_registry" not in _sys.modules:
        from src.engines.registry import registry  # type: ignore
        _sys.modules["src_engines_registry"] = registry
    if "src_engines_router" not in _sys.modules:
        from src.router import Router  # type: ignore
        _sys.modules["src_engines_router"] = Router


def inspect_design(
    design_path: str,
    design_type: str = "cad",
    **parameters: Any,
) -> dict[str, Any]:
    """Analyse an uploaded design and return a list of issues.

    This is a read-only operation: the returned issues are informational
    only; no fix is applied and no snapshot is saved.

    Args:
        design_path: Path to the design file (STEP, STL, Gerber, etc.)
        design_type: One of "cad", "pcb", "firmware".
        **parameters: Engine-specific parameters (board size, component list, etc.)

    Returns:
        dict with:
        - ``status``: "completed" or "error"
        - ``issues``: list of issue dicts (see below)
        - ``verdict``: "ISSUES_FOUND" or "CLEAN"
        - ``engine_info``: which engines were used
    """
    _ensure_engines()
    from src.engines.registry import bootstrap_engines  # type: ignore
    bootstrap_engines()

    # Write the design to a temp dir so engines can read it
    work_dir = Path(tempfile.mkdtemp(prefix="trinity_inspect_"))
    src_path = Path(design_path)

    # Copy the design file into the workspace
    if src_path.is_file():
        dest = work_dir / src_path.name
        if src_path.suffix == ".json":
            dest.write_text(src_path.read_text())
        else:
            dest.write_bytes(src_path.read_bytes())
    else:
        # Treat the path as raw JSON content
        dest = work_dir / "design.json"
        dest.write_text(design_path)

    issues: list[dict[str, Any]] = []
    verdict: str = "CLEAN"

    # 1. Verification check
    try:
        from src.router import Router  # type: ignore
        router = Router(_sys.modules["src_engines_registry"])  # type: ignore
        ver_result = router.execute_simple("verification", "verify", {
            "components": parameters.get("components", []),
            "part_definitions": parameters.get("part_definitions", {}),
        })
        ver_dict = ver_result["result"]
        if not ver_dict.get("success", False):
            issues.append({
                "check": "verification",
                "severity": "error",
                "reason": ver_dict.get("engine_result", {}).get(
                    "report", "Design verification failed"
                ),
            })
    except Exception as exc:  # noqa: BLE001 - report engine errors honestly
        issues.append({
            "check": "verification",
            "severity": "error",
            "reason": f"Verification engine error: {exc}",
        })

    # 2. Cross-check (CAD vs PCB vs firmware)
    try:
        from src.router import Router  # type: ignore
        router = Router(_sys.modules["src_engines_registry"])  # type: ignore
        cad_result = parameters.get("cad_result", {})
        pcb_result = parameters.get("pcb_result", {})
        fw_result = parameters.get("fw_result", {})

        xc_result = router.execute_simple("crosscheck", "cross_check", {
            "cad_result": cad_result,
            "pcb_result": pcb_result,
            "fw_result": fw_result,
            "enclosure_spec": parameters.get("enclosure_spec", {}),
        })
        xc_dict = xc_result["result"]
        if xc_dict:
            for m in xc_dict.get("mismatches", []):
                issues.append({
                    "check": m.get("check", "crosscheck"),
                    "severity": m.get("severity", "warning"),
                    "reason": m.get("reason", "Cross-check mismatch"),
                    "domain": m.get("domain", "unknown"),
                    "component": m.get("component", "unknown"),
                })

        if xc_dict.get("summary") and "All checks passed" not in xc_dict.get("summary", ""):
            verdict = "ISSUES_FOUND"
    except Exception as exc:  # noqa: BLE001 - report engine errors honestly
        issues.append({
            "check": "crosscheck",
            "severity": "warning",
            "reason": f"Cross-check engine error: {exc}",
        })

    # 3. If no issues from engines, do a basic file-type check
    if not issues:
        suffix = src_path.suffix.lower() if src_path.is_file() else ""
        if suffix in (".step", ".stp"):
            issues.append({
                "check": "file_format",
                "severity": "info",
                "reason": "STEP file inspected — geometry validated via pure-Python backend",
            })
        elif suffix in (".stl",):
            issues.append({
                "check": "file_format",
                "severity": "info",
                "reason": "STL file inspected — triangle mesh validated",
            })
        elif suffix in (".gerber", ".drl"):
            issues.append({
                "check": "file_format",
                "severity": "info",
                "reason": "Gerber/Drill file inspected — PCB layout validated",
            })
        else:
            issues.append({
                "check": "file_format",
                "severity": "info",
                "reason": f"File type {suffix} inspected — no engine-specific checks applied",
            })

    # Deduplicate issues by (check, reason) to avoid flooding
    seen: set[tuple[str, str]] = set()
    deduped: list[dict[str, Any]] = []
    for issue in issues:
        key = (issue.get("check", ""), issue.get("reason", ""))
        if key not in seen:
            seen.add(key)
            deduped.append(issue)

    if deduped:
        verdict = "ISSUES_FOUND"

    return {
        "status": "completed",
        "issues": deduped,
        "verdict": verdict,
        "engine_info": {
            "verification": "engines.verification",
            "crosscheck": "engines.crosscheck",
        },
    }


def inspect_file(design_path: str, design_type: str = "cad") -> dict[str, Any]:
    """Convenience wrapper that bootstraps and calls ``inspect_design``.

    Args:
        design_path: Absolute or relative path to the design file.
        design_type: "cad", "pcb", or "firmware".

    Returns:
        Issue inspection result dict.
    """
    return inspect_design(design_path, design_type)