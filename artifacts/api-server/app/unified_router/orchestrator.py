"""
Trinity Orchestrator — deterministic engine routing and workflow planning.
Routes user queries to the correct Trinity engine using keyword/pattern matching
that is always available and requires no API key. Multi-domain requests receive
an explicit ordered workflow plan rather than opaque autonomous execution.

"""
import re
from typing import Any

from app.engines.math_engine import MathEngine
from app.engines.quantum_engine import QuantumEngine
from app.engines.maker_cad import MakerCadEngine
from app.engines.maker_pcb import MakerPcbEngine
from app.engines.literature_engine import LiteratureEngine
from app.engines.vision_engine import VisionEngine
from app.designs.jobs import DesignJobService
from app.designs.models import CadDesignRequest, PcbDesignRequest
from app.firmware.jobs import FirmwareJobService
from app.firmware.models import FirmwareRequest
from app.workflows import build_workflow_plan, plan_to_dict


# ---------------------------------------------------------------------------
# Routing rules: (engine_id, regex_patterns, base_priority)
# Higher priority wins when multiple engines match.
# ---------------------------------------------------------------------------
ENGINE_IDS = {
    "firmware", "maker_pcb", "maker_cad", "quantum", "math",
    "literature", "vision", "collab", "orchestrator",
}

ROUTING_RULES: list[tuple[str, list[str], int]] = [
    ("firmware", [
        r"\b(firmware|embedded|microcontroller|mcu|bare[\s_-]?metal|rtos|driver|bootloader)\b",
        r"\b(flight[\s_-]?controller|pixhawk|px4|ardupilot|betaflight|inav)\b",
        r"\b(esp32|stm32|rp2040|raspberry[\s_-]?pi\s+pico|arduino|atmega|nrf52840)\b.*\b(code|firmware|program|driver)\b",
    ], 12),
    ("maker_pcb", [
        r"\b(pcb|circuit[\s_-]?board|kicad|schematic|route[\s_]+board)\b",
        r"\b(solder|footprint|gerber|netlist|bom|drill[\s_]+file)\b",
        r"\b(esp32|stm32|arduino|attiny|raspberry[\s_]+pi)\s+board\b",
        r"\b(4[\s_-]?layer|2[\s_-]?layer|multilayer)\s+(pcb|board)\b",
    ], 10),
    ("maker_cad", [
        r"\b(cad|fusion[\s_]?360|solidworks|3d[\s_]+model|parametric[\s_]+design)\b",
        r"\b(bracket|housing|enclosure|mount|flange|gear|shaft|axle|bushing)\b",
        r"\b(stl|step|iges|dxf)\s+file\b",
        r"\bdesign\s+a\s+(part|component|assembly)\b",
    ], 10),
    ("quantum", [
        r"\b(quantum|qubit|q-bit|qc)\b",
        r"\b(bloch[\s_]+sphere|entangl|superposition|decoherence)\b",
        r"\b(hadamard|cnot|pauli|bell[\s_]+state|grover|shor|qft)\b",
        r"\b(quantum[\s_]+circuit|quantum[\s_]+gate|quantum[\s_]+computing)\b",
    ], 9),
    ("math", [
        r"\b(solve|integral|integrat|derivative|differentiat|simplif|expand|factor)\b",
        r"\b(equation|algebra|calculus|matrix|eigenvalue|laplace|fourier[\s_]+transform)\b",
        r"[=\+\-\*\/\^]\s*\d",          # arithmetic/algebraic expression
        r"\b(sin|cos|tan|log|ln|exp|sqrt)\s*\(",
        r"\b(dx|dy|d\/dx|d\/dt|lim|∫|∑|∏)\b",
    ], 8),
    ("literature", [
        r"\b(paper|papers|arxiv|preprint|publication|journal|conference)\b",
        r"\b(research|survey|review|study|findings|literature)\b",
        r"\b(find|search|look[\s_]+up|retrieve)\s+(papers?|articles?|studies|research)\b",
        r"\b(author|wrote|published by)\b",
    ], 7),
    ("vision", [
        r"\b(ocr|handwrit|recognize[\s_]+text|scan[\s_]+equation)\b",
        r"\b(image[\s_]+to[\s_]+latex|latex[\s_]+from[\s_]+image|extract[\s_]+latex)\b",
        r"\b(pdf[\s_]+export|export[\s_]+to[\s_]+pdf|generate[\s_]+pdf)\b",
    ], 6),
    ("collab", [
        r"\b(collaborat|share[\s_]+notebook|real[\s_-]?time[\s_]+edit|multi[\s_-]?user)\b",
    ], 5),
]


