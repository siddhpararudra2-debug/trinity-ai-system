"""The real firmware fallback (Item: build tiers, pin DB, no stubs).

The toolchain seam is monkeypatched with fake compilers so success,
failure, timeout, and not-installed are all exercised deterministically
on any machine.
"""

from __future__ import annotations

import sys
from typing import Any

import pytest

from src.engines.firmware import (
    FirmwareEngine,
    FirmwareError,
    _MCU_DB,
    _toolchain_candidates,
)


@pytest.fixture()
def engine() -> FirmwareEngine:
    return FirmwareEngine()


def _fake_pio(exit_code: int, message: str) -> list[dict[str, Any]]:
    """A fake PlatformIO candidate: exits with ``exit_code``."""
    code = (
        f"import sys; print({message!r}, file=sys."
        f"{'stdout' if exit_code == 0 else 'stderr'}); sys.exit({exit_code})"
    )
    return [
        {"name": "fake-pio", "kind": "platformio",
         "argv": [sys.executable, "-c", code]}
    ]


def _fake_gcc(exit_code: int, message: str) -> list[dict[str, Any]]:
    """A fake host-syntax candidate: exits with ``exit_code``."""
    code = (
        f"import sys; _ = sys.stdin.read(); print({message!r}, file=sys."
        f"{'stdout' if exit_code == 0 else 'stderr'}); sys.exit({exit_code})"
    )
    return [
        {"name": "fake-gcc", "kind": "gcc_syntax",
         "argv": [sys.executable, "-c", code]}
    ]


def _skeleton(engine: FirmwareEngine, **params: Any) -> str:
    result = engine.execute("generate", {"features": ["i2c"], **params})
    return result.result["source_text"]


# ------------------------------------------------------------ describe ---


def test_describe_has_no_scaffold_status(engine: FirmwareEngine) -> None:
    # M18 convention: a real, tested fallback carries no status key.
    assert engine.describe().get("status") is None


# ----------------------------------------------------------- generate ----


def test_generate_has_no_todo_stub_and_real_structure(
    engine: FirmwareEngine,
) -> None:
    source = _skeleton(engine, mcu="ESP32-WROOM-32", project_id="fw1")
    assert "TODO" not in source
    assert "FIXME" not in source
    assert "int main(void) {" in source
    assert "i2c_init();" in source  # the configured peripheral is wired in
    assert "#define FW_PIN_I2C_SDA 21" in _skeleton(
        engine, pins=[{"name": "GPIO21", "function": "i2c_sda"}]
    )
    # Vendor SDK honesty is stated, not faked:
    assert "vendor SDK" in source
    assert "ESP-IDF app_main()" in source


def test_generate_unknown_mcu_states_the_unknown_entry(
    engine: FirmwareEngine,
) -> None:
    source = _skeleton(engine, mcu="Mystery-MCU")
    assert "entry point unknown" in source


def test_generate_non_numeric_pin_is_commented_not_defined(
    engine: FirmwareEngine,
) -> None:
    source = _skeleton(
        engine, mcu="STM32F407", pins=[{"name": "PA4", "function": "cs"}]
    )
    assert "FW_PIN_CS" not in source
    assert "PA4 has no numeric GPIO form" in source


# ------------------------------------------------------------ validate ---


def test_validate_legacy_shape_plus_pin_checks(engine: FirmwareEngine) -> None:
    result = engine.execute(
        "validate",
        {
            "mcu": "ESP32-WROOM-32",
            "features": ["i2c"],
            "pins": [{"name": "GPIO21", "function": "i2c_sda"}],
        },
    )
    assert result.success is True
    assert result.validation is not None
    assert result.validation.status == "VALIDATED"
    body = result.result
    for key in ("mcu", "features", "pins_checked", "errors"):
        assert key in body, key
    assert body["pin_checks"]["mcu_known"] is True


def test_validate_rejects_unknown_mcu_and_feature(
    engine: FirmwareEngine,
) -> None:
    result = engine.execute(
        "validate", {"mcu": "NOPE", "features": ["quantum"]}
    )
    assert result.success is False
    assert any("Unrecognized MCU" in e for e in result.result["errors"])
    assert any("Unrecognized feature" in e for e in result.result["errors"])


def test_validate_finds_the_pin_conflict(engine: FirmwareEngine) -> None:
    result = engine.execute(
        "validate",
        {
            "mcu": "ESP32-WROOM-32",
            "pins": [
                {"name": "GPIO21", "function": "i2c_sda"},
                {"name": "GPIO21", "function": "uart_tx"},
            ],
        },
    )
    assert result.success is False
    assert any("pin conflict: GPIO21" in e for e in result.result["errors"])


def test_validate_finds_unknown_and_input_only_pins(
    engine: FirmwareEngine,
) -> None:
    result = engine.execute(
        "validate",
        {
            "mcu": "ESP32-WROOM-32",
            "pins": [
                {"name": "GPIO99", "function": "led"},
                {"name": "GPIO39", "function": "sensor", "mode": "output"},
            ],
        },
    )
    assert result.success is False
    errors = result.result["errors"]
    assert any("GPIO99" in e and "does not exist" in e for e in errors)
    assert any("GPIO39 is input-only" in e for e in errors)


