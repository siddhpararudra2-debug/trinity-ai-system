"""Phase 2: the four new parts — geometry, limits, filenames, registration."""

from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from src.core.errors import RequestValidationError
from src.engines.cad.engine import CADEngine
from src.engines.cad.parts import get_part, list_supported_types
from src.main import app

NEW_PARTS = ["enclosure", "l_bracket", "mounting_plate", "standoff"]
ALL_PARTS = [
    "enclosure",
    "l_bracket",
    "mounting_plate",
    "quadcopter_frame",
    "standoff",
]

EXPECTED = {
    "enclosure": {
        "params": {"length": 100, "width": 60, "height": 40, "wall_thickness": 2},
        "triangles": 60,
        "bbox": ((-50, -30, 0), (50, 30, 40)),
        "stem": "enclosure_100x60x40mm",
    },
    "l_bracket": {
        "params": {"base_length": 40, "height": 40, "width": 20, "thickness": 3},
        "triangles": 24,
        "bbox": ((-20, -10, 0), (20, 10, 40)),
        "stem": "l_bracket_40x40x20mm",
    },
    "mounting_plate": {
        "params": {
            "length": 100,
            "width": 60,
            "thickness": 3,
            "boss_diameter": 8,
            "boss_height": 2,
            "boss_inset": 8,
        },
        "triangles": 524,
        "bbox": ((-50, -30, 0), (50, 30, 5)),
        "stem": "mounting_plate_100x60mm",
    },
    "standoff": {
        "params": {
            "height": 10,
            "diameter": 6,
            "foot_diameter": 10,
            "foot_height": 1.5,
        },
        "triangles": 256,
        "bbox": ((-5, -5, 0), (5, 5, 10)),
        "stem": "standoff_10mm",
    },
}


def test_registry_lists_all_five_parts_sorted():
    assert list_supported_types() == ALL_PARTS


@pytest.mark.parametrize("part", NEW_PARTS)
def test_part_spec_mirrors_ir_defaults_in_declaration_order(part):
    expected = EXPECTED[part]["params"]
    definition = get_part(part)
    assert [p.name for p in definition.parameters] == list(expected)
    for spec, value in zip(definition.parameters, expected.values(), strict=True):
        assert spec.default == value
        assert spec.unit == "mm"
    assert definition.description

    ir = definition.ir_class.from_request({})
    assert ir.to_dict() == {"type": part, "units": "mm", "parameters": expected}


@pytest.mark.parametrize("part", NEW_PARTS)
def test_default_generation_is_validated_with_expected_geometry(part):
    engine = CADEngine()
    result = engine.generate({"type": part, "outputs": ["json"]})
    assert result.success is True, result.errors
    assert result.validation.status == "VALIDATED"
    assert result.result["type"] == part
    assert result.result["triangle_count"] == EXPECTED[part]["triangles"]

    (min_c, max_c) = result.result["bounding_box_mm"]
    exp_min, exp_max = EXPECTED[part]["bbox"]
    for actual, expected in zip(min_c, exp_min, strict=True):
        assert abs(actual - expected) < 1e-6
    for actual, expected in zip(max_c, exp_max, strict=True):
        assert abs(actual - expected) < 1e-6


@pytest.mark.parametrize("part", NEW_PARTS)
def test_default_artifact_filename_uses_part_stem(part):
    engine = CADEngine()
    result = engine.generate({"type": part, "outputs": ["stl"]})
    path = Path(result.pending_artifacts[0][0])
    assert path.name.startswith(EXPECTED[part]["stem"] + "_")
    assert path.suffix == ".stl"


def test_stl_size_follows_triangle_count_formula():
    engine = CADEngine()
    result = engine.generate({"type": "mounting_plate", "outputs": ["stl"]})
    path = Path(result.pending_artifacts[0][0])
    expected_size = 84 + 50 * EXPECTED["mounting_plate"]["triangles"]
    assert path.stat().st_size == expected_size


def test_fractional_dimensions_produce_dot_free_filename():
    engine = CADEngine()
    result = engine.generate(
        {
            "type": "enclosure",
            "parameters": {
                "length": 2.5,
                "width": 3.5,
                "height": 4.5,
                "wall_thickness": 1,
            },
            "outputs": ["stl"],
        }
    )
    assert result.success is True, result.errors
    path = Path(result.pending_artifacts[0][0])
    assert path.name.startswith("enclosure_2p5x3p5x4p5mm_")


@pytest.mark.parametrize("part", NEW_PARTS)
def test_from_request_rejects_unknown_parameters(part):
    with pytest.raises(RequestValidationError) as excinfo:
        get_part(part).ir_class.from_request({"bogus": 1})
    assert str(excinfo.value) == f"Unknown {part} parameter 'bogus'"
    assert excinfo.value.details == {"allowed": sorted(EXPECTED[part]["params"])}


