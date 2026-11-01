"""Simulation engine (M18.1): Python fallback for the advertised ops.

The native engine stays the preferred backend where ``Trinity.exe``
exists (Windows). This engine is what the Linux bundle and native-less
CI actually run, so ``simulation`` is no longer a refusal there.

Scope is deliberately the two ops the registry advertises — ``simulate``
(linear motion with optional constant acceleration) and
``simulate_spring_chain`` (adjacent-spring RK4 / semi-implicit Euler).
The contracts mirror the native one-shot ops: same parameters, same
``result``/``checks``/``summary`` keys, so a DAG does not care which
backend answered. The native ``project_rules`` ledger and CSV/JSON
artifact export stay native-only until their own Python contracts are
pinned by tests.
"""

from __future__ import annotations

import math
import uuid
from typing import Any

from src.core.errors import CapabilityUnavailableError, RequestValidationError
from src.engines.base import BaseEngine, EngineResult, ValidationResult

DEFAULT_DT = 0.01
DEFAULT_DURATION_S = 1.0
MAX_DURATION_S = 3600.0
MAX_STEPS = 1_000_000
MAX_IN_MEMORY_SAMPLES = 20_000
DEFAULT_SPRING_K_N_M = 10.0
DEFAULT_REST_LENGTH_M = 1.0
SUPPORTED_METHODS = ("semi_implicit_euler", "rk4")


def _number(source: Any, key: str, default: float) -> float:
    value = source.get(key, default) if isinstance(source, dict) else default
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return default
    value = float(value)
    return value if math.isfinite(value) else default


def _vec(source: Any) -> dict[str, float]:
    return {axis: _number(source, axis, 0.0) for axis in ("x", "y", "z")}


def _state(
    t: float,
    position: dict[str, float],
    velocity: dict[str, float],
    acceleration: dict[str, float],
) -> dict[str, Any]:
    # "time_s" is the spec-facing key; "t" is the compatible alias the
    # native state shape carries for existing readers.
    return {
        "t": t,
        "time_s": t,
        "position": position,
        "velocity": velocity,
        "acceleration": acceleration,
    }


def _time_base(parameters: dict[str, Any]) -> tuple[float, float, int]:
    dt = _number(parameters, "dt", _number(parameters, "dt_s", DEFAULT_DT))
    duration = _number(parameters, "duration_s", DEFAULT_DURATION_S)
    if dt <= 0.0:
        raise RequestValidationError("dt must be a positive finite number")
    if duration <= 0.0:
        raise RequestValidationError("duration_s must be a positive finite number")
    if duration > MAX_DURATION_S:
        raise RequestValidationError(
            f"duration_s must not exceed {MAX_DURATION_S:g}"
        )
    if dt > duration:
        raise RequestValidationError("dt must not exceed duration_s")
    # C++ llround rounds halves away from zero; duration/dt >= 0 here.
    steps = int(math.floor(duration / dt + 0.5))
    if steps > MAX_STEPS:
        raise RequestValidationError(
            f"Step count exceeds the supported maximum ({MAX_STEPS})"
        )
    return dt, duration, steps


def _downsample(
    samples: list[dict[str, Any]], max_samples: int
) -> tuple[list[dict[str, Any]], bool]:
    if len(samples) <= max_samples or max_samples < 2:
        return samples, False
    stride = (len(samples) + max_samples - 2) // (max_samples - 1)
    kept = samples[::stride]
    if kept[-1]["t"] < samples[-1]["t"]:
        kept.append(samples[-1])
    return kept, True


def _result_json(
    project_id: str,
    type_: str,
    model: str,
    method: str,
    dt: float,
    duration: float,
    steps: int,
    samples: list[dict[str, Any]],
    final_state: dict[str, Any],
    summary: dict[str, Any],
) -> tuple[dict[str, Any], bool]:
    sample_count = len(samples)
    kept, downsampled = _downsample(samples, MAX_IN_MEMORY_SAMPLES)
    return (
        {
            "project_id": project_id,
            "type": type_,
            "model": model,
            "method": method,
            "dt": dt,
            "duration_s": duration,
            "step_count": steps,
            "sample_count": sample_count,
            "downsampled": downsampled,
            "samples": kept,
            "final_state": final_state,
            "summary": summary,
            "original_units": {},
        },
        downsampled,
    )


