import struct
from fastapi.testclient import TestClient
from app.main import app
from app.engines.cad.ir import QuadcopterFrameIR
from app.engines.cad.builder import build_quadcopter_frame
from app.engines.cad.glb import write_glb

def test_flagship_frame_emits_real_glb(tmp_path):
    mesh = build_quadcopter_frame(QuadcopterFrameIR.from_request({"overall_size": 50}))
    target = tmp_path / "frame.glb"; write_glb(mesh, target)
    assert target.read_bytes()[:4] == b"glTF"
    magic, version, length = struct.unpack("<4sII", target.read_bytes()[:12])
    assert magic == b"glTF" and version == 2 and length == target.stat().st_size

def test_api_flagship_links_checked_artifacts_to_job():
    with TestClient(app) as client:
        response = client.post("/api/cad/generate", json={"parameters": {"overall_size": 50}, "outputs": ["stl", "glb", "json"]})
        body = response.json()
        assert response.status_code == 200 and body["success"]
        assert body["validation"]["status"] == "VALIDATED"
        assert {item["type"] for item in body["artifacts"]} == {"stl", "glb", "json"}
        assert client.get(f"/api/jobs/{body['job_id']}").json()["status"] == "completed"