# (part, parameters) pairs that must be rejected by from_request: absolute
# bounds (0 / negative / above max / below inclusive min), non-finite
# values, and the cross-field rules each part enforces.
REJECTIONS = [
    ("enclosure", {"length": 0}),
    ("enclosure", {"length": -5}),
    ("enclosure", {"length": 1001}),
    ("enclosure", {"width": 0}),
    ("enclosure", {"height": -1}),
    ("enclosure", {"wall_thickness": 0.5}),
    ("enclosure", {"wall_thickness": 51}),
    ("enclosure", {"length": float("inf")}),
    ("enclosure", {"wall_thickness": float("nan")}),
    ("enclosure", {"wall_thickness": 50}),
    ("enclosure", {"height": 2}),
    ("l_bracket", {"base_length": 0}),
    ("l_bracket", {"base_length": 1001}),
    ("l_bracket", {"height": -1}),
    ("l_bracket", {"width": 0}),
    ("l_bracket", {"thickness": 0.5}),
    ("l_bracket", {"thickness": 51}),
    ("l_bracket", {"thickness": 40}),
    ("l_bracket", {"thickness": 40, "base_length": 41}),
    ("mounting_plate", {"length": 0}),
    ("mounting_plate", {"length": 1001}),
    ("mounting_plate", {"width": -2}),
    ("mounting_plate", {"thickness": 0.9}),
    ("mounting_plate", {"thickness": 51}),
    ("mounting_plate", {"boss_diameter": 0.5}),
    ("mounting_plate", {"boss_diameter": 1001}),
    ("mounting_plate", {"boss_height": 0.5}),
    ("mounting_plate", {"boss_height": 51}),
    ("mounting_plate", {"boss_inset": 0}),
    ("mounting_plate", {"boss_inset": 1001}),
    ("mounting_plate", {"boss_inset": 1}),
    ("mounting_plate", {"boss_inset": 60}),
    ("standoff", {"height": 0}),
    ("standoff", {"height": -1}),
    ("standoff", {"height": 1001}),
    ("standoff", {"diameter": 0.5}),
    ("standoff", {"diameter": 1001}),
    ("standoff", {"foot_diameter": 0}),
    ("standoff", {"foot_diameter": 1001}),
    ("standoff", {"foot_height": 0.5}),
    ("standoff", {"foot_height": 1001}),
    ("standoff", {"foot_height": 11}),
]


@pytest.mark.parametrize(
    ("part", "params"),
    REJECTIONS,
    ids=[f"{part}-{params}" for part, params in REJECTIONS],
)
def test_out_of_range_parameters_are_rejected(part, params):
    with pytest.raises(RequestValidationError):
        get_part(part).ir_class.from_request(params)


# (part, parameters) pairs proving the exact bound values are usable:
# maxima and inclusive minima, with other parameters raised where a
# cross-field rule would otherwise make the bound unreachable.
ACCEPTANCES = [
    ("enclosure", {"length": 1000}),
    ("enclosure", {"width": 1000}),
    ("enclosure", {"height": 1000}),
    ("enclosure", {"wall_thickness": 1}),
    ("enclosure", {"wall_thickness": 50, "length": 200, "width": 150, "height": 60}),
    ("l_bracket", {"base_length": 1000}),
    ("l_bracket", {"height": 1000}),
    ("l_bracket", {"width": 1000}),
    ("l_bracket", {"thickness": 1}),
    ("l_bracket", {"thickness": 50, "base_length": 60, "height": 60}),
    ("mounting_plate", {"length": 1000}),
    ("mounting_plate", {"width": 1000}),
    ("mounting_plate", {"thickness": 1}),
    ("mounting_plate", {"thickness": 50}),
    ("mounting_plate", {"boss_diameter": 1}),
    (
        "mounting_plate",
        {"boss_diameter": 1000, "length": 1000, "width": 1000, "boss_inset": 500},
    ),
    ("mounting_plate", {"boss_height": 1}),
    ("mounting_plate", {"boss_height": 50}),
    ("standoff", {"height": 1000}),
    ("standoff", {"diameter": 1}),
    ("standoff", {"diameter": 1000}),
    ("standoff", {"foot_diameter": 1}),
    ("standoff", {"foot_diameter": 1000}),
    ("standoff", {"foot_height": 1}),
    ("standoff", {"foot_height": 1000, "height": 1000}),
]


@pytest.mark.parametrize(
    ("part", "params"),
    ACCEPTANCES,
    ids=[f"{part}-{params}" for part, params in ACCEPTANCES],
)
def test_bound_values_are_accepted_and_validated(part, params):
    engine = CADEngine()
    result = engine.generate({"type": part, "parameters": params, "outputs": ["json"]})
    assert result.success is True, result.errors
    assert result.validation.status == "VALIDATED"


def test_http_generate_rejects_cross_field_violation():
    with TestClient(app) as client:
        resp = client.post(
            "/api/cad/generate",
            json={"type": "enclosure", "parameters": {"wall_thickness": 50}},
        )
        body = resp.json()
        assert resp.status_code == 200
        assert body["success"] is False
        assert body["errors"][0]["code"] == "request_validation_error"


def test_http_generate_rejects_unknown_parameter_with_allowed_list():
    with TestClient(app) as client:
        resp = client.post(
            "/api/cad/generate",
            json={"type": "standoff", "parameters": {"bogus": 1}},
        )
        body = resp.json()
        assert resp.status_code == 200
        assert body["success"] is False
        assert body["errors"][0]["code"] == "request_validation_error"
        assert body["errors"][0]["details"]["allowed"] == [
            "diameter",
            "foot_diameter",
            "foot_height",
            "height",
        ]
