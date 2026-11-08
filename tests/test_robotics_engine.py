"""M18.2: Python robotics engine — FK and linear trajectory planning.

The contract mirrors the native one-shot ops (same keys, same six-decimal
rounding, same refusals), so these tests pin what a DAG sees on the
Linux bundle and native-less CI.
"""

from __future__ import annotations

import math

import pytest

from src.core.errors import CapabilityUnavailableError, RequestValidationError
from src.engines import registry as registry_module
from src.engines.pcb import PcbEngine
from src.engines.robotics.engine import RoboticsEngine
from src.engines.scaffold import ScaffoldEngine
from src.engines.simulation.engine import SimulationEngine


@pytest.fixture()
def engine() -> RoboticsEngine:
    return RoboticsEngine()


def test_describe_advertises_only_implemented_ops(engine):
    described = engine.describe()
    assert described["name"] == "robotics"
    assert set(described["capabilities"]) == {"kinematics", "trajectory_plan"}
    assert "status" not in described


def test_default_chain_reaches_x_two_meters(engine):
    result = engine.execute("kinematics", {"joint_angles": [0.0, 0.0]})
    data = result.result
    assert result.success is True
    assert data["chain_source"] == "engine_default_2link"
    assert data["joint_count"] == 2
    assert data["angles_source"] == "parameters"
    assert data["units"] == {"length": "m", "angle": "rad"}
    position = data["end_effector"]["position"]
    assert (position["x"], position["y"], position["z"]) == (2.0, 0.0, 0.0)
    assert data["end_effector"]["rotation"] == [
        [1.0, 0.0, 0.0],
        [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0],
    ]
    assert [frame["name"] for frame in data["frames"]] == ["link1", "link2"]
    assert [frame["index"] for frame in data["frames"]] == [1, 2]
    assert data["frames"][0]["position"] == {"x": 1.0, "y": 0.0, "z": 0.0}
    assert result.validation is not None
    assert result.validation.status == "VALIDATED"
    assert result.validation.checks["rule"] == "robotics.forward_kinematics"


def test_missing_angles_default_to_zero(engine):
    data = engine.execute("kinematics", {}).result
    assert data["angles_source"] == "default_zero"
    assert data["joint_angles"] == [0.0, 0.0]
    assert data["end_effector"]["position"]["x"] == 2.0


def test_first_joint_rotates_the_whole_chain(engine):
    data = engine.execute(
        "kinematics", {"joint_angles": [math.pi / 2.0, 0.0]}
    ).result
    position = data["end_effector"]["position"]
    assert position["x"] == pytest.approx(0.0, abs=1e-6)
    assert position["y"] == pytest.approx(2.0, abs=1e-6)
    assert data["frames"][0]["position"] == pytest.approx(
        {"x": 0.0, "y": 1.0, "z": 0.0}
    )


def test_dh_params_replace_the_default_chain(engine):
    data = engine.execute(
        "kinematics",
        {
            "dh_params": [{"a": 1.0, "alpha": 0.0, "d": 0.0}],
            "joint_angles": [math.pi / 2.0],
        },
    ).result
    assert data["chain_source"] == "parameters"
    assert data["joint_count"] == 1
    assert data["frames"][0]["name"] == "link_1"
    position = data["end_effector"]["position"]
    assert position["x"] == pytest.approx(0.0, abs=1e-6)
    assert position["y"] == pytest.approx(1.0, abs=1e-6)
    assert data["end_effector"]["rotation"][0] == pytest.approx([0.0, -1.0, 0.0])


def test_theta_offset_is_added_to_the_joint_angle(engine):
    data = engine.execute(
        "kinematics",
        {
            "dh_params": [
                {"a": 1.0, "alpha": 0.0, "d": 0.0, "theta_offset": math.pi / 2.0}
            ],
        },
    ).result
    position = data["end_effector"]["position"]
    assert position["x"] == pytest.approx(0.0, abs=1e-6)
    assert position["y"] == pytest.approx(1.0, abs=1e-6)


def test_prismatic_d_offsets_stack(engine):
    data = engine.execute(
        "kinematics",
        {
            "dh_params": [
                {"a": 0.0, "alpha": 0.0, "d": 0.5},
                {"a": 0.0, "alpha": 0.0, "d": 0.25},
            ],
        },
    ).result
    assert data["end_effector"]["position"] == {"x": 0.0, "y": 0.0, "z": 0.75}


def test_positions_are_rounded_to_six_decimals(engine):
    data = engine.execute(
        "kinematics",
        {
            "dh_params": [{"a": 1.0, "alpha": 0.0, "d": 0.0}],
            "joint_angles": [1.23456789],
        },
    ).result
    x = data["end_effector"]["position"]["x"]
    assert x == pytest.approx(math.cos(1.23456789), abs=5e-7)
    assert x == round(x, 6)


