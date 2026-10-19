"""Native bridge (Item 1, Stage 4b/4c): op/error/status tables + mocked transport.

Live tests (real Trinity.exe) are gated on TRINITY_NATIVE_EXEC and live in
test_native_bridge_live.py so the default suite never needs the binary.
"""

import json
import subprocess
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from src.core.errors import CapabilityUnavailableError, EngineExecutionError
from src.engines.native_bridge import (
    ERROR_CODE_MAP,
    EXEC_ENV_VAR,
    NATIVE_ENGINES,
    OP_MAP,
    STATUS_MAP,
    NativeBridgeEngine,
    native_exe_path,
)
from src.engines.scaffold import ScaffoldEngine
from src.main import app

# Every scaffolded capability must be either bridged or explicitly documented
# as unmapped (no native equivalent). This test fails loudly if someone adds
# a capability without deciding its fate.
DOCUMENTED_UNMAPPED = {
    ("firmware", "test"),
    ("firmware", "compile"),
    ("pcb", "inspect"),
    ("vision", "ocr"),
    ("vision", "document_parse"),
    ("vision", "geometry_extract"),
}

NATIVE_CODES = {
    "TrinityError",
    "RequestValidationError",
    "EngineNotFoundError",
    "EngineExecutionError",
    "GeometryValidationError",
    "JobNotFoundError",
    "ArtifactNotFoundError",
    "CapabilityUnavailableError",
}


def test_every_scaffold_capability_is_decided():
    for engine, capabilities in NATIVE_ENGINES.items():
        for op in capabilities:
            mapped = op in OP_MAP.get(engine, {})
            documented = (engine, op) in DOCUMENTED_UNMAPPED
            assert mapped or documented, f"{engine}/{op}: mapped or documented?"
    assert set(OP_MAP) <= set(NATIVE_ENGINES)


def test_error_code_table_is_complete():
    assert set(ERROR_CODE_MAP) == NATIVE_CODES
    assert ERROR_CODE_MAP["EngineNotFoundError"] == "engine_not_found"
    assert ERROR_CODE_MAP["CapabilityUnavailableError"] == "capability_unavailable"


def test_status_map_collapses_invalid_to_failed():
    assert STATUS_MAP["INVALID"] == "FAILED"
    for ok in ("GENERATED", "VALIDATED", "VERIFIED"):
        assert STATUS_MAP[ok] == ok


def test_describe_reports_bridged_not_scaffolded():
    engine = NativeBridgeEngine("pcb", NATIVE_ENGINES["pcb"])
    described = engine.describe()
    assert described["status"] == "bridged"
    assert described["capabilities"] == ScaffoldEngine("pcb", NATIVE_ENGINES["pcb"]).describe()["capabilities"]


def test_exe_path_none_without_env(monkeypatch):
    monkeypatch.delenv(EXEC_ENV_VAR, raising=False)
    assert native_exe_path() is None


def test_unmapped_op_stays_501():
    engine = NativeBridgeEngine("pcb", NATIVE_ENGINES["pcb"])
    with pytest.raises(CapabilityUnavailableError, match="no bridged implementation"):
        engine.execute("inspect", {})


def _envelope(**overrides):
    base = {
        "success": True,
        "engine": "research",
        "operation": "search",
        "job_id": "",
        "request_id": "r1",
        "result": {"hit_count": 1},
        "artifacts": [],
        "pending_artifacts": [],
        "validation": {"status": "VALIDATED", "checks": {"hits": 1}},
        "errors": [],
    }
    base.update(overrides)
    return base


class _Proc:
    def __init__(self, stdout, returncode=0, stderr=""):
        self.stdout = stdout
        self.returncode = returncode
        self.stderr = stderr


def _bridge(monkeypatch, tmp_path, payload, binary="C:/fake/Trinity.exe"):
    # Bypass the is_file() gate: transport behavior is what these tests pin.
    monkeypatch.setattr(
        "src.engines.native_bridge.native_exe_path", lambda: binary
    )
    monkeypatch.setattr(
        "src.engines.native_bridge.subprocess.run",
        lambda *a, **k: _Proc(json.dumps(payload)),
    )
    return NativeBridgeEngine("research", NATIVE_ENGINES["research"])


def _patch_exe(monkeypatch, binary="C:/fake/Trinity.exe"):
    monkeypatch.setattr(
        "src.engines.native_bridge.native_exe_path", lambda: binary
    )


def test_execute_success_maps_envelope(monkeypatch, tmp_path):
    engine = _bridge(monkeypatch, tmp_path, _envelope())
    out = engine.execute("search", {"query": "q"})
    assert out.success is True
    assert out.operation == "search"  # Python op echoed, not native op
    assert out.result["hit_count"] == 1
    assert out.validation is not None and out.validation.status == "VALIDATED"
    assert out.errors == []


def test_execute_engine_failure_envelope_maps_codes(monkeypatch, tmp_path):
    payload = _envelope(
        success=False,
        validation={"status": "INVALID", "checks": {}},
        errors=[{"code": "RequestValidationError", "message": "bad query", "details": {}}],
    )
    engine = _bridge(monkeypatch, tmp_path, payload)
    out = engine.execute("search", {})
    assert out.success is False
    assert out.validation is not None and out.validation.status == "FAILED"
    assert out.errors[0]["code"] == "request_validation_error"


