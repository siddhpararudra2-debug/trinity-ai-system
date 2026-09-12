"""
CAD Engine (PRD §12, §16).

Implements the platform-agnostic `generate / validate / export / preview`
interface. The concrete geometry backend used here is the pure-Python
local fallback (app.engines.cad.primitives) — swapping in CadQuery,
FreeCAD, or the Onshape adapter means writing a new module that
satisfies the same four methods; nothing above this layer changes.
"""
from __future__ import annotations

import tempfile
import uuid
from pathlib import Path
from typing import Any

from app.core.errors import GeometryValidationError, RequestValidationError
from app.engines.base import BaseEngine, EngineResult, ValidationResult
from app.engines.cad.builder import build_quadcopter_frame
from app.engines.cad.ir import QuadcopterFrameIR
from app.engines.cad.primitives import Mesh, write_binary_stl
from app.engines.cad.glb import write_glb
from app.engines.cad.validators import validate_quadcopter_frame

SUPPORTED_TYPES = {"quadcopter_frame"}
NOT_YET_SUPPORTED_FORMATS = {"step"}


class CADEngine(BaseEngine):
    name = "cad"
    version = "1.0"
    capabilities = ["generate", "validate", "export", "preview"]

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        if operation == "generate":
            return self.generate(parameters)
        raise RequestValidationError(f"CAD engine has no operation '{operation}'")

    # ------------------------------------------------------------ generate ---

    def generate(self, parameters: dict[str, Any]) -> EngineResult:
        part_type = parameters.get("type", "quadcopter_frame")
        if part_type not in SUPPORTED_TYPES:
            raise RequestValidationError(
                f"Unsupported CAD type '{part_type}'",
                details={"supported": sorted(SUPPORTED_TYPES)},
            )

        outputs: list[str] = parameters.get("outputs") or ["stl", "json"]
        ir = QuadcopterFrameIR.from_request(parameters.get("parameters", {}))
        mesh = build_quadcopter_frame(ir)

        ok, checks = self.validate(mesh, ir)
        if not ok:
            raise GeometryValidationError(
                "Generated geometry failed validation", details={"checks": checks}
            )

        pending_artifacts: list[tuple[str, str]] = []
        unavailable_formats = [f for f in outputs if f in NOT_YET_SUPPORTED_FORMATS]

        work_dir = Path(tempfile.mkdtemp(prefix="trinity_cad_"))
        base_name = f"{part_type}_{int(ir.parameters['overall_size'])}mm_{uuid.uuid4().hex[:8]}"

        if "stl" in outputs:
            stl_path = work_dir / f"{base_name}.stl"
            write_binary_stl(mesh, str(stl_path))
            pending_artifacts.append((str(stl_path), "stl"))

        if "glb" in outputs:
            glb_path = work_dir / f"{base_name}.glb"
            write_glb(mesh, glb_path)
            pending_artifacts.append((str(glb_path), "glb"))

        if "json" in outputs:
            json_path = work_dir / f"{base_name}.json"
            json_path.write_text(_ir_json(ir))
            pending_artifacts.append((str(json_path), "json"))

        result: dict[str, Any] = {
            "type": part_type,
            "spec": ir.to_dict(),
            "triangle_count": len(mesh.triangles),
            "bounding_box_mm": mesh.bounding_box(),
        }
        if unavailable_formats:
            result["unavailable_formats"] = {
                fmt: "CAD_KERNEL_UNAVAILABLE: STEP requires CadQuery/OpenCascade or another real CAD kernel."
                for fmt in unavailable_formats
            }

        return EngineResult(
            success=True,
            engine=self.name,
            operation="generate",
            result=result,
            pending_artifacts=pending_artifacts,
            validation=ValidationResult(status="VALIDATED", checks=checks),
        )

    # ------------------------------------------------------------ validate ---

    def validate(self, mesh: Mesh, ir: QuadcopterFrameIR) -> tuple[bool, dict[str, Any]]:
        return validate_quadcopter_frame(ir, mesh)

    # -------------------------------------------------------------- export ---

    def export(self, mesh: Mesh, formats: list[str], out_dir: Path, base_name: str) -> list[Path]:
        paths: list[Path] = []
        if "stl" in formats:
            p = out_dir / f"{base_name}.stl"
            write_binary_stl(mesh, str(p))
            paths.append(p)
        return paths

    # ------------------------------------------------------------- preview ---

    def preview(self, mesh: Mesh) -> dict[str, Any]:
        (min_c, max_c) = mesh.bounding_box()
        return {
            "triangle_count": len(mesh.triangles),
            "bounding_box_mm": {"min": min_c, "max": max_c},
        }


def _ir_json(ir: QuadcopterFrameIR) -> str:
    import json

    return json.dumps(ir.to_dict(), indent=2)