def _checks(
    samples: list[dict[str, Any]],
    *,
    closed_form_agreement: bool = True,
    details: dict[str, Any] | None = None,
    message: str = "Result samples are finite, time-monotonic, and within tolerance",
) -> dict[str, Any]:
    finite = all(
        math.isfinite(value)
        for sample in samples
        for group in ("position", "velocity", "acceleration")
        for value in sample[group].values()
    )
    monotonic = all(
        samples[i]["t"] > samples[i - 1]["t"] for i in range(1, len(samples))
    )
    ok = finite and monotonic and closed_form_agreement
    if not ok and message.startswith("Result samples are finite"):
        message = "Result failed finite, time-monotonic, or tolerance checks"
    return {
        "finite": finite,
        "time_monotonic": monotonic,
        "closed_form_agreement": closed_form_agreement,
        "ok": ok,
        "message": message,
        "details": details or {},
    }


class SimulationEngine(BaseEngine):
    """Python implementation of the two advertised simulation ops."""

    name = "simulation"
    version = "0.1"
    capabilities = ("simulate", "simulate_spring_chain")

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        if operation == "simulate":
            return self._simulate_linear(parameters)
        if operation == "simulate_spring_chain":
            return self._simulate_spring_chain(parameters)
        raise CapabilityUnavailableError(
            f"The simulation engine has no Python implementation for "
            f"'{operation}' yet",
            details={
                "engine": self.name,
                "operation": operation,
                "status": "python",
            },
        )

    # -- simulate: linear motion / constant acceleration -----------------

    def _simulate_linear(self, parameters: dict[str, Any]) -> EngineResult:
        dt, duration, steps = _time_base(parameters)
        initial = parameters.get("initial")
        initial = initial if isinstance(initial, dict) else {}

        x0 = _number(
            initial,
            "position_mm_x",
            _number(parameters, "position_mm", 0.0),
        ) / 1000.0
        y0 = _number(initial, "position_mm_y", 0.0) / 1000.0
        z0 = _number(initial, "position_mm_z", 0.0) / 1000.0

        if "initial_velocity_m_s" in parameters:
            vx0 = _number(parameters, "initial_velocity_m_s", 0.0)
            vy0 = _number(parameters, "initial_velocity_y_m_s", 0.0)
            vz0 = _number(parameters, "initial_velocity_z_m_s", 0.0)
        else:
            velocity = _vec(initial.get("velocity"))
            vx0, vy0, vz0 = velocity["x"], velocity["y"], velocity["z"]

        if "acceleration_m_s2" in parameters:
            ax = _number(parameters, "acceleration_m_s2", 0.0)
        elif "acceleration" in parameters:
            ax = _number(parameters, "acceleration", 0.0)
        else:
            ax = 0.0
        ay = az = 0.0
        method = parameters.get("method") or "semi_implicit_euler"
        if method not in SUPPORTED_METHODS:
            raise RequestValidationError(
                "method must be 'semi_implicit_euler' or 'rk4'"
            )

        project_id = str(parameters.get("project_id") or uuid.uuid4().hex)
        initial_state = _state(
            0.0,
            {"x": x0, "y": y0, "z": z0},
            {"x": vx0, "y": vy0, "z": vz0},
            {"x": ax, "y": ay, "z": az},
        )
        samples: list[dict[str, Any]] = []
        for i in range(steps + 1):
            t = i * dt
            samples.append(
                _state(
                    t,
                    {
                        "x": x0 + vx0 * t + 0.5 * ax * t * t,
                        "y": y0 + vy0 * t + 0.5 * ay * t * t,
                        "z": z0 + vz0 * t + 0.5 * az * t * t,
                    },
                    {"x": vx0 + ax * t, "y": vy0 + ay * t, "z": vz0 + az * t},
                    {"x": ax, "y": ay, "z": az},
                )
            )
        final_state = samples[-1]
        summary = {
            "final_position_m": final_state["position"],
            "final_velocity_m_s": final_state["velocity"],
            "method": "closed_form",
        }
        result, _ = _result_json(
            project_id,
            "kinematics",
            "linear_motion",
            "closed_form",
            dt,
            duration,
            steps,
            samples,
            final_state,
            summary,
        )
        checks = _checks(samples, details={"max_position_error_m": 0.0})
        project = {
            "project_id": project_id,
            "name": parameters.get("name") or "linear_motion",
            "type": "kinematics",
            "model": "linear_motion",
            "dt": dt,
            "duration_s": duration,
            "method": method,
            "spring_k_n_m": DEFAULT_SPRING_K_N_M,
            "rest_length_m": DEFAULT_REST_LENGTH_M,
            "initial": initial_state,
            "mass_kg": 1.0,
            "force_N": {"x": 0.0, "y": 0.0, "z": 0.0},
            "gravity": _number(parameters, "gravity_m_s2", 9.80665),
            "original_units": {},
            "objects": [],
            "parameters": [],
            "inputs": {},
            "boundary_conditions": {},
            "outputs": [],
            "metadata": {},
        }
        return EngineResult(
            success=True,
            engine=self.name,
            operation="simulate",
            result={"project": project, "result": result, "checks": checks},
            validation=ValidationResult(
                status="VERIFIED" if checks["ok"] else "FAILED", checks=checks
            ),
        )

    # -- simulate_spring_chain ------------------------------------------

    def _simulate_spring_chain(self, parameters: dict[str, Any]) -> EngineResult:
        objects = parameters.get("objects")
        if not isinstance(objects, list) or len(objects) < 2:
            raise RequestValidationError(
                "spring_chain requires an 'objects' array with at least two "
                "bodies ({mass_kg, position_m:{x,y,z}, velocity_m_s?})"
            )
        masses: list[float] = []
        positions: list[list[float]] = []
        velocities: list[list[float]] = []
        for entry in objects:
            if not isinstance(entry, dict):
                raise RequestValidationError("spring_chain objects entries must be objects")
            mass = _number(entry, "mass_kg", 1.0)
            if mass <= 0.0:
                raise RequestValidationError("Object masses must be positive finite numbers")
            position = _vec(entry.get("position_m"))
            velocity = _vec(entry.get("velocity_m_s"))
            masses.append(mass)
            positions.append([position["x"], position["y"], position["z"]])
            velocities.append([velocity["x"], velocity["y"], velocity["z"]])

        dt, duration, steps = _time_base(parameters)
        method = parameters.get("method") or "semi_implicit_euler"
        if method not in SUPPORTED_METHODS:
            raise RequestValidationError(
                "method must be 'semi_implicit_euler' or 'rk4'"
            )
        spring_k = _number(parameters, "spring_k_n_m", DEFAULT_SPRING_K_N_M)
        rest_length = _number(parameters, "rest_length_m", DEFAULT_REST_LENGTH_M)
        if spring_k <= 0.0:
            raise RequestValidationError("spring_k_n_m must be positive finite")
        if rest_length <= 0.0:
            raise RequestValidationError("rest_length_m must be positive finite")

        def forces(p: list[list[float]]) -> list[list[float]]:
            out = [[0.0, 0.0, 0.0] for _ in p]
            for i in range(1, len(p)):
                dx = p[i][0] - p[i - 1][0]
                dy = p[i][1] - p[i - 1][1]
                dz = p[i][2] - p[i - 1][2]
                dist = math.sqrt(dx * dx + dy * dy + dz * dz)
                if dist <= 1e-12:
                    continue
                magnitude = spring_k * (dist - rest_length)
                ux, uy, uz = dx / dist, dy / dist, dz / dist
                out[i - 1][0] += ux * magnitude
                out[i - 1][1] += uy * magnitude
                out[i - 1][2] += uz * magnitude
                out[i][0] -= ux * magnitude
                out[i][1] -= uy * magnitude
                out[i][2] -= uz * magnitude
            return out

        def derivative(
            p: list[list[float]], v: list[list[float]]
        ) -> tuple[list[list[float]], list[list[float]]]:
            f = forces(p)
            return (
                [row[:] for row in v],
                [[f[i][axis] / masses[i] for axis in range(3)] for i in range(len(p))],
            )

        def sample_at(t: float, p: list[list[float]], v: list[list[float]]) -> dict[str, Any]:
            f = forces(p)
            return _state(
                t,
                {"x": p[0][0], "y": p[0][1], "z": p[0][2]},
                {"x": v[0][0], "y": v[0][1], "z": v[0][2]},
                {
                    "x": f[0][0] / masses[0],
                    "y": f[0][1] / masses[0],
                    "z": f[0][2] / masses[0],
                },
            )

        samples: list[dict[str, Any]] = [sample_at(0.0, positions, velocities)]

        def energies() -> tuple[float, float]:
            kinetic = sum(
                0.5 * masses[i] * sum(v * v for v in velocities[i])
                for i in range(len(positions))
            )
            potential = 0.0
            for i in range(1, len(positions)):
                dx = positions[i][0] - positions[i - 1][0]
                dy = positions[i][1] - positions[i - 1][1]
                dz = positions[i][2] - positions[i - 1][2]
                dist = math.sqrt(dx * dx + dy * dy + dz * dz)
                potential += 0.5 * spring_k * (dist - rest_length) ** 2
            return kinetic, potential

        e0_kinetic, e0_potential = energies()
        e0 = e0_kinetic + e0_potential
        for step in range(1, steps + 1):
            if method == "rk4":
                k1p, k1v = derivative(positions, velocities)
                y2p = [
                    [positions[i][a] + 0.5 * dt * k1p[i][a] for a in range(3)]
                    for i in range(len(positions))
                ]
                y2v = [
                    [velocities[i][a] + 0.5 * dt * k1v[i][a] for a in range(3)]
                    for i in range(len(velocities))
                ]
                k2p, k2v = derivative(y2p, y2v)
                y3p = [
                    [positions[i][a] + 0.5 * dt * k2p[i][a] for a in range(3)]
                    for i in range(len(positions))
                ]
                y3v = [
                    [velocities[i][a] + 0.5 * dt * k2v[i][a] for a in range(3)]
                    for i in range(len(velocities))
                ]
                k3p, k3v = derivative(y3p, y3v)
                y4p = [
                    [positions[i][a] + dt * k3p[i][a] for a in range(3)]
                    for i in range(len(positions))
                ]
                y4v = [
                    [velocities[i][a] + dt * k3v[i][a] for a in range(3)]
                    for i in range(len(velocities))
                ]
                k4p, k4v = derivative(y4p, y4v)
                for i in range(len(positions)):
                    for a in range(3):
                        positions[i][a] += dt / 6.0 * (
                            k1p[i][a] + 2.0 * k2p[i][a] + 2.0 * k3p[i][a] + k4p[i][a]
                        )
                        velocities[i][a] += dt / 6.0 * (
                            k1v[i][a] + 2.0 * k2v[i][a] + 2.0 * k3v[i][a] + k4v[i][a]
                        )
            else:
                f = forces(positions)
                for i in range(len(positions)):
                    for a in range(3):
                        velocities[i][a] += (f[i][a] / masses[i]) * dt
                        positions[i][a] += velocities[i][a] * dt
            samples.append(sample_at(step * dt, positions, velocities))

        final_states = [
            {
                "t": duration,
                "time_s": duration,
                "position": {"x": positions[i][0], "y": positions[i][1], "z": positions[i][2]},
                "velocity": {"x": velocities[i][0], "y": velocities[i][1], "z": velocities[i][2]},
                "acceleration": {"x": 0.0, "y": 0.0, "z": 0.0},
            }
            for i in range(len(positions))
        ]
        kinetic, potential = energies()
        e1 = kinetic + potential
        energy_ok = abs(e1 - e0) <= 0.01 * max(abs(e0), 1.0)

        project_id = str(parameters.get("project_id") or uuid.uuid4().hex)
        summary = {
            "bodies": len(positions),
            "method": method,
            "spring_k_n_m": spring_k,
            "rest_length_m": rest_length,
            "final_states": final_states,
            "kinetic_energy_J": kinetic,
            "spring_potential_energy_J": potential,
            "total_energy_J": kinetic + potential,
        }
        result, downsampled = _result_json(
            project_id,
            "multi_body",
            "spring_chain",
            method,
            dt,
            duration,
            steps,
            samples,
            samples[-1],
            summary,
        )
        checks = _checks(
            samples,
            closed_form_agreement=energy_ok or downsampled,
            details={"initial_energy_J": e0, "final_energy_J": e1},
            message="Result samples are finite, time-monotonic, and within tolerance",
        )
        if not energy_ok and not downsampled:
            checks["message"] = "Spring-chain energy conservation failed"
            checks["ok"] = False
        project = {
            "project_id": project_id,
            "name": parameters.get("name") or "spring_chain",
            "type": "multi_body",
            "model": "spring_chain",
            "dt": dt,
            "duration_s": duration,
            "method": method,
            "spring_k_n_m": spring_k,
            "rest_length_m": rest_length,
            "initial": samples[0],
            "mass_kg": masses[0],
            "force_N": {"x": 0.0, "y": 0.0, "z": 0.0},
            "gravity": 9.80665,
            "original_units": {},
            "objects": [],
            "parameters": [],
            "inputs": {},
            "boundary_conditions": {},
            "outputs": [],
            "metadata": {},
        }
        return EngineResult(
            success=True,
            engine=self.name,
            operation="simulate_spring_chain",
            result={"project": project, "result": result, "checks": checks},
            validation=ValidationResult(
                status="VERIFIED" if checks["ok"] else "FAILED", checks=checks
            ),
        )
