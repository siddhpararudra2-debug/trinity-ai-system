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
            yield "Plan "
            yield "parts."

    from src.api import routes as routes_module

    monkeypatch.setattr(routes_module, "_local_llm", lambda: FakeLLM())
    with TestClient(app) as client:
        resp = client.post("/api/intelligence/stream-plan", json={"text": "do something"})
        events = _parse_sse(resp.text)
        assert events[-1]["type"] == "final"
        deltas = [e for e in events if e["type"] == "delta"]
        assert "".join(e["text"] for e in deltas) == "Plan parts."


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
