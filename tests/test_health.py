from fastapi.testclient import TestClient

from src.main import app


def test_health():
    with TestClient(app) as client:
        resp = client.get("/api/health")
        assert resp.status_code == 200
        assert resp.json()["status"] == "ok"


def test_engines_listed():
    with TestClient(app) as client:
        resp = client.get("/api/engines")
        assert resp.status_code == 200
        names = {e["name"] for e in resp.json()}
        assert {"math", "cad"} <= names


def test_unknown_engine_is_a_real_404():
    with TestClient(app) as client:
        resp = client.post("/api/execute", json={"engine": "quantum", "operation": "solve", "parameters": {}})
        assert resp.status_code == 404
        assert resp.json()["errors"][0]["code"] == "engine_not_found"


def test_unknown_job_is_a_real_404():
    with TestClient(app) as client:
        resp = client.get("/api/jobs/does-not-exist")
        assert resp.status_code == 404
        assert resp.json()["errors"][0]["code"] == "job_not_found"
