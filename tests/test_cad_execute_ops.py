"""CAD execute() operation routing (Stage 2: defects A + B) and engines status (C).

The engine advertises ("generate", "validate", "export", "preview") so every
capability must be reachable through POST /api/execute. export() must write
every format generate() produces (stl/glb/json). GET /api/engines must
distinguish scaffolds from real engines via the status field.
"""

import pytest
from fastapi.testclient import TestClient

from src.core.errors import RequestValidationError
from src.engines.cad.engine import CADEngine
from src.main import app

ENGINE = CADEngine()
GOOD = {"type": "quadcopter_frame", "parameters": {"overall_size": 50}}


def _execute(client, operation, parameters):
    resp = client.post(
        "/api/execute",
        json={"engine": "cad", "operation": operation, "parameters": parameters},
    )
    assert resp.status_code == 200, resp.text
    return resp.json()


# ------------------------------------------------------------- validate ---


def test_execute_validate_returns_checks_and_validated():
    body = ENGINE.execute("validate", GOOD)
    assert body.success is True
    assert body.validation is not None and body.validation.status == "VALIDATED"
    assert body.validation.checks["triangle_count"] == 108
    assert body.result["triangle_count"] == 108


def test_execute_validate_via_http_has_no_artifacts():
    with TestClient(app) as client:
        body = _execute(client, "validate", GOOD)
    assert body["success"] is True
    assert body["validation"]["status"] == "VALIDATED"
    assert body["artifacts"] == []
    assert body["result"]["checks"]["degenerate_triangle_count"] == 0


def test_execute_validate_bad_params_fails_truthfully():
    with TestClient(app) as client:
        body = _execute(
            client, "validate", {"type": "quadcopter_frame", "parameters": {"overall_size": 0}}
        )
    assert body["success"] is False
    assert body["errors"], "must explain why, not fail silently"


# -------------------------------------------------------------- preview ---


def test_execute_preview_returns_bbox_without_artifacts_or_validation():
    body = ENGINE.execute("preview", GOOD)
    assert body.success is True
    assert body.result["triangle_count"] == 108
    assert set(body.result["bounding_box_mm"]) == {"min", "max"}
    assert body.artifacts == []
    assert body.validation is None


def test_execute_preview_via_http():
    with TestClient(app) as client:
        body = _execute(client, "preview", GOOD)
    assert body["success"] is True and body["operation"] == "preview"
    assert body["result"]["triangle_count"] == 108


# --------------------------------------------------------------- export ---


def test_execute_export_writes_all_three_formats():
    with TestClient(app) as client:
        body = _execute(client, "export", {**GOOD, "outputs": ["stl", "glb", "json"]})
    assert body["success"] is True
    assert {a["type"] for a in body["artifacts"]} == {"stl", "glb", "json"}
    assert body["validation"]["status"] == "VALIDATED"
    assert body["result"]["triangle_count"] == 108


def test_execute_export_reports_step_without_failing():
    with TestClient(app) as client:
        body = _execute(client, "export", {**GOOD, "outputs": ["stl", "step"]})
    assert body["success"] is True
    assert {a["type"] for a in body["artifacts"]} == {"stl"}
    assert "step" in body["result"]["unavailable_formats"]


def test_export_method_writes_glb_and_json(tmp_path):
    from src.engines.cad.parts import get_part

    part = get_part("quadcopter_frame")
    ir = part.ir_class.from_request({"overall_size": 50})
    mesh = part.builder(ir)
    pairs = ENGINE.export(mesh, ["stl", "glb", "json"], tmp_path, "frame", ir)
    assert {t for _, t in pairs} == {"stl", "glb", "json"}
    assert all(p.stat().st_size > 0 for p, _ in pairs)


# -------------------------------------------------------------- unknown ---


def test_execute_unknown_operation_still_rejected():
    with pytest.raises(RequestValidationError, match="no operation"):
        ENGINE.execute("teleport", GOOD)


# --------------------------------------------------------------- status ---


def test_engines_endpoint_marks_scaffolds(monkeypatch):
    # Bridge tests set TRINITY_NATIVE_EXEC process-wide; clear it here so
    # this test pins the no-binary behavior regardless of run order.
    monkeypatch.delenv("TRINITY_NATIVE_EXEC", raising=False)
    with TestClient(app) as client:
        engines = {e["name"]: e for e in client.get("/api/engines").json()}
    assert engines["cad"].get("status") is None
    assert engines["math"].get("status") is None
    for name in ("pcb", "firmware", "vision", "research", "simulation", "robotics"):
        assert engines[name]["status"] == "scaffolded", name
