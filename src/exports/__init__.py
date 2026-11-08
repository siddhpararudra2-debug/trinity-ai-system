"""Export utilities (PRD §24, §31).

Convenience functions for exporting designs in standard formats:
- STEP/STL for CAD models
- Gerber for PCB designs
- BOM CSV for component lists
- Firmware zip for firmware source code

Each function wraps the corresponding engine operation and returns
artifacts ready for the artifact manager.
"""

from __future__ import annotations

import csv
import tempfile
from pathlib import Path
from typing import Any

from src.router import Router
from src.engines.registry import bootstrap_engines, registry


def bootstrap():
    """Ensure engines are bootstrapped before exporting."""
    bootstrap_engines()


def export_cad_stl(cad_type: str = "quadcopter_frame", **parameters: Any) -> dict[str, Any]:
    """Export a CAD part as STL.

    Args:
        cad_type: Part type name (e.g. "quadcopter_frame", "enclosure").
        **parameters: Engine generate parameters.

    Returns:
        dict with ``status`` and either ``artifact_paths`` (on success)
        or ``error``.
    """
    bootstrap()
    router = Router(registry)

    result = router.execute_simple("cad", "generate", {
        "type": cad_type,
        "parameters": parameters,
        "outputs": ["stl"],
    })

    if result["status"] != "completed":
        return {"status": "error", "error": result.get("error", "Unknown error")}

    result_dict = result["result"]
    # STL file path is in pending_artifacts as (path, type) tuples
    pending = result_dict.get("pending_artifacts", [])
    stl_paths = [p[0] for p in pending if len(p) > 1 and p[1] == "stl"]

    if not stl_paths:
        return {
            "status": "error",
            "error": "STL not produced — engine did not write an STL file",
        }

    return {
        "status": "completed",
        "artifact_paths": stl_paths,
        "format": "stl",
    }


def export_cad_step(cad_type: str = "quadcopter_frame", **parameters: Any) -> dict[str, Any]:
    """Export a CAD part as STEP.

    Note: STEP export requires an optional CAD kernel. If a kernel is
    not available, the unavailable reason is reported.

    Args:
        cad_type: Part type name.
        **parameters: Engine generate parameters.

    Returns:
        dict with ``status`` and either ``artifact_paths`` (on success)
        or ``error``.
    """
    bootstrap()
    router = Router(registry)

    result = router.execute_simple("cad", "generate", {
        "type": cad_type,
        "parameters": parameters,
        "outputs": ["step"],
    })

    if result["status"] != "completed":
        return {"status": "error", "error": result.get("error", "Unknown error")}

    result_dict = result["result"]
    step_available = result_dict.get("step_available", False)

    if not step_available:
        step_reason = result_dict.get("step_reason", "STEP writer not available")
        return {
            "status": "unavailable",
            "error": step_reason,
        }

    # In a full implementation, step_path would come from the engine.
    # For now return a status indicating STEP is conditionally available.
    return {
        "status": "conditional",
        "error": "STEP export requires a CAD kernel; not available in pure-Python fallback",
    }


def export_pcb_gerber(components: list[dict[str, Any]] | None = None,
                      netlist: dict[str, Any] | None = None,
                      **parameters: Any) -> dict[str, Any]:
    """Export a PCB design as Gerber files.

    Args:
        components: List of component dicts with ref, value, type, nets.
        netlist: Dict of net connections.
        **parameters: Board parameters (width_mm, height_mm, thickness_mm).

    Returns:
        dict with ``status`` and either ``artifact_paths`` (on success)
        or ``error``.
    """
    bootstrap()
    router = Router(registry)

    params: dict[str, Any] = {"width_mm": 50.0, "height_mm": 40.0, "thickness_mm": 1.6}
    params.update(parameters)

    if components is not None:
        result = router.execute_simple("pcb", "export", {
            "components": components,
            **params,
        })
    elif netlist is not None:
        result = router.execute_simple("pcb", "export", {
            "netlist": netlist,
            **params,
        })
    else:
        result = router.execute_simple("pcb", "export", params)

    if result["status"] != "completed":
        return {"status": "error", "error": result.get("error", "Unknown error")}

    result_dict = result["result"]
    gerber_files = result_dict.get("gerber_files", {})

    if not gerber_files:
        return {
            "status": "error",
            "error": "No Gerber files produced",
        }

    # Return the file names as artifact paths
    artifact_paths = list(gerber_files.keys())

    return {
        "status": "completed",
        "artifact_paths": artifact_paths,
        "format": "gerber",
        "gerber_info": gerber_files,
    }


def export_bom(components: list[dict[str, Any]]) -> dict[str, Any]:
    """Export a BOM (Bill of Materials) CSV for the given components.

    Args:
        components: List of component dicts with ref, value, type, nets.

    Returns:
        dict with ``status`` and either ``artifact_paths`` (CSV file path)
        or ``error``.
    """
    bootstrap()
    work_dir = Path(tempfile.mkdtemp(prefix="trinity_bom_"))
    csv_path = work_dir / "bom.csv"

    try:
        with open(csv_path, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(["Ref", "Value", "Type", "Nets"])
            for comp in components:
                ref = comp.get("ref", "")
                value = comp.get("value", "")
                part_type = comp.get("type", "")
                nets = ", ".join(comp.get("nets", []))
                writer.writerow([ref, value, part_type, nets])
    except Exception as exc:  # noqa: BLE001 - export failures are returned
        return {"status": "error", "error": f"BOM export failed: {exc}"}

    return {
        "status": "completed",
        "artifact_paths": [str(csv_path)],
        "format": "csv",
    }


def export_firmware_zip(project_id: str, mcu: str = "ESP32-WROOM-32",
                        features: list[str] | None = None) -> dict[str, Any]:
    """Export firmware source as a zip-like structure.

    Args:
        project_id: Unique project identifier.
        mcu: Microcontroller unit identifier.
        features: List of peripheral features (uart, spi, i2c, etc.).

    Returns:
        dict with ``status`` and either ``artifact_paths`` (zip path or
        source files) or ``error``.
    """
    bootstrap()
    router = Router(registry)

    result = router.execute_simple("firmware", "generate", {
        "mcu": mcu,
        "project_id": project_id,
        "features": features or ["uart", "spi"],
    })

    if result["status"] != "completed":
        return {"status": "error", "error": result.get("error", "Unknown error")}

    result_dict = result["result"]
    source_text = result_dict.get("engine_result", {}).get("source_text", "")

    # Write the source to a temp file (simulating a zip entry)
    work_dir = Path(tempfile.mkdtemp(prefix="trinity_firmware_"))
    source_path = work_dir / f"firmware_{project_id}.c"
    try:
        source_path.write_text(source_text, encoding="utf-8")
    except Exception as exc:  # noqa: BLE001 - export failures are returned
        return {"status": "error", "error": f"Firmware write failed: {exc}"}

    return {
        "status": "completed",
        "artifact_paths": [str(source_path)],
        "format": "firmware_source",
    }