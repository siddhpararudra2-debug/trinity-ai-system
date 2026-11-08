"""M18.1: Python simulation engine — linear motion + spring chain.

The contract mirrors the native one-shot ops, so these tests pin what a
DAG sees: exact closed-form trajectories, energy conservation for the
RK4 chain, the sample/time-base shape, and honest refusals.
"""

from __future__ import annotations

import pytest

from src.core.errors import CapabilityUnavailableError, RequestValidationError
from src.engines import registry as registry_module
from src.engines.pcb import PcbEngine
from src.engines.simulation.engine import SimulationEngine


@pytest.fixture()
def engine() -> SimulationEngine:
    return SimulationEngine()


def _two_body(**overrides):
    parameters = {
        "dt": 0.001,
        "duration_s": 2.0,
        "method": "rk4",
        "spring_k_n_m": 10.0,
        "rest_length_m": 1.0,
        "objects": [
            {"mass_kg": 1.0, "position_m": {"x": 0.0}},
            {"mass_kg": 1.0, "position_m": {"x": 1.2}},
        ],
    }
    parameters.update(overrides)
    return parameters


def test_describe_advertises_only_implemented_ops(engine):
    described = engine.describe()
    assert described["name"] == "simulation"
    assert set(described["capabilities"]) == {"simulate", "simulate_spring_chain"}
    # A real engine, not a scaffold marker.
    assert "status" not in described


def test_linear_motion_matches_closed_form(engine):
    result = engine.execute(
        "simulate",
        {
            "duration_s": 2.0,
            "initial_velocity_m_s": 3.0,
            "acceleration_m_s2": 1.0,
        },
    )
    payload = result.result["result"]
    assert result.success is True
    assert payload["step_count"] == 200
    assert payload["sample_count"] == 201
    assert payload["method"] == "closed_form"
    final = payload["final_state"]
    assert final["position"]["x"] == pytest.approx(8.0)
    assert final["velocity"]["x"] == pytest.approx(5.0)
    assert payload["summary"]["final_position_m"]["x"] == pytest.approx(8.0)
    checks = result.result["checks"]
    assert checks["ok"] is True
    assert checks["closed_form_agreement"] is True


def test_position_mm_is_converted_to_meters(engine):
    result = engine.execute("simulate", {"duration_s": 1.0, "position_mm": 1500.0})
    final = result.result["result"]["final_state"]
    assert final["position"]["x"] == pytest.approx(1.5)


def test_initial_velocity_vector_and_dt_alias(engine):
    result = engine.execute(
        "simulate",
        {
            "dt_s": 0.1,
            "duration_s": 1.0,
            "initial": {"velocity": {"x": 2.0}},
        },
    )
    payload = result.result["result"]
    assert payload["step_count"] == 10
    times = [sample["t"] for sample in payload["samples"]]
    assert times == pytest.approx([i * 0.1 for i in range(11)])
    assert payload["final_state"]["position"]["x"] == pytest.approx(2.0)


def test_spring_chain_rk4_conserves_energy_and_momentum(engine):
    result = engine.execute("simulate_spring_chain", _two_body())
    payload = result.result["result"]
    assert payload["summary"]["bodies"] == 2
    assert payload["summary"]["method"] == "rk4"
    checks = result.result["checks"]
    assert checks["ok"] is True
    e0 = checks["details"]["initial_energy_J"]
    e1 = checks["details"]["final_energy_J"]
    assert e0 == pytest.approx(0.2)
    assert e1 == pytest.approx(e0, rel=0.01)
    # Symmetric stretch: the centre of mass must not drift.
    momentum = sum(state["velocity"]["x"] for state in payload["summary"]["final_states"])
    assert momentum == pytest.approx(0.0, abs=1e-6)


def test_spring_chain_euler_is_honest_about_its_method(engine):
    result = engine.execute(
        "simulate_spring_chain",
        _two_body(method="semi_implicit_euler"),
    )
    payload = result.result["result"]
    assert payload["method"] == "semi_implicit_euler"
    assert result.result["checks"]["ok"] is True


def test_downsampling_sets_the_flag_and_keeps_the_last_sample(engine, monkeypatch):
    from src.engines.simulation import engine as engine_module

    monkeypatch.setattr(engine_module, "MAX_IN_MEMORY_SAMPLES", 10)
    result = engine.execute("simulate", {"duration_s": 1.0, "dt": 0.01})
    payload = result.result["result"]
    assert payload["downsampled"] is True
    assert payload["sample_count"] == 101
    assert len(payload["samples"]) == 10
    assert payload["samples"][-1]["t"] == pytest.approx(1.0)


@pytest.mark.parametrize(
    "parameters",
    [
        {"dt": 0.0},
        {"duration_s": 0.0},
        {"duration_s": 7200.0},
        {"duration_s": 0.5, "dt": 1.0},
        {"method": "bogus"},
    ],
)
def test_bad_time_base_or_method_refuses(engine, parameters):
    with pytest.raises(RequestValidationError):
        engine.execute("simulate", parameters)


def test_spring_chain_requires_two_bodies(engine):
    with pytest.raises(RequestValidationError, match="at least two"):
        engine.execute(
            "simulate_spring_chain",
            {"objects": [{"mass_kg": 1.0, "position_m": {"x": 0.0}}]},
        )


def test_spring_chain_rejects_nonpositive_mass(engine):
    with pytest.raises(RequestValidationError, match="positive"):
        engine.execute(
            "simulate_spring_chain",
            {
                "objects": [
                    {"mass_kg": 1.0},
                    {"mass_kg": 0.0},
                ]
            },
        )


def test_unknown_operation_refuses_with_capability_code(engine):
    with pytest.raises(CapabilityUnavailableError) as excinfo:
        engine.execute("export_results", {})
    assert excinfo.value.code == "capability_unavailable"


def test_registry_uses_python_fallback_without_native(monkeypatch):
    monkeypatch.delenv("TRINITY_NATIVE_EXEC", raising=False)
    monkeypatch.setattr(registry_module.registry, "_engines", {})
    registry_module.bootstrap_engines()

    simulation = registry_module.registry.get("simulation")
    assert isinstance(simulation, SimulationEngine)
    assert isinstance(registry_module.registry.get("pcb"), PcbEngine)
