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
from src.engines.cad.families import build_family
from src.engines.cad.ir import PartIR
from src.engines.cad.parts import DEFAULT_PART_TYPE, PartDefinition, get_part
from src.engines.cad.primitives import Mesh, write_binary_stl
from src.engines.cad.step import kernel_info, write_step

DEFAULT_OUTPUTS = ("stl", "json")

# Formats the pure-Python backend always writes. `step` is deliberately
# absent: it needs an optional CAD kernel, so its availability is decided
# per call, not baked in here.
ALWAYS_SUPPORTED_FORMATS = ("stl", "glb", "json")


def unsupported_formats() -> set[str]:
    """Formats this environment cannot write right now.

    `step` depends on an optional CAD kernel, so the answer is resolved at
    call time instead of being frozen into a constant. Everything else is
    pure Python and always available.
    """
    return set() if kernel_info()["available"] else {"step"}


def format_reason(fmt: str) -> str:
    """Why `fmt` is unavailable. Empty string when it is available."""
    if fmt == "step":
        return str(kernel_info().get("reason", ""))
    return ""


def step_unavailable_reason() -> str:
    return format_reason("step")


class CADEngine(BaseEngine):
    name = "cad"
    version = "1.0"
    capabilities = ("generate", "validate", "export", "preview", "families")

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        if operation == "generate":
            return self.generate(parameters)
        if operation == "validate":
            return self.execute_validate(parameters)
        if operation == "preview":
            return self.execute_preview(parameters)
        if operation == "export":
            return self.execute_export(parameters)
        if operation == "families":
            return self.execute_families(parameters)
        raise RequestValidationError(f"CAD engine has no operation '{operation}'")

    # ----------------------------------------- execute() operation routes ---

    def _build_from_execute_params(
        self, parameters: dict[str, Any]
    ) -> tuple[PartDefinition, PartIR, Mesh]:
        """Shared rebuild for validate/preview/export: no persisted state.

        Takes the same {type, parameters} shape as generate() so callers
        use one payload shape for every CAD operation.
        """
        part = get_part(parameters.get("type", DEFAULT_PART_TYPE))
        ir = part.ir_class.from_request(parameters.get("parameters", {}))
        mesh = part.builder(ir)
        return part, ir, mesh

    def execute_families(self, parameters: dict[str, Any]) -> EngineResult:
        """Parameter families: truth per member (M14).

        Success is about the OP, not the members: a family whose members
        contain bound-violating variants is still a truthful answer.
        """
        part = get_part(parameters.get("type", DEFAULT_PART_TYPE))
        selector = parameters.get("family") or {}
        family = build_family(
            part,
            parameters.get("parameters") or {},
            str(selector.get("parameter", "")),
            selector.get("values") or [],
        )
        return EngineResult(
            success=True,
            engine=self.name,
            operation="families",
            result=family,
        )

    def execute_validate(self, parameters: dict[str, Any]) -> EngineResult:
        part, ir, mesh = self._build_from_execute_params(parameters)
        ok, checks = self.validate(mesh, ir, part)
        return EngineResult(
            success=ok,
            engine=self.name,
            operation="validate",
            result={
                "type": part.name,
                "spec": ir.to_dict(),
                "triangle_count": len(mesh.triangles),
                "bounding_box_mm": mesh.bounding_box(),
                "checks": checks,
            },
            validation=ValidationResult(
                status="VALIDATED" if ok else "FAILED", checks=checks
            ),
        )

    def execute_preview(self, parameters: dict[str, Any]) -> EngineResult:
        part, ir, mesh = self._build_from_execute_params(parameters)
        return EngineResult(
            success=True,
            engine=self.name,
            operation="preview",
            result={"type": part.name, **self.preview(mesh)},
        )

    def execute_export(self, parameters: dict[str, Any]) -> EngineResult:
        part, ir, mesh = self._build_from_execute_params(parameters)
        ok, checks = self.validate(mesh, ir, part)
        if not ok:
            raise GeometryValidationError(
                "Generated geometry failed validation", details={"checks": checks}
            )
        outputs: list[str] = parameters.get("outputs", parameters.get("formats")) or list(
            DEFAULT_OUTPUTS
        )
        work_dir = Path(tempfile.mkdtemp(prefix="trinity_cad_"))
        base_name = f"{part.filename_stem(ir)}_{uuid.uuid4().hex[:8]}"
        written, step_stats = self.export(mesh, outputs, work_dir, base_name, ir)
        result: dict[str, Any] = {
            "type": part.name,
            "spec": ir.to_dict(),
            "triangle_count": len(mesh.triangles),
            "bounding_box_mm": mesh.bounding_box(),
        }
        if step_stats:
            result["step"] = step_stats
        self._report_unavailable(result, outputs, written)
        return EngineResult(
            success=True,
            engine=self.name,
            operation="export",
            result=result,
            pending_artifacts=[(str(p), t) for p, t in written],
            validation=ValidationResult(status="VALIDATED", checks=checks),
        )

    # ------------------------------------------------------------ generate ---

    def generate(self, parameters: dict[str, Any]) -> EngineResult:
        part = get_part(parameters.get("type", DEFAULT_PART_TYPE))

        outputs: list[str] = parameters.get("outputs") or list(DEFAULT_OUTPUTS)
        ir = part.ir_class.from_request(parameters.get("parameters", {}))
        mesh = part.builder(ir)

        ok, checks = self.validate(mesh, ir)
        if not ok:
            raise GeometryValidationError(
                "Generated geometry failed validation", details={"checks": checks}
            )

        work_dir = Path(tempfile.mkdtemp(prefix="trinity_cad_"))
        base_name = f"{part.filename_stem(ir)}_{uuid.uuid4().hex[:8]}"

        written, step_stats = self.export(mesh, outputs, work_dir, base_name, ir)

        result: dict[str, Any] = {
            "type": part.name,
            "spec": ir.to_dict(),
            "triangle_count": len(mesh.triangles),
            "bounding_box_mm": mesh.bounding_box(),
        }
        if step_stats:
            result["step"] = step_stats
        self._report_unavailable(result, outputs, written)

        return EngineResult(
            success=True,
            engine=self.name,
            operation="generate",
            result=result,
            pending_artifacts=[(str(p), t) for p, t in written],
            validation=ValidationResult(status="VALIDATED", checks=checks),
        )

    @staticmethod
    def _report_unavailable(
        result: dict[str, Any], outputs: list[str], written: list[tuple[Path, str]]
    ) -> None:
        """Name every requested format this call did not write, with the reason.

        Driven by what actually landed on disk rather than by a hardcoded
        list, so a format can never disappear silently: if it was asked for
        and is not in `written`, it gets an entry here.
        """
        produced = {fmt for _, fmt in written}
        missing = {
            fmt: format_reason(fmt) or "NOT_WRITTEN: no writer produced this format."
            for fmt in outputs
            if fmt not in produced and fmt not in ALWAYS_SUPPORTED_FORMATS
        }
        if missing:
            result["unavailable_formats"] = missing

    # ------------------------------------------------------------ validate ---

    def validate(
        self, mesh: Mesh, ir: PartIR, part: PartDefinition | None = None
    ) -> tuple[bool, dict[str, Any]]:
        definition = part or get_part(ir.to_dict().get("type", ""))
        return definition.validator(ir, mesh)

    # -------------------------------------------------------------- export ---

    def export(
        self,
        mesh: Mesh,
        formats: list[str],
        out_dir: Path,
        base_name: str,
        ir: PartIR | None = None,
    ) -> tuple[list[tuple[Path, str]], dict[str, Any]]:
        """Write each requested format. Returns ((path, type) pairs, step stats).

        `ir` is optional only for backward compatibility: JSON export needs
        the spec, so callers requesting "json" must pass it. Formats that
        cannot be written here are never silently dropped — the caller
        reports them from the returned pairs, so every format in `formats`
        that this backend can write produces exactly one pair.

        `step` differs from the pure-Python writers: without a CAD kernel
        it is skipped rather than raising, so a caller asking for
        stl+step still gets its STL and an honest reason for the STEP. With
        a kernel present, a STEP failure IS an error and propagates.
        """
        pairs: list[tuple[Path, str]] = []
        step_stats: dict[str, Any] = {}
        if "stl" in formats:
            p = out_dir / f"{base_name}.stl"
            write_binary_stl(mesh, str(p))
            pairs.append((p, "stl"))
        if "glb" in formats:
            p = out_dir / f"{base_name}.glb"
            write_glb(mesh, p)
            pairs.append((p, "glb"))
        if "json" in formats and ir is not None:
            p = out_dir / f"{base_name}.json"
            p.write_text(_ir_json(ir))
            pairs.append((p, "json"))
        if "step" in formats and kernel_info()["available"]:
            p = out_dir / f"{base_name}.step"
            step_stats = write_step(mesh, p)
            pairs.append((p, "step"))
        return pairs, step_stats

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
