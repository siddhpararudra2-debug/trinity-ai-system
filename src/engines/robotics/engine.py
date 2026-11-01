"""Robotics engine (M18.2): Python fallback for the advertised ops.

The native engine stays preferred wherever ``Trinity.exe`` exists
(Windows). This engine is what the Linux bundle and native-less CI run,
so ``robotics`` no longer refuses there.

Scope is exactly the two ops the registry advertises: ``kinematics``
(standard-DH forward kinematics, with the native default two-link chain)
and ``trajectory_plan`` (closed-form linear joint-space motion). The
result keys mirror the native one-shot ops byte for byte (``chain_source``,
``joint_count``, ``end_effector``, ``frames`` / ``integrator``,
``sample_count``, ``trajectory``, ...), including the six-decimal
rounding the native engine applies. The joint/link IR operations
(``create_robot``, ``inverse_kinematics``, URDF export, ...) are not
advertised and stay native-only.
"""

from __future__ import annotations

import math
from typing import Any

from src.core.errors import CapabilityUnavailableError, RequestValidationError
from src.engines.base import BaseEngine, EngineResult, ValidationResult

MAX_JOINTS = 12
MAX_LINK_M = 100.0
MAX_ANGLE_RAD = 12.6  # ~4*pi
MAX_JOINT_VALUE = 1.0e6
MAX_DURATION_S = 3600.0
MAX_TRAJECTORY_STEPS = 1_000_000
DEFAULT_DURATION_S = 2.0
DEFAULT_DT = 0.01
MAX_IN_MEMORY_SAMPLES = 20_000


def _round6(value: float) -> float:
    # C++ std::round rounds halves away from zero; Python's round() is
    # banker's rounding, which would make the fallback differ at ties.
    scaled = value * 1_000_000.0
    if scaled >= 0.0:
        return math.floor(scaled + 0.5) / 1_000_000.0
    return math.ceil(scaled - 0.5) / 1_000_000.0