def test_execute_artifact_files_become_pending(monkeypatch, tmp_path):
    victim = tmp_path / "board.kicad_pcb"
    victim.write_text("pcb")
    payload = _envelope(
        artifacts=[{"artifact_id": "x", "type": "pcb", "path": str(victim)}]
    )
    engine = _bridge(monkeypatch, tmp_path, payload)
    out = engine.execute("search", {})
    assert len(out.pending_artifacts) == 1
    dest, artifact_type = out.pending_artifacts[0]
    assert artifact_type == "pcb"
    assert Path(dest).name == "board.kicad_pcb"  # copied into bridge scratch
    assert Path(dest).is_file() and Path(dest).read_text() == "pcb"


def test_execute_missing_artifact_file_is_an_error(monkeypatch, tmp_path):
    payload = _envelope(
        artifacts=[{"artifact_id": "x", "type": "pcb", "path": str(tmp_path / "nope")}]
    )
    engine = _bridge(monkeypatch, tmp_path, payload)
    with pytest.raises(EngineExecutionError, match="missing artifact"):
        engine.execute("search", {})


def test_execute_timeout_is_an_error(monkeypatch):
    _patch_exe(monkeypatch)

    def _boom(*a, **k):
        raise subprocess.TimeoutExpired(cmd=a[0], timeout=1)

    monkeypatch.setattr("src.engines.native_bridge.subprocess.run", _boom)
    engine = NativeBridgeEngine("research", NATIVE_ENGINES["research"])
    with pytest.raises(EngineExecutionError, match="timed out"):
        engine.execute("search", {})


def test_execute_garbage_stdout_is_an_error(monkeypatch):
    _patch_exe(monkeypatch)
    monkeypatch.setattr(
        "src.engines.native_bridge.subprocess.run",
        lambda *a, **k: _Proc("not json{{", returncode=1, stderr="boom"),
    )
    engine = NativeBridgeEngine("research", NATIVE_ENGINES["research"])
    with pytest.raises(EngineExecutionError, match="unparseable"):
        engine.execute("search", {})


def test_execute_tolerates_native_stdout_noise(monkeypatch):
    """OpenCV logs to stdout before the envelope; the result must still parse.

    Canny is the first vision op to reach cv::parallel, and the first call
    in a process makes OpenCV print seven plugin_loader INFO lines on
    stdout. json.loads() over the whole stream then failed and the real
    envelope behind the noise was discarded.
    """
    noise = (
        "[ INFO:0@0.043] global registry_parallel.impl.hpp:96 "
        "cv::parallel::ParallelBackendRegistry core(parallel): Enabled backends(3)\n"
        "[ INFO:0@0.047] global plugin_loader.impl.hpp:67 cv::plugin::impl::"
        "DynamicLib::libraryLoad load opencv_core_parallel_tbb4140_64d.dll => FAILED\n"
    )
    _patch_exe(monkeypatch)
    monkeypatch.setattr(
        "src.engines.native_bridge.subprocess.run",
        lambda *a, **k: _Proc(noise + json.dumps(_envelope()) + "\n", returncode=0),
    )
    engine = NativeBridgeEngine("vision", NATIVE_ENGINES["vision"])
    assert engine.execute("image_edges", {"path": "x.png", "low": 50, "high": 150}).success


def test_execute_noise_without_an_envelope_is_still_an_error(monkeypatch):
    """Forgiving leading lines must not turn pure noise into a success."""
    _patch_exe(monkeypatch)
    monkeypatch.setattr(
        "src.engines.native_bridge.subprocess.run",
        lambda *a, **k: _Proc("[ INFO:0] something\nstill not json\n", returncode=0),
    )
    engine = NativeBridgeEngine("vision", NATIVE_ENGINES["vision"])
    with pytest.raises(EngineExecutionError, match="unparseable"):
        engine.execute("image_edges", {"path": "x.png"})


def test_execute_flows_through_jobs_and_artifacts(monkeypatch, tmp_path):
    """End-to-end through run_sync with a mocked binary: job tracked, file persisted."""
    victim = tmp_path / "result.json"
    victim.write_text('{"ok": true}')
    payload = _envelope(pending_artifacts=[{"path": str(victim), "type": "json"}])
    _patch_exe(monkeypatch)
    monkeypatch.setattr(
        "src.engines.native_bridge.subprocess.run",
        lambda *a, **k: _Proc(json.dumps(payload)),
    )
    import src.engines.registry as registry_mod

    with TestClient(app) as client:
        # lifespan re-bootstraps scaffolds on entry: patch after, inside.
        monkeypatch.setattr(
            registry_mod.registry,
            "_engines",
            {
                **registry_mod.registry._engines,
                "research": NativeBridgeEngine(
                    "research", NATIVE_ENGINES["research"]
                ),
            },
        )
        body = client.post(
            "/api/execute",
            json={"engine": "research", "operation": "search", "parameters": {"query": "q"}},
        ).json()
    assert body["success"] is True
    assert len(body["artifacts"]) == 1
    with TestClient(app) as client:
        job = client.get(f"/api/jobs/{body['job_id']}").json()
    assert job["status"] == "completed"
