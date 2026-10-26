"""Coder role — drives the firmware/PCB toolchain through the agent loop.

`to_toolcall` remains the deterministic single-shot mapping used by the
prompt-engineering chain; `run` is the autonomous multi-step loop (see
src/agents/loop.py): the model plans calls, engines execute, diagnostics
feed back until done or the step budget is spent.
"""
from __future__ import annotations

from collections.abc import Callable
from typing import Any

from src.agents.loop import AGENT_SYSTEM_PROMPT, run_agent_loop

CODER_SYSTEM = AGENT_SYSTEM_PROMPT + (
    " You are the CODER agent: coordinate the firmware and pcb engines "
    "(create/build/flash flows) to realize the engineering request. Chain "
    "operations that depend on earlier artifacts."
)


class CoderAgent:
    engine = "firmware"
    operation = "create"

    def to_toolcall(self, spec: str) -> dict[str, Any]:
        return {"engine": self.engine, "operation": self.operation, "parameters": {"spec": spec}}

    def run(
        self,
        spec: str,
        llm: Any,
        max_steps: int = 8,
        executor: Callable[..., dict[str, Any]] | None = None,
    ) -> dict[str, Any]:
        return run_agent_loop(spec, CODER_SYSTEM, llm, max_steps=max_steps, executor=executor)