def _finite(value: Any, key: str, operation: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise RequestValidationError(
            f"Parameter '{key}' must be numeric",
            details={"operation": operation, "param": key},
        )
    number = float(value)
    if not math.isfinite(number):
        raise RequestValidationError(
            f"Parameter '{key}' must be finite", details={"param": key}
        )
    return number


def _default_chain() -> list[dict[str, Any]]:
    return [
        {"a": 1.0, "alpha": 0.0, "d": 0.0, "theta_offset": 0.0, "name": "link1"},
        {"a": 1.0, "alpha": 0.0, "d": 0.0, "theta_offset": 0.0, "name": "link2"},
    ]


def _parse_chain(
    parameters: dict[str, Any], operation: str
) -> tuple[list[dict[str, Any]], str]:
    chain = _default_chain()
    source = "engine_default_2link"
    if parameters.get("dh_params") is not None:
        raw = parameters["dh_params"]
        if not isinstance(raw, list) or not raw:
            raise RequestValidationError(
                "dh_params must be a non-empty array",
                details={"operation": operation, "param": "dh_params"},
            )
        chain = raw
        source = "parameters"
    if len(chain) > MAX_JOINTS:
        raise RequestValidationError(
            f"dh_params must contain between 1 and {MAX_JOINTS} rows",
            details={"param": "dh_params"},
        )
    links: list[dict[str, Any]] = []
    for index, row in enumerate(chain):
        if not isinstance(row, dict):
            raise RequestValidationError(
                f"dh_params[{index}] must be an object",
                details={"param": "dh_params"},
            )
        for key in ("a", "alpha", "d"):
            if key not in row:
                raise RequestValidationError(
                    f"dh_params[{index}] is missing '{key}'",
                    details={"param": "dh_params"},
                )
        link = {
            "a": _finite(row["a"], "dh_params.a", operation),
            "alpha": _finite(row["alpha"], "dh_params.alpha", operation),
            "d": _finite(row["d"], "dh_params.d", operation),
            "theta_offset": 0.0,
            "name": row.get("name") if isinstance(row.get("name"), str) else f"link_{index + 1}",
        }
        if row.get("theta_offset") is not None:
            link["theta_offset"] = _finite(
                row["theta_offset"], "dh_params.theta_offset", operation
            )
        if abs(link["a"]) > MAX_LINK_M or abs(link["d"]) > MAX_LINK_M:
            raise RequestValidationError(
                f"dh_params link lengths must be within ±{MAX_LINK_M:g} m",
                details={"param": "dh_params"},
            )
        if abs(link["alpha"]) > MAX_ANGLE_RAD or abs(link["theta_offset"]) > MAX_ANGLE_RAD:
            raise RequestValidationError(
                f"dh_params angles must be within ±{MAX_ANGLE_RAD:g} rad",
                details={"param": "dh_params"},
            )
        links.append(link)
    return links, source


def _parse_joint_array(
    parameters: dict[str, Any],
    key: str,
    operation: str,
    *,
    required: bool,
    expected_length: int,
) -> list[float]:
    raw = parameters.get(key)
    if raw is None:
        if required:
            raise RequestValidationError(
                f"Operation '{operation}' requires array '{key}'",
                details={"operation": operation, "param": key},
            )
        return [0.0] * expected_length
    if not isinstance(raw, list) or not raw or len(raw) > MAX_JOINTS:
        raise RequestValidationError(
            f"{key} must be an array of 1 to {MAX_JOINTS} numbers",
            details={"operation": operation, "param": key},
        )
    if expected_length > 0 and len(raw) != expected_length:
        raise RequestValidationError(
            f"{key} must have exactly {expected_length} entries",
            details={"param": key, "expected": expected_length},
        )
    values: list[float] = []
    for index, item in enumerate(raw):
        value = _finite(item, f"{key}[{index}]", operation)
        if abs(value) > MAX_JOINT_VALUE:
            raise RequestValidationError(
                f"{key} values must be within ±1e6", details={"param": key}
            )
        values.append(value)
    return values


def _dh_matrix(link: dict[str, Any], theta: float) -> list[list[float]]:
    ct, st = math.cos(theta), math.sin(theta)
    ca, sa = math.cos(link["alpha"]), math.sin(link["alpha"])
    return [
        [ct, -st * ca, st * sa, link["a"] * ct],
        [st, ct * ca, -ct * sa, link["a"] * st],
        [0.0, sa, ca, link["d"]],
        [0.0, 0.0, 0.0, 1.0],
    ]


def _mul4(left: list[list[float]], right: list[list[float]]) -> list[list[float]]:
    return [
        [sum(left[r][k] * right[k][c] for k in range(4)) for c in range(4)]
        for r in range(4)
    ]


def _identity4() -> list[list[float]]:
    return [[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]


def _downsample_indices(count: int, max_samples: int) -> list[int]:
    if count <= max_samples or max_samples < 2:
        return list(range(count))
    stride = (count + max_samples - 2) // (max_samples - 1)
    kept = list(range(0, count, stride))
    if kept[-1] != count - 1:
        kept.append(count - 1)
    return kept


class RoboticsEngine(BaseEngine):
    """Python implementation of the two advertised robotics ops."""

    name = "robotics"
    version = "0.1"
    capabilities = ("kinematics", "trajectory_plan")

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        if operation == "kinematics":
            return self._kinematics(parameters)
        if operation == "trajectory_plan":
            return self._trajectory_plan(parameters)
        raise CapabilityUnavailableError(
            f"The robotics engine has no Python implementation for "
            f"'{operation}' yet",
            details={
                "engine": self.name,
                "operation": operation,
                "status": "python",
            },
        )

    # -- kinematics: standard-DH forward kinematics ----------------------

    def _kinematics(self, parameters: dict[str, Any]) -> EngineResult:
        links, chain_source = _parse_chain(parameters, "forward_kinematics")
        angles_given = parameters.get("joint_angles") is not None
        angles = _parse_joint_array(
            parameters,
            "joint_angles",
            "forward_kinematics",
            required=False,
            expected_length=len(links),
        )

        total = _identity4()
        frames: list[dict[str, Any]] = []
        for index, link in enumerate(links):
            total = _mul4(total, _dh_matrix(link, link["theta_offset"] + angles[index]))
            frames.append(
                {
                    "index": index + 1,
                    "name": link["name"],
                    "position": {
                        "x": _round6(total[0][3]),
                        "y": _round6(total[1][3]),
                        "z": _round6(total[2][3]),
                    },
                }
            )
        rotation = [
            [_round6(total[r][c]) for c in range(3)] for r in range(3)
        ]
        data = {
            "chain_source": chain_source,
            "joint_count": len(links),
            "joint_angles": [_round6(value) for value in angles],
            "angles_source": "parameters" if angles_given else "default_zero",
            "units": {"length": "m", "angle": "rad"},
            "end_effector": {
                "position": {
                    "x": _round6(total[0][3]),
                    "y": _round6(total[1][3]),
                    "z": _round6(total[2][3]),
                },
                "rotation": rotation,
            },
            "frames": frames,
        }
        return EngineResult(
            success=True,
            engine=self.name,
            operation="kinematics",
            result=data,
            validation=ValidationResult(
                status="VALIDATED",
                checks={
                    "engine": "robotics",
                    "operation": "kinematics",
                    "rule": "robotics.forward_kinematics",
                    "message": "Deterministic DH forward kinematics computed",
                },
            ),
        )

    # -- trajectory_plan: linear joint-space motion ----------------------

    def _trajectory_plan(self, parameters: dict[str, Any]) -> EngineResult:
        start = _parse_joint_array(
            parameters,
            "joint_start",
            "plan_trajectory",
            required=True,
            expected_length=0,
        )
        goal = _parse_joint_array(
            parameters,
            "joint_goal",
            "plan_trajectory",
            required=True,
            expected_length=len(start),
        )

        duration = DEFAULT_DURATION_S
        if parameters.get("duration_s") is not None:
            duration = _finite(parameters["duration_s"], "duration_s", "plan_trajectory")
        if not (0.0 < duration <= MAX_DURATION_S):
            raise RequestValidationError(
                f"duration_s must be in (0, {MAX_DURATION_S:g}]",
                details={"param": "duration_s"},
            )
        dt = DEFAULT_DT
        if parameters.get("dt") is not None:
            dt = _finite(parameters["dt"], "dt", "plan_trajectory")
        if not (0.0 < dt <= 10.0):
            raise RequestValidationError(
                "dt must be in (0, 10]", details={"param": "dt"}
            )
        raw_steps = duration / dt
        if raw_steps > MAX_TRAJECTORY_STEPS:
            raise RequestValidationError(
                f"duration_s/dt exceeds {MAX_TRAJECTORY_STEPS} steps",
                details={"param": "duration_s"},
            )
        if dt > duration:
            # The native path refuses this inside simulation::integrate;
            # surfacing it here keeps the refusal reason identical.
            raise RequestValidationError(
                "dt must not exceed duration_s", details={"param": "dt"}
            )
        steps = int(math.floor(raw_steps + 0.5))
        sample_count = steps + 1
        velocities = [(goal[k] - start[k]) / duration for k in range(len(start))]
        positions = [
            [start[k] + velocities[k] * (i * dt) for k in range(len(start))]
            for i in range(sample_count)
        ]
        final_positions = positions[-1]

        start_reached = all(
            abs(positions[0][k] - start[k]) <= 1e-6 for k in range(len(start))
        )
        goal_reached = all(
            abs(final_positions[k] - goal[k]) <= 1e-6 for k in range(len(start))
        )

        kept = _downsample_indices(sample_count, MAX_IN_MEMORY_SAMPLES)
        trajectory = [
            {
                "t": _round6(i * dt),
                "positions": [_round6(positions[i][k]) for k in range(len(start))],
                "velocities": [_round6(velocities[k]) for k in range(len(start))],
            }
            for i in kept
        ]

        ok = start_reached and goal_reached and bool(trajectory)
        message = (
            "Trajectory endpoints verified against integrator output"
            if ok
            else (
                "Trajectory has no samples"
                if not trajectory
                else "Trajectory endpoints do not match requested start/goal"
            )
        )
        data = {
            "integrator": "simulation::integrate (closed_form)",
            "duration_s": duration,
            "dt": dt,
            "sample_count": sample_count,
            "joint_count": len(start),
            "joint_start": [_round6(value) for value in start],
            "joint_goal": [_round6(value) for value in goal],
            "final_positions": [_round6(value) for value in final_positions],
            "start_reached": start_reached,
            "goal_reached": goal_reached,
            "trajectory": trajectory,
        }
        return EngineResult(
            success=True,
            engine=self.name,
            operation="trajectory_plan",
            result=data,
            validation=ValidationResult(
                status="VALIDATED" if ok else "FAILED",
                checks={
                    "engine": "robotics",
                    "operation": "trajectory_plan",
                    "rule": "robotics.trajectory",
                    "message": message,
                },
            ),
        )
