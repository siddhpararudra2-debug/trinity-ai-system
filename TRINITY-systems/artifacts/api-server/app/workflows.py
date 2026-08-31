"""Deterministic multi-engine workflow planning.

This provides an honest local alternative to opaque autonomous reasoning: every
step is derived from visible trigger rules and returned to the caller.
"""
from __future__ import annotations

import re
from dataclasses import dataclass


@dataclass(frozen=True)
class WorkflowStep:
    id: str
    engine: str
    objective: str
    depends_on: list[str]


@dataclass(frozen=True)
class WorkflowPlan:
    objective: str
    steps: list[WorkflowStep]
    mode: str = "deterministic"


def build_workflow_plan(query: str) -> WorkflowPlan:
    text = query.strip()
    lower = text.lower()
    matches: list[tuple[str, str, int]] = []
    patterns = [
        ("literature", r"\b(paper|papers|research|arxiv|literature)\b", 1, "Find and summarize relevant research."),
        ("math", r"\b(solve|integrate|differentiate|equation|calculate|symbolic)\b", 2, "Compute or simplify the mathematical request."),
        ("maker_cad", r"\b(cad|bracket|housing|gear|shaft|3d model|fusion)\b", 3, "Generate and validate the CAD design."),
        ("maker_pcb", r"\b(pcb|schematic|kicad|circuit board|gerber|bom)\b", 4, "Generate and validate the PCB project."),
        ("firmware", r"\b(firmware|embedded|microcontroller|mcu|esp32|stm32|rp2040|arduino|px4|ardupilot|betaflight|inav)\b", 5, "Generate a target-specific firmware project."),
        ("vision", r"\b(image|photo|ocr|handwritten|latex from)\b", 6, "Extract visual text or mathematical notation."),
    ]
    for engine, pattern, order, objective in patterns:
        if re.search(pattern, lower):
            matches.append((engine, objective, order))
    matches.sort(key=lambda item: item[2])
    steps: list[WorkflowStep] = []
    for index, (engine, objective, _) in enumerate(matches, start=1):
        step_id = f"step_{index}"
        steps.append(WorkflowStep(id=step_id, engine=engine, objective=objective, depends_on=[steps[-1].id] if steps else []))
    return WorkflowPlan(objective=text, steps=steps)


def plan_to_dict(plan: WorkflowPlan) -> dict:
    return {
        "objective": plan.objective,
        "mode": plan.mode,
        "steps": [
            {"id": step.id, "engine": step.engine, "objective": step.objective, "depends_on": step.depends_on}
            for step in plan.steps
        ],
    }
