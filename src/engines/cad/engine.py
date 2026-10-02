"""
CAD Engine (PRD §12, §16).

Implements the platform-agnostic `generate / validate / export / preview`
interface. The concrete geometry backend used here is the pure-Python
local fallback (src.engines.cad.primitives) — swapping in CadQuery,
FreeCAD, or the Onshape adapter means writing a new module that
satisfies the same four methods; nothing above this layer changes.

Part types are dispatched through the registry in
src.engines.cad.parts, so this engine never hardcodes a specific
part's IR, builder, validator or filename scheme.
"""

from __future__ import annotations

import tempfile
import uuid
from pathlib import Path
from typing import Any

from src.core.errors import GeometryValidationError, RequestValidationError
from src.engines.base import BaseEngine, EngineResult, ValidationResult
from src.engines.cad.glb import write_glb
from src.engines.cad.ir import PartIR
from src.engines.cad.parts import DEFAULT_PART_TYPE, PartDefinition, get_part
from src.engines.cad.primitives import Mesh, write_binary_stl

NOT_YET_SUPPORTED_FORMATS = {"step"}


class CADEngine(BaseEngine):
    name = "cad"
    version = "1.0"
    capabilities = ("generate", "validate", "export", "preview")

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        if operation == "generate":
            return self.generate(parameters)
        raise RequestValidationError(f"CAD engine has no operation '{operation}'")

    # ------------------------------------------------------------ generate ---

    def generate(self, parameters: dict[str, Any]) -> EngineResult:
        part = get_part(parameters.get("type", DEFAULT_PART_TYPE))

        outputs: list[str] = parameters.get("outputs") or ["stl", "json"]
        ir = part.ir_class.from_request(parameters.get("parameters", {}))
        mesh = part.builder(ir)

        ok, checks = self.validate(mesh, ir)
        if not ok:
            raise GeometryValidationError(
                "Generated geometry failed validation", details={"checks": checks}
            )

        pending_artifacts: list[tuple[str, str]] = []
        unavailable_formats = [f for f in outputs if f in NOT_YET_SUPPORTED_FORMATS]

        work_dir = Path(tempfile.mkdtemp(prefix="trinity_cad_"))
        base_name = f"{part.filename_stem(ir)}_{uuid.uuid4().hex[:8]}"

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
            "type": part.name,
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

    def validate(
        self, mesh: Mesh, ir: PartIR, part: PartDefinition | None = None
    ) -> tuple[bool, dict[str, Any]]:
        definition = part or get_part(ir.to_dict().get("type", ""))
        return definition.validator(ir, mesh)

    # -------------------------------------------------------------- export ---

    def export(
        self, mesh: Mesh, formats: list[str], out_dir: Path, base_name: str
    ) -> list[Path]:
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


def _ir_json(ir: PartIR) -> str:
    import json

    return json.dumps(ir.to_dict(), indent=2)
