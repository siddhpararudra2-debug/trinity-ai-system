"""Multi-step agent loop — LLM proposes tool calls, Trinity executes them.

Protocol works over ANY OpenAI-compatible model (no function-calling API
needed): the loop asks the model for a JSON action object each step,

    {"action": "call", "tool": {"engine": ..., "operation": ..., "parameters": ...}}
or  {"action": "done", "summary": ...}

A step's engine result (or failure diagnostics) is fed back verbatim as the
next observation, so the model can self-correct — adjust parameters after a
validation error, retry, or stop honestly. No step is ever faked: engine
envelopes carry the job_id, artifacts and error dicts exactly as returned.
The step budget (max_steps) bounds runaway loops; hitting it is reported
truthfully as step_budget_exhausted rather than a success.
"""

from __future__ import annotations

import json
from collections.abc import Callable
from typing import Any

from src.core.errors import TrinityError

DEFAULT_MAX_STEPS = 8
_RESULT_SNIPPET = 1600

AGENT_SYSTEM_PROMPT = (
    "You are a Trinity engineering agent. Each turn you must reply with "
    "EXACTLY one JSON object and nothing else:\n"
    '  {"action": "call", "tool": {"engine": <name>, "operation": <name>, '
    '"parameters": {...}}}\n'
    'or {"action": "done", "summary": <one-sentence result>}.\n'
    "Engines execute the work — never execute, invent results or claim "
    "success for a tool you did not call. When a step fails, use the error "
    "in the observation to adjust parameters and retry; if a failure is not "
    "fixable, stop with \"done\" and say so in the summary."
)


def _first_json_object(text: str) -> str | None:
    """Extract the first balanced {...} block; models add prose despite
    instructions, and that must degrade to a parse error, not a guess."""
    start = text.find("{")
    while start != -1:
        depth = 0
        in_string = False
        escaped = False
        for index in range(start, len(text)):
            char = text[index]
            if in_string:
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == '"':
                    in_string = False
            elif char == '"':
                in_string = True
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return text[start : index + 1]
        start = text.find("{", start + 1)
    return None


def _parse_action(reply: str) -> dict[str, Any]:
    if reply is None:
        raise ValueError("model returned nothing")
    block = _first_json_object(reply)
    if block is None:
        raise ValueError("reply contained no JSON object")
    action = json.loads(block)
    if not isinstance(action, dict):
        raise ValueError("action was not a JSON object")
    return action


def _default_executor(engine: str, operation: str, parameters: dict[str, Any]) -> dict[str, Any]:
    """Job-manager execution; unknown engines and engine failures come back
    as honest failure envelopes instead of escaping as exceptions."""
    from src.jobs.manager import job_manager

    try:
        return job_manager.run_sync(engine, operation, parameters)
    except TrinityError as exc:
        return {
            "success": False,
            "engine": engine,
            "operation": operation,
            "result": {},
            "artifacts": [],
            "validation": None,
            "errors": [exc.to_dict()],
            "job_id": "",
        }


def _observation(step_number: int, call: dict[str, Any], envelope: dict[str, Any]) -> str:
    lines = [f"Step {step_number} executed {call.get('engine')}.{call.get('operation')}."]
    if envelope.get("success"):
        snippet = json.dumps(envelope.get("result", {}), default=str)[:_RESULT_SNIPPET]
        lines.append("Result: " + snippet)
        artifacts = envelope.get("artifacts") or []
        if artifacts:
            ids = ", ".join(
                str(a.get("artifact_id") or a) for a in artifacts
            )
            lines.append(f"Artifacts: {ids}")
    else:
        errors = envelope.get("errors") or [{"code": "unknown", "message": "no detail"}]
        first = errors[0]
        code = first.get("code", "unknown") if isinstance(first, dict) else str(first)
        message = (
            first.get("message", "") if isinstance(first, dict) else str(first)
        )
        lines.append(
            f"FAILED with {code}: {message}. Fix the parameters and retry, "
            "or stop if this cannot be fixed."
        )
    return "\n".join(lines)


def run_agent_loop(
    prompt: str,
    system: str,
    llm: Any,
    max_steps: int = DEFAULT_MAX_STEPS,
    executor: Callable[[str, str, dict[str, Any]], dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """Iterate llm -> tool call -> engine -> observation until done or budget.

    `llm` needs only generate(prompt, system) -> str — the same minimal
    surface LocalClient exposes, which keeps the loop fully testable with a
    scripted fake model before any real LLM is connected.
    """
    execute = executor or _default_executor
    transcript = f"Request: {prompt}\n"
    steps: list[dict[str, Any]] = []
    summary = ""
    status = "step_budget_exhausted"

    for step_number in range(1, max_steps + 1):
        reply = llm.generate(transcript, system=system)
        try:
            action = _parse_action(reply)
        except ValueError as exc:
            steps.append(
                {"step": step_number, "action": "invalid_model_reply", "error": str(exc)}
            )
            transcript += f"\nObservation (step {step_number}): the previous reply was not a "
            transcript += (
                "valid action object. Reply with exactly one JSON action object."
            )
            continue

        if action.get("action") == "done":
            summary = str(action.get("summary", ""))
            status = "completed"
            break

        tool = action.get("tool")
        if not isinstance(tool, dict) or "engine" not in tool or "operation" not in tool:
            steps.append(
                {"step": step_number, "action": "invalid_tool", "tool": tool}
            )
            transcript += (
                f"\nObservation (step {step_number}): malformed tool call "
                f"{json.dumps(tool)} — engine and operation are required."
            )
            continue

        parameters = tool.get("parameters") if isinstance(tool.get("parameters"), dict) else {}
        try:
            envelope = execute(tool["engine"], tool["operation"], parameters)
        except Exception as exc:  # noqa: BLE001 — never let a stray crash kill the loop
            envelope = {"success": False, "errors": [{"code": "executor_crash", "message": str(exc)}]}

        steps.append(
            {
                "step": step_number,
                "action": "call",
                "engine": tool["engine"],
                "operation": tool["operation"],
                "parameters": parameters,
                "success": bool(envelope.get("success")),
                "job_id": envelope.get("job_id", ""),
                "artifacts": envelope.get("artifacts", []),
                "errors": envelope.get("errors", []),
            }
        )
        transcript += f"\nObservation (step {step_number}): {_observation(step_number, tool, envelope)}"

    return {"role": "agent", "status": status, "summary": summary, "steps": steps}


def refusal(role: str) -> dict[str, Any]:
    """Truthful no-LLM response — the agent cannot run without a model."""
    from src.core.errors import CapabilityUnavailableError

    exc = CapabilityUnavailableError(
        f"{role} agent cannot run: no model endpoint configured. Set "
        f"TRINITY_MODEL_ENDPOINT (OpenAI-compatible chat/completions URL) "
        f"to enable it.",
        details={"agent": role, "env_var": "TRINITY_MODEL_ENDPOINT"},
    )
    return {"success": False, "errors": [exc.to_dict()]}
