"""Stage 4 HTTP surface: SSE stream-plan, agent runs, DAG workflow execution."""
from __future__ import annotations

import json

from fastapi.testclient import TestClient

from src.main import app


def _parse_sse(text: str) -> list[dict]:
    events = []
    for line in text.split("\n"):
        if line.startswith("data: "):
            events.append(json.loads(line[len("data: ") :]))
    return events


def test_agent_run_without_model_refuses_truthfully(monkeypatch):
    monkeypatch.delenv("TRINITY_MODEL_ENDPOINT", raising=False)
    with TestClient(app) as client:
        resp = client.post("/api/agents/coder/run", json={"prompt": "build it"})
        assert resp.status_code == 200
        assert resp.json()["success"] is False
        assert resp.json()["errors"][0]["code"] == "capability_unavailable"


def test_unknown_agent_role_is_validation_error():
    with TestClient(app) as client:
        resp = client.post("/api/agents/ghost/run", json={"prompt": "x"})
        assert resp.status_code in (400, 422)


def test_stream_plan_without_model_falls_back_to_deterministic_router(monkeypatch):
    monkeypatch.delenv("TRINITY_MODEL_ENDPOINT", raising=False)
    with TestClient(app) as client:
        resp = client.post(
            "/api/intelligence/stream-plan", json={"text": "Create a 5cm x 3cm x 2cm box"}
        )
        assert resp.status_code == 200
        assert resp.headers["content-type"].startswith("text/event-stream")
        events = _parse_sse(resp.text)
        assert events, "stream must not be empty"
        final = events[-1]
        assert final["type"] in ("final", "error")
        if final["type"] == "final":
            assert final["fallback"] == "deterministic_router"
            assert final["tool"]["engine"] == "cad"


def test_stream_plan_streams_model_deltas(monkeypatch):
    class FakeLLM:
        def available(self):
            return True

        def stream(self, prompt, system=None):
            yield "I "
            yield "will plan."

    from src.api import routes as routes_module

    monkeypatch.setattr(routes_module, "_local_llm", lambda: FakeLLM())
    with TestClient(app) as client:
        resp = client.post("/api/intelligence/stream-plan", json={"text": "do something"})
        events = _parse_sse(resp.text)
        # No JSON object in the reply: the stream ends with the honest
        # plan_invalid error, never with a plan, never with a bare final.
        final = events[-1]
        assert final["type"] == "error"
        assert final["message"]["code"] == "plan_invalid"
        assert "no JSON object" in final["message"]["message"]
        deltas = [e for e in events if e["type"] == "delta"]
        assert "".join(e["text"] for e in deltas) == "I will plan."


class _StreamPlanLLM:
    """Scripted model: streams chunks verbatim, records what it was asked."""

    def __init__(self, *chunks: str):
        self.chunks = list(chunks)
        self.systems: list[str | None] = []
        self.prompts: list[str] = []

    def available(self) -> bool:
        return True

    def stream(self, prompt: str, system=None):
        self.prompts.append(prompt)
        self.systems.append(system)
        yield from self.chunks


def _plan(*nodes, summary=""):
    return json.dumps({"summary": summary, "nodes": list(nodes)})


def _stream_plan(monkeypatch, *chunks: str):
    fake = _StreamPlanLLM(*chunks)
    from src.api import routes as routes_module

    monkeypatch.setattr(routes_module, "_local_llm", lambda: fake)
    with TestClient(app) as client:
        resp = client.post(
            "/api/intelligence/stream-plan", json={"text": "plan it"}
        )
    events = _parse_sse(resp.text)
    return fake, events[-1]


def _math_solve_node(node_id="a", deps=()):
    return {
        "id": node_id,
        "engine": "math",
        "operation": "solve",
        "parameters": {"expression": "x*2=10", "solve_for": "x"},
        "dependencies": list(deps),
    }


def test_stream_plan_model_plan_final_frame_carries_validated_plan(monkeypatch):
    chunks = (
        '{"summary": "add the roots", "nodes": '
        + json.dumps([_math_solve_node("a"), _math_solve_node("b", ["a"])])
        + "}",
    )
    fake, final = _stream_plan(monkeypatch, *chunks)
    assert fake.systems[0] is not None
    assert final["type"] == "final"
    assert final["plan"]["source"] == "model"
    assert final["plan"]["summary"] == "add the roots"
    assert final["plan"]["nodes"] == [
        _math_solve_node("a"),
        _math_solve_node("b", ["a"]),
    ]