@pytest.mark.parametrize(
    "parameters, match",
    [
        ({"joint_angles": [0.0, 0.0, 0.0]}, "exactly 2 entries"),
        ({"joint_angles": []}, "array of 1 to 12"),
        ({"joint_angles": [0.0, "x"]}, "must be numeric"),
        ({"dh_params": []}, "non-empty"),
        ({"dh_params": [{"alpha": 0.0, "d": 0.0}]}, "missing 'a'"),
        (
            {"dh_params": [{"a": 101.0, "alpha": 0.0, "d": 0.0}]},
            "within ±100",
        ),
        (
            {"dh_params": [{"a": 1.0, "alpha": 13.0, "d": 0.0}]},
            "within ±12.6",
        ),
        ({"joint_angles": [1.0e7, 0.0]}, "within ±1e6"),
    ],
)
def test_kinematics_refuses_bad_inputs(engine, parameters, match):
    with pytest.raises(RequestValidationError, match=match):
        engine.execute("kinematics", parameters)


def test_trajectory_plan_matches_the_native_defaults(engine):
    result = engine.execute(
        "trajectory_plan",
        {
            "joint_start": [0.0, 0.0],
            "joint_goal": [1.0, 0.5],
            "duration_s": 2.0,
            "dt": 0.01,
        },
    )
    data = result.result
    assert result.success is True
    assert data["integrator"] == "simulation::integrate (closed_form)"
    assert data["joint_count"] == 2
    assert data["sample_count"] == 201
    assert data["start_reached"] is True
    assert data["goal_reached"] is True
    assert data["final_positions"] == pytest.approx([1.0, 0.5])
    assert data["trajectory"][0]["t"] == 0.0
    assert data["trajectory"][0]["positions"] == [0.0, 0.0]
    assert data["trajectory"][-1]["t"] == 2.0
    assert data["trajectory"][-1]["positions"] == pytest.approx([1.0, 0.5])
    assert data["trajectory"][-1]["velocities"] == pytest.approx([0.5, 0.25])
    assert len(data["trajectory"]) == 201
    assert result.validation is not None
    assert result.validation.status == "VALIDATED"
    assert result.validation.checks["rule"] == "robotics.trajectory"


def test_trajectory_plan_honors_custom_dt(engine):
    data = engine.execute(
        "trajectory_plan",
        {
            "joint_start": [0.0],
            "joint_goal": [1.0],
            "duration_s": 1.0,
            "dt": 0.5,
        },
    ).result
    assert data["sample_count"] == 3
    assert [sample["t"] for sample in data["trajectory"]] == [0.0, 0.5, 1.0]
    assert [sample["positions"][0] for sample in data["trajectory"]] == [0.0, 0.5, 1.0]
    assert data["final_positions"] == [1.0]


def test_trajectory_plan_downsamples_but_keeps_the_true_count(engine, monkeypatch):
    from src.engines.robotics import engine as engine_module

    monkeypatch.setattr(engine_module, "MAX_IN_MEMORY_SAMPLES", 10)
    data = engine.execute(
        "trajectory_plan",
        {
            "joint_start": [0.0],
            "joint_goal": [1.0],
            "duration_s": 1.0,
            "dt": 0.01,
        },
    ).result
    assert data["sample_count"] == 101
    assert len(data["trajectory"]) == 10
    assert data["trajectory"][-1]["t"] == 1.0


@pytest.mark.parametrize(
    "parameters, match",
    [
        ({"joint_goal": [1.0]}, "requires array 'joint_start'"),
        (
            {"joint_start": [0.0, 0.0], "joint_goal": [1.0]},
            "exactly 2 entries",
        ),
        (
            {"joint_start": [0.0], "joint_goal": [1.0], "duration_s": 0.0},
            r"\(0, 3600\]",
        ),
        (
            {"joint_start": [0.0], "joint_goal": [1.0], "duration_s": 3601.0},
            r"\(0, 3600\]",
        ),
        (
            {"joint_start": [0.0], "joint_goal": [1.0], "dt": 0.0},
            r"\(0, 10\]",
        ),
        (
            {"joint_start": [0.0], "joint_goal": [1.0], "dt": 11.0},
            r"\(0, 10\]",
        ),
        (
            {"joint_start": [0.0], "joint_goal": [1.0], "duration_s": 1.0, "dt": 2.0},
            "dt must not exceed duration_s",
        ),
        (
            {
                "joint_start": [0.0],
                "joint_goal": [1.0],
                "duration_s": 3600.0,
                "dt": 0.001,
            },
            "exceeds 1000000 steps",
        ),
    ],
)
def test_trajectory_plan_refuses_bad_inputs(engine, parameters, match):
    with pytest.raises(RequestValidationError, match=match):
        engine.execute("trajectory_plan", parameters)


def test_unknown_operation_refuses_with_capability_code(engine):
    with pytest.raises(CapabilityUnavailableError) as excinfo:
        engine.execute("inverse_kinematics", {})
    assert excinfo.value.code == "capability_unavailable"


def test_registry_uses_python_fallbacks_without_native(monkeypatch):
    monkeypatch.delenv("TRINITY_NATIVE_EXEC", raising=False)
    monkeypatch.setattr(registry_module.registry, "_engines", {})
    registry_module.bootstrap_engines()

    assert isinstance(registry_module.registry.get("robotics"), RoboticsEngine)
    assert isinstance(registry_module.registry.get("simulation"), SimulationEngine)
    assert isinstance(registry_module.registry.get("pcb"), PcbEngine)
