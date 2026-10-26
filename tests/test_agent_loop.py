"""Stage 4: multi-step agent loop — scripted fakes, no network, no fake results."""
from __future__ import annotations

import json


from src.agents.coder import CoderAgent
from src.agents.researcher import ResearcherAgent
from src.agents.loop import _parse_action, run_agent_loop


class FakeLLM:
    """Scripted script: pops one queued reply per generate() call."""

    def __init__(self, *replies: str):
        self.replies = list(replies)
        self.prompts: list[str] = []

    def generate(self, prompt: str, system: str = "") -> str:
        self.prompts.append(prompt)
        if not self.replies:
            return '{"action": "done", "summary": "nothing left"}'
        return self.replies.pop(0)


def ok_envelope(engine: str, result: dict | None = None) -> dict:
    return {
        "success": True,
        "engine": engine,
        "operation": "op",
        "result": result or {"ok": True},
        "artifacts": [{"artifact_id": "art-1"}],
        "validation": None,
        "errors": [],
        "job_id": "job-1",
    }


def test_call_then_done_happy_path():
    llm = FakeLLM(
        '{"action": "call", "tool": {"engine": "firmware", "operation": "create", '
        '"parameters": {"spec": "led blink"}}}',
        '{"action": "done", "summary": "built it"}',
    )
    calls = []

    def executor(engine, operation, parameters):
        calls.append((engine, operation, parameters))
        return ok_envelope(engine)

    result = run_agent_loop("build led blink", "sys", llm, executor=executor)
    assert result["status"] == "completed"
    assert result["summary"] == "built it"
    assert calls == [("firmware", "create", {"spec": "led blink"})]
    assert len(result["steps"]) == 1
    assert result["steps"][0]["success"] is True
    # The success observation reached the model before the done step.
    assert "Step 1 executed firmware.create" in llm.prompts[1]
    assert "Artifacts: art-1" in llm.prompts[1]


def test_engine_failure_feeds_diagnostics_back_for_retry():
    llm = FakeLLM(
        '{"action": "call", "tool": {"engine": "pcb", "operation": "generate", "parameters": {}}}',
        # After seeing the failure the model corrects itself:
        '{"action": "call", "tool": {"engine": "pcb", "operation": "generate", '
        '"parameters": {"nets": 2}}}',
        '{"action": "done", "summary": "fixed after retry"}',
    )
    scripted_results = [
        {
            "success": False,
            "engine": "pcb",
            "result": {},
            "artifacts": [],
            "errors": [{"code": "validation_error", "message": "nets must be >= 2"}],
            "job_id": "job-fail",
        },
        ok_envelope("pcb"),
    ]

    def executor(engine, operation, parameters):
        return scripted_results.pop(0)

    result = run_agent_loop("make a pcb", "sys", llm, executor=executor)
    assert result["status"] == "completed"
    assert [s["success"] for s in result["steps"] if s["action"] == "call"] == [False, True]
    # Duck-typed self-correction: second prompt contains the framed failure.
    retry_prompt = llm.prompts[2]
    assert "FAILED with validation_error" in retry_prompt
    assert "nets must be >= 2" in retry_prompt


def test_step_budget_exhausted_is_reported_truthfully():
    reply = '{"action": "call", "tool": {"engine": "firmware", "operation": "create", "parameters": {}}}'
    llm = FakeLLM(*([reply] * 10))

    result = run_agent_loop("loop forever", "sys", llm, max_steps=3, executor=lambda *_: ok_envelope("firmware"))
    assert result["status"] == "step_budget_exhausted"
    assert result["summary"] == ""
    calls = [s for s in result["steps"] if s["action"] == "call"]
    assert len(calls) == 3
    assert calls[0]["job_id"] == "job-1"


def test_invalid_model_reply_is_fed_back_not_fatal():
    llm = FakeLLM(
        "I will build the thing as soon as I can.",
        '{"action": "done", "summary": "recovered"}',
    )
    result = run_agent_loop("do it", "sys", llm, executor=lambda *_: ok_envelope("x"))
    assert result["status"] == "completed"
    assert result["steps"][0]["action"] == "invalid_model_reply"
    assert "not a valid action object" in llm.prompts[1]


def test_malformed_tool_reports_but_does_not_execute():
    llm = FakeLLM(
        '{"action": "call", "tool": {"engine": "cad"}}',
        '{"action": "done", "summary": "gave up on bad call"}',
    )
    executed = []
    result = run_agent_loop("x", "sys", llm, executor=lambda *a, **k: executed.append(a))
    assert result["steps"][0]["action"] == "invalid_tool"
    assert not executed


def test_researcher_runs_loop_with_search_plan():
    llm = FakeLLM(
        '{"action": "call", "tool": {"engine": "research", "operation": "search", '
        '"parameters": {"query": "esp32 frame"}}}',
        '{"action": "done", "summary": "found references"}',
    )
    result = ResearcherAgent().run("esp32 frame parts", llm, executor=lambda *_: {"success": True, "result": {}, "artifacts": [], "errors": [], "job_id": "j"})
    assert result["status"] == "completed"
    assert "esp32 frame parts" in llm.prompts[0]
    assert result["steps"][0]["engine"] == "research"


def test_refusal_agent():
    from src.agents.loop import refusal

    payload = refusal("coder")
    assert payload["success"] is False
    assert payload["errors"][0]["code"] == "capability_unavailable"


def test_parse_action_extracts_json_surrounded_by_prose():
    action = _parse_action('Sure! {"action": "call", "tool": {"engine": "cad", "operation": "generate", "parameters": {"type": "box"}}} — on it.')
    assert action["tool"]["engine"] == "cad"


def test_coder_agent_backwards_compatible_toolcall():
    agent = CoderAgent()
    assert agent.to_toolcall("spec text")["engine"] == "firmware"
    assert "firmware" in json.dumps(agent.to_toolcall("spec text"))
