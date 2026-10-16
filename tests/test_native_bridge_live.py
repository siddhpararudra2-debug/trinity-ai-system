"""Live bridge tests (Item 1, Stage 4c). Skipped unless TRINITY_NATIVE_EXEC is set.

Every mapped op is single-shot with full parameters (no cross-call state
needed), so these mirror --selftest shapes with deterministic assertions.
"""

import os

import pytest
from fastapi.testclient import TestClient

from src.main import app

needs_binary = pytest.mark.skipif(
    not os.getenv("TRINITY_NATIVE_EXEC", ""),
    reason="needs Trinity.exe via TRINITY_NATIVE_EXEC",
)


def _exec(client, engine, operation, parameters):
    resp = client.post(
        "/api/execute",
        json={"engine": engine, "operation": operation, "parameters": parameters},
    )
    assert resp.status_code == 200, resp.text
    return resp.json()


@needs_binary
def test_live_robotics_kinematics_matches_selftest():
    with TestClient(app) as client:
        body = _exec(
            client,
            "robotics",
            "kinematics",
            {"joint_angles": [0.0, 0.0]},
        )
    assert body["success"] is True
    pos = body["result"]["end_effector"]["position"]
    assert abs(pos["x"] - 2.0) < 1e-6 and abs(pos["y"]) < 1e-6


@needs_binary
def test_live_simulation_runs_linear_motion():
    with TestClient(app) as client:
        body = _exec(
            client,
            "simulation",
            "simulate",
            {
                "duration_s": 1.0,
                "initial_velocity_m_s": 2.0,
                "acceleration_m_s2": 1.0,
            },
        )
    assert body["success"] is True
    assert body["result"]["result"]["step_count"] > 0


@needs_binary
def test_live_pcb_generates_board_with_artifacts():
    with TestClient(app) as client:
        body = _exec(
            client,
            "pcb",
            "generate",
            {
                "width_mm": 50.0,
                "height_mm": 40.0,
                "components": [{"part": "esp32"}],
            },
        )
    assert body["success"] is True
    assert body["validation"]["status"] == "VALIDATED"
    assert len(body["artifacts"]) >= 1


@needs_binary
def test_live_unmapped_op_stays_truthful():
    with TestClient(app) as client:
        body = _exec(client, "firmware", "test", {})
    assert body["success"] is False
    assert body["errors"][0]["code"] == "capability_unavailable"