class TrinityOrchestrator:
    """
    Routes user messages to the right engine and formats the response.
    Falls back to a general welcome/help message for unrecognized inputs.
    """

    def __init__(self) -> None:
        self._math = MathEngine()
        self._quantum = QuantumEngine()
        self._cad = MakerCadEngine()
        self._pcb = MakerPcbEngine()
        self._literature = LiteratureEngine()
        self._vision = VisionEngine()
        self._designs = DesignJobService()
        self._firmware = FirmwareJobService()

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    async def route(
        self,
        query: str,
        history: list | None = None,
        engine_override: str | None = None,
        owner_id: int | None = None,
    ) -> dict[str, Any]:
        """Detect or explicitly select an engine, then return {content, engine, data}."""
        if engine_override is not None:
            normalized = engine_override.strip().lower()
            if normalized not in ENGINE_IDS:
                return {
                    "content": f"Unknown engine `{engine_override}`. Choose one of: {', '.join(sorted(ENGINE_IDS - {'orchestrator'}))}.",
                    "engine": "orchestrator",
                    "data": {"error": "unknown_engine", "requested": engine_override},
                }
            if normalized == "orchestrator":
                engine_override = None
            else:
                try:
                    return await self._dispatch(normalized, query, owner_id=owner_id)
                except Exception as exc:
                    return {
                        "content": f"Engine **{normalized}** encountered an issue: {exc}",
                        "engine": "orchestrator",
                        "data": {"error": str(exc), "attempted": normalized},
                    }

        plan = build_workflow_plan(query)
        if len(plan.steps) > 1:
            return {
                "content": self._fmt_workflow(plan),
                "engine": "orchestrator",
                "data": {"workflow": plan_to_dict(plan)},
            }
        engine_id = self._detect(query)

        try:
            return await self._dispatch(engine_id, query, owner_id=owner_id)
        except Exception as exc:
            return {
                "content": (
                    f"Engine **{engine_id}** encountered an issue: {exc}\n\n"
                    + self._help_message()
                ),
                "engine": "orchestrator",
                "data": {"error": str(exc), "attempted": engine_id},
            }

    # ------------------------------------------------------------------
    # Engine detection
    # ------------------------------------------------------------------

    def _detect(self, text: str) -> str:
        scores: dict[str, float] = {}
        for engine_id, patterns, priority in ROUTING_RULES:
            score = 0.0
            for pat in patterns:
                matches = len(re.findall(pat, text, re.IGNORECASE))
                score += matches * priority
            if score > 0:
                scores[engine_id] = scores.get(engine_id, 0) + score

        return max(scores, key=scores.__getitem__) if scores else "orchestrator"

    # ------------------------------------------------------------------
    # Dispatch to engines
    # ------------------------------------------------------------------

    async def _dispatch(self, engine_id: str, query: str, owner_id: int | None = None) -> dict[str, Any]:
        if engine_id == "math":
            result = await self._math.process(query, "auto")
            return {"content": self._fmt_math(result), "engine": "math", "data": result}

        if engine_id == "quantum":
            result = await self._quantum.process(query, 2, 1024)
            return {"content": self._fmt_quantum(result), "engine": "quantum", "data": result}

        if engine_id == "firmware":
            job = await self._firmware.create(FirmwareRequest(description=query), owner_id=owner_id)
            result = job.model_dump(mode="json")
            return {"content": self._fmt_firmware(job), "engine": "firmware", "data": result}

        if engine_id == "maker_cad":
            job = await self._designs.create_cad(CadDesignRequest(description=query), owner_id=owner_id)
            result = job.model_dump(mode="json")
            return {"content": self._fmt_design_job(job), "engine": "maker_cad", "data": result}

        if engine_id == "maker_pcb":
            job = await self._designs.create_pcb(PcbDesignRequest(description=query), owner_id=owner_id)
            result = job.model_dump(mode="json")
            return {"content": self._fmt_design_job(job), "engine": "maker_pcb", "data": result}

        if engine_id == "literature":
            result = await self._literature.process(query, 5)
            return {"content": self._fmt_literature(result), "engine": "literature", "data": result}

        if engine_id == "vision":
            result = await self._vision.process(query)
            return {"content": self._fmt_vision(result), "engine": "vision", "data": result}

        if engine_id == "collab":
            return {
                "content": "**Trinity Collab Desktop** — Real-time collaborative notebook. Connect via WebSocket at `/api/ws`. Share the session URL with teammates to co-edit research notebooks live.",
                "engine": "collab",
                "data": {"ws_endpoint": "/api/ws", "status": "ready"},
            }

        # Default — general orchestrator response
        return {
            "content": self._help_message(),
            "engine": "orchestrator",
            "data": None,
        }

    # ------------------------------------------------------------------
    # Response formatters
    # ------------------------------------------------------------------

    def _fmt_math(self, r: dict) -> str:
        lines = ["**Trinity Math Engine** ∑"]
        if r.get("result"):
            lines.append(f"\n**Result:** `{r['result']}`")
        if r.get("latex"):
            lines.append(f"\n**LaTeX:** `{r['latex']}`")
        if r.get("steps"):
            lines.append("\n**Steps:**")
            lines.extend(f"  {i+1}. {s}" for i, s in enumerate(r["steps"]))
        return "\n".join(lines)

    def _fmt_quantum(self, r: dict) -> str:
        bs = r.get("bloch_sphere", {})
        lines = [
            f"**Trinity Quantum Lab** ⚛ — *{r.get('circuit_name', '')}*",
            f"\n{r.get('description', '')}",
        ]
        if r.get("circuit_diagram"):
            lines.append(f"\n**Circuit:**\n```\n{r['circuit_diagram']}\n```")
        if r.get("state_label"):
            lines.append(f"\n**State:** {r['state_label']}")
        if bs:
            lines.append(f"\n**Bloch sphere:** x={bs.get('x', 0):.3f}  y={bs.get('y', 0):.3f}  z={bs.get('z', 0):.3f}")
        counts = r.get("counts", {})
        if counts:
            top = sorted(counts.items(), key=lambda kv: -kv[1])[:4]
            lines.append("\n**Top measurement outcomes:**")
            total = sum(counts.values()) or 1
            for state, cnt in top:
                pct = 100 * cnt / total
                lines.append(f"  |{state}⟩  →  {cnt} shots  ({pct:.1f}%)")
        return "\n".join(lines)

    def _fmt_workflow(self, plan) -> str:
        lines = ["**Trinity Workflow Plan**", "", f"**Objective:** {plan.objective}", "", "The request spans multiple specialist engines. Steps are ordered explicitly and are not silently executed as one engine:"]
        for index, step in enumerate(plan.steps, start=1):
            dependency = f" (after `{step.depends_on[0]}`)" if step.depends_on else ""
            lines.append(f"{index}. `{step.id}` — **{step.engine}**: {step.objective}{dependency}")
        lines.append("\nSubmit each step or connect a workflow worker to execute the plan and persist outputs.")
        return "\n".join(lines)

    def _fmt_firmware(self, job) -> str:
        lines = [
            f"**Trinity Firmware Engine** — job `{job.job_id}`",
            f"\n**Status:** `{job.status.value}`",
            f"\n**Target:** `{job.target.name if job.target else 'unresolved'}`",
            f"\n**Validation:** `{job.validation.status}`",
        ]
        if job.questions:
            lines.append("\n**Target selection required:**")
            lines.extend(f"- {question}" for question in job.questions)
        if job.assumptions:
            lines.append("\n**Precision and safety notes:**")
            lines.extend(f"- {assumption}" for assumption in job.assumptions)
        if job.artifacts:
            lines.append("\n**Firmware project artifacts:**")
            lines.extend(f"- `{artifact.filename}` — {artifact.download_url}" for artifact in job.artifacts)
        warnings = [check.message for check in job.validation.checks if check.status in {"warning", "failed"}]
        if warnings:
            lines.append("\n**Validation notes:**")
            lines.extend(f"- {warning}" for warning in warnings)
        return "\n".join(lines)

    def _fmt_design_job(self, job) -> str:
        lines = [
            f"**{job.engine.replace('_', ' ').title()}** — job `{job.job_id}`",
            f"\n**Status:** `{job.status.value}`",
            f"\n**Validation:** `{job.validation.status.value}`",
        ]
        if job.questions:
            lines.append("\n**Confirmation needed:**")
            lines.extend(f"- {question}" for question in job.questions)
        if job.assumptions:
            lines.append("\n**Assumptions:**")
            lines.extend(f"- {assumption}" for assumption in job.assumptions)
        if job.artifacts:
            lines.append("\n**Artifacts:**")
            lines.extend(f"- `{artifact.filename}` — {artifact.download_url}" for artifact in job.artifacts)
        failures = [check.message for check in job.validation.checks if check.status in {"failed", "warning"}]
        if failures:
            lines.append("\n**Validation notes:**")
            lines.extend(f"- {message}" for message in failures)
        return "\n".join(lines)

    def _fmt_cad(self, r: dict) -> str:
        return (
            f"**Trinity Maker Engine (CAD)** ⚙\n\n"
            f"{r.get('description', '')}\n\n"
            f"📎 Download: `{r.get('filename', 'part.py')}` — Fusion 360 Python script\n\n"
            f"{r.get('instructions', '')}"
        )

    def _fmt_pcb(self, r: dict) -> str:
        fn = r.get("filename", "board")
        return (
            f"**Trinity Maker Engine (PCB)** 🔌\n\n"
            f"{r.get('description', '')}\n\n"
            f"📎 Download: `{fn}.kicad_sch` — KiCad Schematic\n"
            f"📎 Download: `{fn}.kicad_pcb` — KiCad PCB Layout\n\n"
            f"{r.get('instructions', '')}"
        )

    def _fmt_literature(self, r: dict) -> str:
        lines = [
            f"**Trinity Literature RAG** 📚 — *{r.get('query', '')}*",
            f"\n{r.get('summary', '')}",
        ]
        for p in r.get("papers", [])[:5]:
            authors_str = ", ".join(p.get("authors", [])[:3])
            lines.append(f"\n**{p['title']}**")
            lines.append(f"  {authors_str} · {p.get('published', '')[:7]}")
            lines.append(f"  {p.get('abstract', '')[:200]}...")
            lines.append(f"  🔗 [{p.get('url', '')}]({p.get('url', '')})")
        return "\n".join(lines)

    def _fmt_vision(self, r: dict) -> str:
        if r.get("status") == "awaiting_image":
            return (
                "**Trinity Vision Engine** 👁\n\n"
                "Upload an image and I'll extract text or LaTeX formulas. "
                "Supports handwritten equations, printed math, and scientific diagrams."
            )
        if r.get("latex"):
            return f"**Trinity Vision Engine** 👁\n\n**Extracted LaTeX:**\n```latex\n{r['latex']}\n```"
        return f"**Trinity Vision Engine** 👁\n\n{r.get('message', 'Ready.')}"

    def _help_message(self) -> str:
        return """\
I'm **Trinity** — your unified AI Engineering and Research Operating System.

Here's what I can do:

| Engine | Trigger examples |
|--------|-----------------|
| **∑ Math** | "solve x² + 2x + 1 = 0" · "integrate sin(x)" · "factor x³ - 8" |
| **⚛ Quantum** | "simulate a Bell state" · "show Grover search circuit" · "QFT on 3 qubits" |
| **⚙ Maker (CAD)** | "design an L-bracket 60×40mm" · "parametric housing 80×60×30mm" |
| **🔌 Maker (PCB)** | "design a 4-layer PCB for ESP32" · "generate KiCad schematic" |
| **📚 Literature** | "find papers on quantum error correction" · "search arxiv for LLMs" |
| **👁 Vision** | "extract LaTeX from this image" · "OCR my handwritten equation" |
| **💾 Firmware** | "write ESP32 firmware" · "generate PX4 module" · "create ArduPilot driver" |
| **🤝 Collab** | "start a shared research notebook" |

What would you like to build or explore?"""
