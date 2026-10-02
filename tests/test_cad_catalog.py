"""Phase 3: GET /api/cad/catalog — shape, purity and spec/engine drift guards.

The catalog is only useful if it describes what the engine actually
enforces, so the drift guards read the payload and push its numbers back
through the real request path: declared defaults must equal the IR
defaults and generate VALIDATED geometry, every bound must reject what
it promises to reject, and every declared bound value must be usable
(where a cross-field rule would otherwise make it unreachable, the probe
carries an override that keeps the part valid).
"""

import json
from typing import Any

import pytest
from fastapi.testclient import TestClient

from src.engines.cad.engine import (
    CADEngine,
    NOT_YET_SUPPORTED_FORMATS,
    STEP_UNAVAILABLE_REASON,
)
from src.engines.cad.parts import get_part, list_supported_types
from src.main import app

PART_PARAM_COUNTS = {
    "enclosure": 4,
    "l_bracket": 4,
    "mounting_plate": 6,
    "quadcopter_frame": 7,
    "standoff": 4,
}


def _catalog() -> dict[str, Any]:
    with TestClient(app) as client:
        response = client.get("/api/cad/catalog")
    assert response.status_code == 200
    return response.json()


def _generate(part: str, parameters: dict[str, Any]) -> dict[str, Any]:
    with TestClient(app) as client:
        response = client.post(
            "/api/cad/generate",
            json={"type": part, "parameters": parameters, "outputs": ["json"]},
        )
    assert response.status_code == 200
    return response.json()


# ------------------------------------------------------------------- shape ---


def test_catalog_lists_every_registered_part_sorted_with_parameter_counts():
    payload = _catalog()
    names = [part["name"] for part in payload["parts"]]
    assert names == list_supported_types()
    assert names == sorted(names)
    counts = {part["name"]: len(part["parameters"]) for part in payload["parts"]}
    assert counts == PART_PARAM_COUNTS
    assert sum(counts.values()) == 25


def test_catalog_parameter_metadata_is_complete_and_well_formed():
    payload = _catalog()
    total = 0
    for part in payload["parts"]:
        assert part["title"].strip()
        assert part["description"].strip()
        assert part["rules"]
        assert all(isinstance(rule, str) and rule.strip() for rule in part["rules"])
        seen: set[str] = set()
        for spec in part["parameters"]:
            total += 1
            assert spec["label"].strip()
            assert spec["description"].strip()
            if part["name"] == "quadcopter_frame" and spec["name"] == "motor_count":
                assert spec["unit"] == "count"
            else:
                assert spec["unit"] == "mm"
            assert isinstance(spec["min_exclusive"], bool)
            assert isinstance(spec["integer"], bool)
            if spec["min_exclusive"]:
                assert spec["default"] > spec["min"]
            else:
                assert spec["default"] >= spec["min"]
            if spec["max"] is not None:
                assert spec["default"] <= spec["max"]
            if spec["allowed_values"] is not None:
                assert spec["default"] in spec["allowed_values"]
            assert spec["name"] not in seen
            seen.add(spec["name"])
    assert total == 25


def test_catalog_declares_default_outputs_and_format_availability():
    payload = _catalog()
    assert payload["default_outputs"] == ["stl", "json"]
    outputs = payload["outputs"]
    assert list(outputs) == ["stl", "glb", "json", "step"]
    for fmt in ("stl", "glb", "json"):
        assert outputs[fmt] == {"available": True}
    assert outputs["step"] == {
        "available": False,
        "reason": STEP_UNAVAILABLE_REASON,
    }
    unavailable = {fmt for fmt, spec in outputs.items() if not spec["available"]}
    assert unavailable == NOT_YET_SUPPORTED_FORMATS


def test_motor_count_is_exactly_integer_four():
    payload = _catalog()
    quad = next(p for p in payload["parts"] if p["name"] == "quadcopter_frame")
    motor = next(s for s in quad["parameters"] if s["name"] == "motor_count")
    assert isinstance(motor["default"], int)
    assert motor["default"] == 4
    assert isinstance(motor["min"], int)
    assert motor["min"] == 4
    assert motor["max"] is None
    assert motor["integer"] is True
    assert motor["allowed_values"] == [4]
    assert isinstance(motor["allowed_values"][0], int)


def test_catalog_payload_contains_no_non_finite_json_constants():
    with TestClient(app) as client:
        text = client.get("/api/cad/catalog").text

    def _reject(value: str) -> None:
        raise AssertionError(f"non-finite JSON constant in payload: {value}")

    parsed = json.loads(text, parse_constant=_reject)
    assert [part["name"] for part in parsed["parts"]] == list_supported_types()


def test_catalog_is_deterministic_across_calls():
    with TestClient(app) as client:
        first = client.get("/api/cad/catalog")
        second = client.get("/api/cad/catalog")
    assert first.status_code == second.status_code == 200
    assert first.text == second.text


def test_catalog_endpoint_has_no_side_effects():
    with TestClient(app) as client:
        jobs_before = client.get("/api/jobs").json()
        engines_before = client.get("/api/engines").json()
        assert client.get("/api/cad/catalog").status_code == 200
        jobs_after = client.get("/api/jobs").json()
        engines_after = client.get("/api/engines").json()
    assert jobs_after == jobs_before
    assert engines_after == engines_before