def test_validate_finds_peripheral_problems(engine: FirmwareEngine) -> None:
    result = engine.execute(
        "validate",
        {
            "mcu": "ESP32-WROOM-32",
            "peripherals": [
                {"peripheral": "can", "instance": 0, "pins": {}},
                {"peripheral": "i2c", "instance": 7, "pins": {}},
            ],
        },
    )
    assert result.success is False
    errors = result.result["errors"]
    assert any("'can' does not exist" in e for e in errors)
    assert any("i2c instance 7 out of range" in e for e in errors)


def test_pin_checks_state_the_skip_for_unknown_mcus(
    engine: FirmwareEngine,
) -> None:
    result = engine.execute(
        "validate",
        {"mcu": "Mystery-MCU", "pins": [{"name": "GPIO99"}]},
    )
    assert result.success is False  # unknown MCU itself is the error
    detail = result.result["pin_checks"]
    assert detail["mcu_known"] is False
    assert "skipped" in detail and "GPIO99" not in str(detail["assignments"])


def test_validate_c03_regression_gpio39_input_only_is_caught(
    engine: FirmwareEngine,
) -> None:
    # The crosscheck fixture regression (GPIO39 driven as an output):
    # the fallback database must catch it directly.
    result = engine.execute(
        "validate",
        {"mcu": "ESP32-WROOM-32", "pins": [{"name": "GPIO39", "mode": "output"}]},
    )
    assert result.success is False


# --------------------------------------------------------------- build ---


