"""Multi-engine workflow step execution."""
from __future__ import annotations

import json
from typing import Any

from app.designs.jobs import DesignJobService
from app.designs.models import CadDesignRequest, PcbDesignRequest
from app.engines.literature_engine import LiteratureEngine
from app.engines.math_engine import MathEngine
from app.engines.quantum_engine import QuantumEngine
from app.engines.vision_engine import VisionEngine
from app.firmware.jobs import FirmwareJobService
from app.firmware.models import FirmwareRequest
from app.pipelines.paper_to_code import PaperToCodePipeline
from app.pipelines.whiteboard_pcb import WhiteboardPcbPipeline


class PipelineExecutor:
    """Execute individual workflow steps and chain artifacts between engines."""

    def __init__(self) -> None:
        self._math = MathEngine()
        self._quantum = QuantumEngine()
        self._literature = LiteratureEngine()
        self._vision = VisionEngine()
        self._designs = DesignJobService()
        self._firmware = FirmwareJobService()
        self._paper_to_code = PaperToCodePipeline()
        self._whiteboard_pcb = WhiteboardPcbPipeline()

    async def execute_step(
        self,
        engine: str,
        objective: str,
        owner_id: int | None = None,
        context: dict[str, Any] | None = None,
        conversation_key: str | None = None,
    ) -> dict[str, Any]:
        context = context or {}
        if engine == "literature":
            result = await self._literature.process(objective, 5)
        elif engine == "math":
            result = await self._math.process(objective, "auto")
        elif engine == "quantum":
            result = await self._quantum.process(objective, 2, 1024)
        elif engine == "maker_cad":
            job = await self._designs.create_cad(CadDesignRequest(description=objective), owner_id=owner_id)
            result = job.model_dump(mode="json")
        elif engine == "maker_pcb":
            prior = context.get("prior_step")
            description = objective
            if prior and prior.get("engine") == "whiteboard_pcb":
                description = prior.get("result", {}).get("pcb_description", objective)
            job = await self._designs.create_pcb(PcbDesignRequest(description=description), owner_id=owner_id)
            result = job.model_dump(mode="json")
        elif engine == "firmware":
            pin_context = ""
            prior = context.get("prior_step")
            if prior and prior.get("engine") == "maker_pcb":
                pin_context = " Use pin assignments from the generated PCB job."
            job = await self._firmware.create(
                FirmwareRequest(description=objective + pin_context),
                owner_id=owner_id,
                conversation_key=conversation_key,
            )
            result = job.model_dump(mode="json")
        elif engine == "vision":
            image_data = context.get("image_data")
            result = await self._vision.process(objective, image_data=image_data)
        elif engine == "paper_to_code":
            paper_text = context.get("paper_text", objective)
            result = await self._paper_to_code.run(paper_text, context.get("equation_hint"))
        elif engine == "whiteboard_pcb":
            image_data = context.get("image_data")
            if not image_data:
                raise ValueError("whiteboard_pcb requires image_data in workflow context")
            result = await self._whiteboard_pcb.run(image_data, objective)
        else:
            raise ValueError(f"Unsupported workflow engine: {engine}")
        return {"engine": engine, "objective": objective, "result": result, "status": "completed"}

    async def execute_plan(
        self,
        plan: dict[str, Any],
        owner_id: int | None = None,
        context: dict[str, Any] | None = None,
        conversation_key: str | None = None,
    ) -> dict[str, Any]:
        steps_out: list[dict[str, Any]] = []
        prior: dict[str, Any] | None = None
        merged_context = dict(context or {})
        for step in plan.get("steps", []):
            if prior:
                merged_context["prior_step"] = prior
            executed = await self.execute_step(
                step["engine"],
                step.get("objective") or plan.get("objective", ""),
                owner_id=owner_id,
                context=merged_context,
                conversation_key=conversation_key,
            )
            steps_out.append({"id": step.get("id"), **executed})
            prior = executed
        return {
            "objective": plan.get("objective"),
            "mode": plan.get("mode", "deterministic"),
            "steps": steps_out,
            "status": "completed",
            "final": steps_out[-1] if steps_out else None,
        }


def plan_from_payload(payload: str | dict) -> dict[str, Any]:
    if isinstance(payload, str):
        return json.loads(payload)
    return payload