def test_catalog_route_is_documented_in_openapi():
    with TestClient(app) as client:
        spec = client.get("/openapi.json").json()
    assert "get" in spec["paths"].get("/api/cad/catalog", {})


def test_step_unavailability_reason_matches_the_engine_constant():
    payload = _catalog()
    with TestClient(app) as client:
        response = client.post(
            "/api/cad/generate",
            json={"type": "enclosure", "parameters": {}, "outputs": ["step"]},
        )
    body = response.json()
    assert body["success"] is True, body.get("errors")
    assert body["result"]["unavailable_formats"]["step"] == STEP_UNAVAILABLE_REASON
    assert payload["outputs"]["step"]["reason"] == STEP_UNAVAILABLE_REASON


# ---------------------------------------------------- drift guard: defaults ---


def test_catalog_defaults_match_ir_defaults_in_declaration_order():
    payload = _catalog()
    for part in payload["parts"]:
        expected = get_part(part["name"]).ir_class.from_request({}).parameters
        assert [spec["name"] for spec in part["parameters"]] == list(expected)
        for spec in part["parameters"]:
            assert spec["default"] == expected[spec["name"]]


def test_catalog_defaults_generate_validated_geometry():
    payload = _catalog()
    for part in payload["parts"]:
        defaults = {spec["name"]: spec["default"] for spec in part["parameters"]}
        result = CADEngine().generate(
            {"type": part["name"], "parameters": defaults, "outputs": ["json"]}
        )
        assert result.success is True, result.errors
        assert result.validation.status == "VALIDATED"


# --------------------------------------------- drift guard: rejections ---


def _below_min_probe(spec: dict[str, Any]) -> float | int:
    if spec["integer"] or spec["allowed_values"] is not None:
        return spec["min"] - 1
    if spec["min_exclusive"]:
        return spec["min"]
    return spec["min"] / 2


def test_every_declared_bound_rejects_values_below_it():
    payload = _catalog()
    for part in payload["parts"]:
        for spec in part["parameters"]:
            probe = _below_min_probe(spec)
            context = (part["name"], spec["name"], probe)
            body = _generate(part["name"], {spec["name"]: probe})
            assert body["success"] is False, context
            assert body["errors"][0]["code"] in (
                "request_validation_error",
                "geometry_validation_error",
            ), (context, body["errors"])


# ------------------------------------------------- drift guard: acceptances ---

# Cross-field fixes that make an otherwise-unreachable declared bound usable.
ACCEPTANCE_OVERRIDES: dict[tuple[str, str], dict[str, Any]] = {
    ("enclosure", "wall_thickness"): {"length": 200, "width": 150, "height": 60},
    ("l_bracket", "thickness"): {"base_length": 60, "height": 60},
    ("mounting_plate", "boss_diameter"): {
        "length": 1000,
        "width": 1000,
        "boss_inset": 500,
    },
    ("standoff", "foot_height"): {"height": 1000},
}

# Declared bounds that cannot be exercised at all, with the arithmetic
# that proves it — these would otherwise silently weaken the guard.
ACCEPTANCE_SKIPS: dict[tuple[str, str], str] = {
    (
        "mounting_plate",
        "boss_inset",
    ): "max 1000 is unreachable: boss_diameter / 2 + boss_inset must be at "
    "most min(length, width) <= 1000, and the smallest boss_diameter is 1, "
    "so any probe reaches at least 1000.5.",
}


def _acceptance_probes() -> list[tuple[str, str, dict[str, Any]]]:
    probes: list[tuple[str, str, dict[str, Any]]] = []
    for part in _catalog()["parts"]:
        for spec in part["parameters"]:
            bound_probes: list[float | int] = []
            if spec["max"] is not None:
                bound_probes.append(spec["max"])
            if not spec["min_exclusive"]:
                bound_probes.append(spec["min"])
            for value in bound_probes:
                key = (part["name"], spec["name"])
                if key in ACCEPTANCE_SKIPS:
                    continue
                parameters = {spec["name"]: value}
                parameters.update(ACCEPTANCE_OVERRIDES.get(key, {}))
                probes.append((part["name"], spec["name"], parameters))
    return probes


@pytest.mark.parametrize(
    ("part", "parameters"),
    [(part, parameters) for part, _, parameters in _acceptance_probes()],
    ids=[
        f"{part}-{parameter}-{parameters}"
        for part, parameter, parameters in _acceptance_probes()
    ],
)
def test_every_declared_bound_value_is_usable(part, parameters):
    body = _generate(part, parameters)
    assert body["success"] is True, (parameters, body.get("errors"))
    assert body["validation"]["status"] == "VALIDATED"


def test_every_declared_bound_is_either_probed_or_documented_as_skipped():
    probed = {(part, parameter) for part, parameter, _ in _acceptance_probes()}
    declared = {
        (part["name"], spec["name"])
        for part in _catalog()["parts"]
        for spec in part["parameters"]
        if spec["max"] is not None or not spec["min_exclusive"]
    }
    skips = set(ACCEPTANCE_SKIPS)
    assert skips <= declared
    assert not (probed & skips)
    assert probed | skips == declared