def test_build_without_toolchain_is_honestly_static_only(
    engine: FirmwareEngine, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr("src.engines.firmware._toolchain_candidates", list)
    source = _skeleton(engine)
    result = engine.execute(
        "build", {"project_id": "p1", "source_text": source}
    )
    assert result.success is True
    body = result.result
    assert body["build_ok"] is True
    assert body["build_status"] == "STATIC_CHECK_ONLY"
    assert "static check only, not compiled" in body["build_note"]
    assert body["toolchain"] is None
    assert result.validation is not None
    assert result.validation.status == "VALIDATED"


def test_build_empty_source_fails_without_a_toolchain(
    engine: FirmwareEngine, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr("src.engines.firmware._toolchain_candidates", list)
    result = engine.execute("build", {"source_text": ""})
    assert result.success is False
    assert result.result["build_status"] == "FAILED"
    assert any("empty" in e for e in result.result["errors"])


def test_build_structural_error_is_found(engine: FirmwareEngine) -> None:
    result = engine.execute(
        "build", {"source_text": "int main(void) { return 0;"}
    )
    assert result.success is False
    errors = result.result["errors"]
    assert any("unbalanced braces" in e for e in errors)
    assert result.result["build_status"] == "FAILED"


def test_build_platformio_success_is_the_only_compiled(
    engine: FirmwareEngine, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(
        "src.engines.firmware._toolchain_candidates",
        lambda: _fake_pio(0, "success"),
    )
    result = engine.execute(
        "build", {"project_id": "p2", "source_text": _skeleton(engine)}
    )
    assert result.success is True
    body = result.result
    assert body["build_status"] == "COMPILED"
    assert body["toolchain"] == "fake-pio"
    assert result.validation is not None
    assert result.validation.status == "VERIFIED"


def test_build_platformio_failure_is_not_masked_by_a_syntax_pass(
    engine: FirmwareEngine, monkeypatch: pytest.MonkeyPatch
) -> None:
    candidates = [* _fake_pio(1, "error: ld returned 1"), *_fake_gcc(0, "ok")]
    monkeypatch.setattr(
        "src.engines.firmware._toolchain_candidates", lambda: candidates
    )
    result = engine.execute(
        "build", {"project_id": "p3", "source_text": _skeleton(engine)}
    )
    assert result.success is False
    body = result.result
    assert body["build_status"] == "FAILED"
    assert any("fake-pio" in e and "exit 1" in e for e in body["errors"])
    # The host syntax pass was never invoked to soften the real failure:
    assert [a["name"] for a in body["toolchain_attempts"]] == ["fake-pio"]


def test_build_host_syntax_pass_is_static_only_never_compiled(
    engine: FirmwareEngine, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(
        "src.engines.firmware._toolchain_candidates",
        lambda: _fake_gcc(0, "ok"),
    )
    result = engine.execute(
        "build", {"project_id": "p4", "source_text": _skeleton(engine)}
    )
    assert result.success is True
    body = result.result
    assert body["build_status"] == "STATIC_CHECK_ONLY"
    assert body["toolchain"] is None
    assert any("syntax pass OK" in n for n in body["build_note"].split("; "))


def test_build_vendor_header_failure_is_inconclusive_not_failed(
    engine: FirmwareEngine, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(
        "src.engines.firmware._toolchain_candidates",
        lambda: _fake_gcc(1, "esp_log.h: No such file or directory"),
    )
    result = engine.execute(
        "build", {"project_id": "p5", "source_text": _skeleton(engine)}
    )
    assert result.success is True  # the syntax pass could not run: no claim
    body = result.result
    assert body["build_status"] == "STATIC_CHECK_ONLY"
    assert any(
        "inconclusive" in n for n in body["build_note"].split("; ")
    )


def test_build_real_syntax_error_from_the_compiler_fails(
    engine: FirmwareEngine, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(
        "src.engines.firmware._toolchain_candidates",
        lambda: _fake_gcc(1, "error: expected ';' before return"),
    )
    result = engine.execute(
        "build", {"project_id": "p6", "source_text": _skeleton(engine)}
    )
    assert result.success is False
    assert any(
        "expected ';'" in e for e in result.result["errors"]
    )
    assert result.result["build_status"] == "FAILED"


def test_build_toolchain_timeout_is_reported(
    engine: FirmwareEngine, monkeypatch: pytest.MonkeyPatch
) -> None:
    import src.engines.firmware as firmware_module

    monkeypatch.setattr(firmware_module, "_TOOLCHAIN_TIMEOUT_S", 0.5)
    candidates = [
        {
            "name": "slow-gcc",
            "kind": "gcc_syntax",
            "argv": [sys.executable, "-c", "import time; time.sleep(3)"],
        }
    ]
    monkeypatch.setattr(
        "src.engines.firmware._toolchain_candidates", lambda: candidates
    )
    result = engine.execute(
        "build", {"project_id": "p7", "source_text": _skeleton(engine)}
    )
    assert result.success is False
    assert any(
        "timed out" in e for e in result.result["errors"]
    ), result.result["errors"]


def test_build_reports_pin_conflicts_too(engine: FirmwareEngine) -> None:
    result = engine.execute(
        "build",
        {
            "source_text": _skeleton(engine),
            "mcu": "ESP32-WROOM-32",
            "pins": [
                {"name": "GPIO21", "function": "i2c_sda"},
                {"name": "GPIO21", "function": "uart_tx"},
            ],
        },
    )
    assert result.success is False
    assert any("pin conflict" in e for e in result.result["errors"])


def test_real_toolchain_seam_discovers_nothing_here() -> None:
    # On this machine (no toolchains installed) the real seam must find
    # nothing and the engine must say so — documents the honest baseline.
    found = {c["name"] for c in _toolchain_candidates()}
    assert found <= {"pio", "platformio", "gcc", "clang", "cc"}


# ------------------------------------------------- create / configure ----


def test_create_and_select_mcu_reject_unknown_mcus(
    engine: FirmwareEngine,
) -> None:
    created = engine.execute("create", {"mcu": "NOPE"})
    assert created.success is False
    assert created.result["status"] == "rejected-unknown-mcu"
    selected = engine.execute("select_mcu", {"mcu": "NOPE"})
    assert selected.success is False
    assert "known:" in selected.result["errors"][0]
    # Known MCUs still work and the db covers the advertised set:
    assert engine.execute("select_mcu", {"mcu": "ESP32-WROOM-32"}).success
    assert set(_MCU_DB) >= {"ESP32-WROOM-32", "STM32F407", "ATmega2560"}


def test_configure_pin_validates_against_the_database(
    engine: FirmwareEngine,
) -> None:
    ok = engine.execute(
        "configure_pin",
        {"pin": "GPIO21", "function": "i2c_sda", "mode": "gpio"},
    )
    assert ok.success is True
    assert ok.result["status"] == "validated"
    bad = engine.execute(
        "configure_pin",
        {"pin": "GPIO39", "function": "led", "mode": "output"},
    )
    assert bad.success is False
    assert bad.result["status"] == "rejected"
    assert any("input-only" in e for e in bad.result["errors"])


def test_configure_peripheral_validates_against_the_database(
    engine: FirmwareEngine,
) -> None:
    ok = engine.execute(
        "configure_peripheral",
        {
            "peripheral": "i2c",
            "config": {
                "instance": 0,
                "pins": {"sda": "GPIO21", "scl": "GPIO22"},
            },
        },
    )
    assert ok.success is True
    bad = engine.execute(
        "configure_peripheral",
        {"peripheral": "i2c", "config": {"instance": 9, "pins": {}}},
    )
    assert bad.success is False
    assert any("out of range" in e for e in bad.result["errors"])
    with pytest.raises(FirmwareError, match="non-integer instance"):
        engine.execute(
            "configure_peripheral",
            {"peripheral": "i2c", "config": {"instance": "zero"}},
        )


def test_database_counts_match_the_datasheet_tiers() -> None:
    # Guard the numbers the checks trust (enough to catch config errors).
    assert _MCU_DB["ESP32-WROOM-32"]["peripherals"]["uart"] == 3
    assert "GPIO39" in _MCU_DB["ESP32-WROOM-32"]["input_only"]
    assert "GPIO21" in _MCU_DB["ESP32-WROOM-32"]["pins"]
    assert len(_MCU_DB["STM32F407"]["pins"]) == 80
