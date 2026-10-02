"""Phase 1: CAD part registry — dispatch, schema validation, extensibility."""

from dataclasses import dataclass, field
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from src.core.errors import RequestValidationError
from src.engines.cad.engine import CADEngine
from src.engines.cad.parts import (
    PartDefinition,
    get_part,
    list_supported_types,
    register_part,
    unregister_part,
)
from src.engines.cad.primitives import Mesh, box
from src.main import app


def test_registry_lists_exactly_quadcopter_frame():
    assert list_supported_types() == ["quadcopter_frame"]


def test_get_part_unknown_raises_request_validation_error():
    with pytest.raises(RequestValidationError) as excinfo:
        get_part("nope")
    assert str(excinfo.value) == "Unsupported CAD type 'nope'"
    assert excinfo.value.details == {"supported": ["quadcopter_frame"]}


def test_generate_without_type_defaults_and_validates():
    with TestClient(app) as client:
        resp = client.post("/api/cad/generate", json={"parameters": {"overall_size": 50}})
        body = resp.json()
        assert resp.status_code == 200, body
        assert body["success"] is True
        assert body["validation"]["status"] == "VALIDATED"


def test_generate_unknown_type_returns_422():
    with TestClient(app) as client:
        resp = client.post("/api/cad/generate", json={"type": "nope"})
        body = resp.json()
        assert resp.status_code == 422
        assert body["success"] is False


def test_execute_unknown_type_fails_with_supported_list():
    with TestClient(app) as client:
        resp = client.post(
            "/api/execute",
            json={
                "engine": "cad",
                "operation": "generate",
                "parameters": {"type": "nope"},
            },
        )
        body = resp.json()
        assert resp.status_code == 200
        assert body["success"] is False
        assert body["errors"][0]["code"] == "request_validation_error"
        assert body["errors"][0]["details"]["supported"] == ["quadcopter_frame"]


def test_execute_defaults_to_quadcopter_frame_when_type_missing():
    with TestClient(app) as client:
        resp = client.post(
            "/api/execute",
            json={
                "engine": "cad",
                "operation": "generate",
                "parameters": {"outputs": ["json"]},
            },
        )
        body = resp.json()
        assert resp.status_code == 200
        assert body["success"] is True
        assert body["result"]["type"] == "quadcopter_frame"


@dataclass
class _DummyIR:
    units: str
    parameters: dict = field(default_factory=dict)

    @classmethod
    def from_request(cls, raw_parameters: dict) -> "_DummyIR":
        return cls(units="mm", parameters=dict(raw_parameters or {}))

    def to_dict(self) -> dict:
        return {
            "type": "dummy_test_part",
            "units": self.units,
            "parameters": self.parameters,
        }


def _build_dummy(ir: _DummyIR) -> Mesh:
    size = ir.parameters.get("size", 10.0)
    return box(center=(0, 0, 0), size=(size, size, size))


def _validate_dummy(ir: _DummyIR, mesh: Mesh) -> tuple[bool, dict]:
    return True, {"triangle_count": len(mesh.triangles)}


def test_engine_generates_a_second_registered_part_end_to_end():
    register_part(
        PartDefinition(
            name="dummy_test_part",
            description="in-test part proving the engine is registry-driven",
            parameters=(),
            ir_class=_DummyIR,
            builder=_build_dummy,
            validator=_validate_dummy,
            filename_stem=lambda ir: f"dummy_test_part_{int(ir.parameters.get('size', 10))}mm",
        )
    )
    try:
        engine = CADEngine()
        result = engine.generate(
            {"type": "dummy_test_part", "parameters": {"size": 7}, "outputs": ["stl"]}
        )
        assert result.success is True
        assert result.result["type"] == "dummy_test_part"
        assert result.result["triangle_count"] == 12
        assert result.validation.status == "VALIDATED"
        filenames = [path for path, _ in result.pending_artifacts]
        assert filenames and "dummy_test_part_7mm_" in filenames[0].rsplit(".stl", 1)[0]
    finally:
        unregister_part("dummy_test_part")
    assert "dummy_test_part" not in list_supported_types()


def test_default_frame_artifact_filename_still_prefixed():
    engine = CADEngine()
    result = engine.generate({"outputs": ["stl"]})
    path = Path(result.pending_artifacts[0][0])
    assert path.name.startswith("quadcopter_frame_50mm_")
    suffix = path.stem.rsplit("_", 1)[-1]
    assert len(suffix) == 8 and all(c in "0123456789abcdef" for c in suffix)
    assert get_part("quadcopter_frame").name == "quadcopter_frame"
