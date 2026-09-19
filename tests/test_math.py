from fastapi.testclient import TestClient

from src.main import app


def test_solve_linear():
    with TestClient(app) as client:
        resp = client.post("/api/math/solve", json={"expression": "2*x + 4 = 0"})
        body = resp.json()
        assert resp.status_code == 200
        assert body["success"] is True
        assert body["result"]["result"] == [-2.0]
        assert body["validation"]["status"] == "VALIDATED"


def test_solve_quadratic():
    with TestClient(app) as client:
        resp = client.post("/api/math/solve", json={"expression": "x**2 - 4 = 0"})
        body = resp.json()
        assert resp.status_code == 200
        assert sorted(body["result"]["result"]) == [-2.0, 2.0]


def test_evaluate_percentage():
    with TestClient(app) as client:
        resp = client.post(
            "/api/execute",
            json={"engine": "math", "operation": "evaluate", "parameters": {"expression": "0.2 * n", "variables": {"n": 50}}},
        )
        body = resp.json()
        assert body["result"]["result"] == 10.0


def test_solve_missing_expression_is_validation_error():
    # Tracked-job failure, not a raw HTTP error — see note in test_cad.py.
    with TestClient(app) as client:
        resp = client.post("/api/math/solve", json={"expression": ""})
        body = resp.json()
        assert resp.status_code == 200
        assert body["success"] is False
        assert body["errors"][0]["code"] == "request_validation_error"
