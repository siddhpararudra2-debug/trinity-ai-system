from fastapi.testclient import TestClient

from src.main import app


def test_generate_50mm_quadcopter_frame():
    with TestClient(app) as client:
        resp = client.post(
            "/api/cad/generate",
            json={
                "type": "quadcopter_frame",
                "parameters": {"overall_size": 50, "motor_count": 4},
                "outputs": ["stl", "json"],
            },
        )
        body = resp.json()
        assert resp.status_code == 200, body
        assert body["success"] is True
        assert body["validation"]["status"] == "VALIDATED"
        types = {a["type"] for a in body["artifacts"]}
        assert types == {"stl", "json"}
        for artifact in body["artifacts"]:
            assert artifact["size_bytes"] > 0


def test_generate_rejects_bad_geometry():
    # Bad *input* (not bad geometry, but exercises the same failure path):
    # engine-time validation failures come back as a tracked, failed job
    # (HTTP 200, success=false, errors populated) rather than a raw 422 —
    # engine-not-found / job-not-found / artifact-not-found are the ones
    # that get real HTTP error codes (see test_health.py's sibling checks).
    with TestClient(app) as client:
        resp = client.post(
            "/api/cad/generate",
            json={
                "type": "quadcopter_frame",
                "parameters": {"overall_size": 20, "center_plate_size": 26},
                "outputs": ["json"],
            },
        )
        body = resp.json()
        assert resp.status_code == 200
        assert body["success"] is False
        assert body["errors"][0]["code"] == "request_validation_error"


def test_artifact_is_downloadable():
    with TestClient(app) as client:
        gen = client.post(
            "/api/cad/generate",
            json={"type": "quadcopter_frame", "parameters": {"overall_size": 50}, "outputs": ["stl"]},
        ).json()
        artifact_id = gen["artifacts"][0]["artifact_id"]
        resp = client.get(f"/api/artifacts/{artifact_id}")
        assert resp.status_code == 200
        assert resp.content[:5] == b"trini"  # binary STL 80-byte header starts with our name