def test_stream_plan_model_reply_is_prompted_for_a_plan_over_real_engines(monkeypatch):
    chunks = ('{"summary": "s", "nodes": ' + json.dumps([_math_solve_node()]) + "}",)
    fake, final = _stream_plan(monkeypatch, *chunks)
    assert final["type"] == "final"
    from src.engines.registry import registry

    system = fake.systems[0]
    # The roster names engines that exist — and nothing invented.
    for entry in registry.list():
        assert f"- {entry['name']}:" in system
    assert "ghost_engine" not in system
    # The contract the parser relies on is spelled out in the prompt.
    assert '"nodes"' in system and '"dependencies"' in system


def test_stream_plan_model_cycle_plan_is_refused(monkeypatch):
    nodes = [_math_solve_node("a", ["b"]), _math_solve_node("b", ["a"])]
    _, final = _stream_plan(monkeypatch, _plan(*nodes))
    assert final["type"] == "error"
    assert final["message"]["code"] == "plan_invalid"
    assert "not an executable DAG" in final["message"]["message"]


def test_stream_plan_model_unknown_dependency_is_refused(monkeypatch):
    _, final = _stream_plan(
        monkeypatch, _plan(_math_solve_node("a", ["missing"]))
    )
    assert final["type"] == "error"
    assert final["message"]["code"] == "plan_invalid"
    assert "'missing' which the plan does not define" in final["message"]["message"]


def test_stream_plan_model_duplicate_ids_are_refused(monkeypatch):
    _, final = _stream_plan(
        monkeypatch, _plan(_math_solve_node("a"), _math_solve_node("a"))
    )
    assert final["type"] == "error"
    assert final["message"]["code"] == "plan_invalid"
    assert "duplicate node id 'a'" in final["message"]["message"]


def test_stream_plan_model_bad_node_shape_is_refused(monkeypatch):
    nodes = [{"id": "a", "engine": "math", "parameters": {"expression": "x"}}]
    _, final = _stream_plan(monkeypatch, _plan(*nodes))
    assert final["type"] == "error"
    assert final["message"]["code"] == "plan_invalid"
    assert "missing or empty operation" in final["message"]["message"]

    nodes = [{"id": "a", "engine": "math", "operation": "solve", "parameters": [1]}]
    _, final = _stream_plan(monkeypatch, _plan(*nodes))
    assert final["type"] == "error"
    assert final["message"]["code"] == "plan_invalid"
    assert "parameters must be an object" in final["message"]["message"]


def test_stream_plan_model_oversized_plan_is_refused(monkeypatch):
    from src.intelligence.plan import MAX_PLAN_NODES

    nodes = [_math_solve_node(f"n{i}") for i in range(MAX_PLAN_NODES + 1)]
    _, final = _stream_plan(monkeypatch, _plan(*nodes))
    assert final["type"] == "error"
    assert final["message"]["code"] == "plan_invalid"
    assert f"maximum is {MAX_PLAN_NODES}" in final["message"]["message"]


def test_stream_plan_fallback_frame_includes_executable_plan(monkeypatch):
    monkeypatch.delenv("TRINITY_MODEL_ENDPOINT", raising=False)
    with TestClient(app) as client:
        resp = client.post(
            "/api/intelligence/stream-plan",
            json={"text": "Solve x**2 - 4 = 0"},
        )
        final = _parse_sse(resp.text)[-1]
        assert final["type"] == "final"
        assert final["fallback"] == "deterministic_router"
        assert final["tool"]["engine"] == "math"
        # The plan section carries the same call in runnable node shape.
        assert final["plan"]["source"] == "deterministic_router"
        assert final["plan"]["nodes"] == [
            {
                "id": "n1",
                "engine": final["tool"]["engine"],
                "operation": final["tool"]["operation"],
                "parameters": final["tool"]["parameters"],
                "dependencies": [],
            }
        ]
        # The contract guarantee: a plan posts, unchanged, as a workflow.
        run = client.post(
            "/api/workflows/execute", json={"nodes": final["plan"]["nodes"]}
        )
        assert run.status_code == 200
        body = run.json()
        assert body["success"] is True
        assert body["nodes"][0]["status"] == "completed"


