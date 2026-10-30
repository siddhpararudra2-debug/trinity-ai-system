"""Parameter families tests (M14): catalog-driven variants with honest
per-member truth. Real builders, real validators - no geometry stubs."""

from __future__ import annotations

import pytest

from src.core.errors import RequestValidationError
from src.engines.cad.engine import CADEngine
from src.engines.cad.families import MAX_FAMILY_MEMBERS, build_family
from src.engines.cad.parts import get_part


def _family_params(part_name: str, param: str, values, base=None):
    part = get_part(part_name)
    return build_family(part, base or {}, param, values)


def test_unknown_family_parameter_raises_with_supported_list():
    with pytest.raises(RequestValidationError) as excinfo:
        _family_params("quadcopter_frame", "nonsense_param", [1.0])
    supported = excinfo.value.details["supported_parameters"]
    assert "arm_width" in supported and "plate_thickness" in supported


def test_values_input_gates():
    with pytest.raises(RequestValidationError, match="non-empty list"):
        _family_params("quadcopter_frame", "arm_width", [])
    with pytest.raises(RequestValidationError, match="at most"):
        _family_params("quadcopter_frame", "arm_width", [1.0] * (MAX_FAMILY_MEMBERS + 1))
    with pytest.raises(RequestValidationError, match="finite numbers"):
        _family_params("quadcopter_frame", "arm_width", [3.0, float("nan")])
    with pytest.raises(RequestValidationError, match="finite numbers"):
        _family_params("quadcopter_frame", "arm_width", [True])


def test_family_builds_real_variants_end_to_end():
    out = _family_params("quadcopter_frame", "arm_width", [3.0, 5.0, 8.0])
    assert out["type"] == "quadcopter_frame"
    assert out["family_parameter"] == "arm_width"
    assert out["count"] == 3 and out["valid_count"] == 3
    variants = {m["variant"] for m in out["members"]}
    assert variants == {"arm_width=3", "arm_width=5", "arm_width=8"}
    for member in out["members"]:
        assert member["valid"] is True
        assert member["reason"] is None
        assert member["triangle_count"] > 0 and member["checksum"]
        # the family parameter is really the one driving each variant
        assert member["parameters"]["arm_width"] == float(
            member["variant"].split("=")[1]
        )
    # different variants => different geometry => different checksums
    checksums = {m["checksum"] for m in out["members"]}
    assert len(checksums) == 3
    # the outer silhouette is driven by overall_size, not arm_width, so
    # the bounding box may match while the member content differs - the
    # checksum is the geometric-truth claim, not the bbox.


def test_bound_violating_member_is_truthfully_invalid_not_dropped():
    out = _family_params("quadcopter_frame", "plate_thickness", [1.5, 60.0])
    assert out["count"] == 2 and out["valid_count"] == 1
    bad = out["members"][1]
    assert bad["valid"] is False
    # the padding engine's own range rule fires, not the parameter bound:
    # the reason is the part's real request-time verdict, verbatim.
    assert bad["reason"] and "engineering range" in bad["reason"]
    assert bad["checksum"] is None and bad["triangle_count"] is None
    good = out["members"][0]
    assert good["valid"] is True and good["checksum"]


def test_checksum_is_stable_for_the_same_variant():
    first = _family_params("quadcopter_frame", "arm_width", [6.0])
    second = _family_params("quadcopter_frame", "arm_width", [6.0])
    assert first["members"][0]["checksum"] == second["members"][0]["checksum"]
    assert first["members"][0]["triangle_count"] == second["members"][0]["triangle_count"]


def test_engine_execute_families_route():
    engine = CADEngine()
    result = engine.execute(
        "families",
        {
            "type": "quadcopter_frame",
            "parameters": {"overall_size": 150.0},
            "family": {"parameter": "arm_width", "values": [3.0, 5.0]},
        },
    )
    assert result.success is True
    assert result.operation == "families"
    out = result.result
    assert out["count"] == 2 and out["valid_count"] == 2
    assert all(m["variant"].startswith("arm_width=") for m in out["members"])
    assert result.engine == "cad"


def test_engine_execute_unknown_operation_still_refuses():
    engine = CADEngine()
    with pytest.raises(RequestValidationError, match="no operation"):
        engine.execute("transmogrify", {})