def test_stream_plan_model_plan_nodes_execute_as_workflow(monkeypatch):
    chunks = (
        '{"summary": "solve twice", "nodes": '
        + json.dumps([_math_solve_node("a"), _math_solve_node("b", ["a"])])
        + "}",
    )
    fake, final = _stream_plan(monkeypatch, *chunks)
    assert final["type"] == "final"
    # The model's plan is not a description: it runs as-is through the
    # workflow executor, dependencies intact.
    with TestClient(app) as client:
        run = client.post(
            "/api/workflows/execute", json={"nodes": final["plan"]["nodes"]}
        )
        assert run.status_code == 200
        body = run.json()
        assert body["success"] is True
        assert [node["status"] for node in body["nodes"]] == ["completed", "completed"]


def test_workflow_dag_happy_path_with_dependency_threading():
    with TestClient(app) as client:
        resp = client.post(
            "/api/workflows/execute",
            json={
                "nodes": [
                    {
                        "id": "n1",
                        "engine": "math",
                        "operation": "solve",
                        "parameters": {"expression": "x*2=10", "solve_for": "x"},
                        "dependencies": [],
                    }
                ]
            },
        )
    assert resp.status_code == 200
    body = resp.json()
    assert body["success"] is True
    assert body["nodes"][0]["status"] == "completed"


def test_workflow_failed_node_blocks_dependents(monkeypatch):
    from src.jobs import manager as manager_module

    def fake_run_sync(self, engine, operation, parameters):
        if operation == "boom":
            return {
                "success": False,
                "engine": engine,
                "operation": operation,
                "result": {},
                "artifacts": [],
                "validation": None,
                "errors": [{"code": "engine_execution_error", "message": "nope"}],
                "job_id": "job-x",
            }
        return {"success": True, "result": {}, "artifacts": [], "errors": [], "job_id": "job-ok"}

    monkeypatch.setattr(manager_module.JobManager, "run_sync", fake_run_sync)
    with TestClient(app) as client:
        resp = client.post(
            "/api/workflows/execute",
            json={
                "nodes": [
                    {"id": "a", "engine": "pump", "operation": "boom", "parameters": {},
                     "dependencies": []},
                    {"id": "b", "engine": "pump", "operation": "ok", "parameters": {},
                     "dependencies": ["a"]},
                ]
            },
        )
    assert resp.status_code == 200
    body = resp.json()
    assert body["success"] is False
    assert body["nodes"][0]["status"] == "failed"
    assert body["nodes"][1]["status"] == "queued"
    assert body["nodes"][1]["errors"][0]["code"] == "blocked_by_upstream"


def test_workflow_dependency_parameter_threading(monkeypatch):
    ran: dict = {}

    def fake_run_sync(self, engine, operation, parameters):
        ran.setdefault("order", []).append((operation, parameters))
        if operation == "first":
            return {"success": True, "result": {"height": 42}, "artifacts": [], "errors": [], "job_id": "j1"}
        return {"success": True, "result": {"echoed": parameters}, "artifacts": [], "errors": [], "job_id": "j2"}

    from src.jobs import manager as manager_module

    monkeypatch.setattr(manager_module.JobManager, "run_sync", fake_run_sync)
    with TestClient(app) as client:
        resp = client.post(
            "/api/workflows/execute",
            json={
                "nodes": [
                    {"id": "up", "engine": "pump", "operation": "first", "parameters": {},
                     "dependencies": []},
                    {"id": "down", "engine": "pump", "operation": "second",
                     "parameters": {"size": "{node:up}.result.height"},
                     "dependencies": ["up"]},
                ]
            },
        )
    body = resp.json()
    assert body["success"] is True
    assert ran["order"][1][1]["size"] == 42


def test_workflow_cycle_is_refused():
    with TestClient(app) as client:
        resp = client.post(
            "/api/workflows/execute",
            json={
                "nodes": [
                    {"id": "a", "engine": "pump", "operation": "op",
                     "parameters": {}, "dependencies": ["b"]},
                    {"id": "b", "engine": "pump", "operation": "op",
                     "parameters": {}, "dependencies": ["a"]},
                ]
            },
        )
    assert resp.status_code in (400, 422)
