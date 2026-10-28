"""Persistent native daemon client (M8): protocol, persistence, recovery.

Every test runs against a scripted fake child — no Trinity.exe is needed,
so this file is part of the default suite. Live behavior against the real
binary (PID reuse under HTTP, kill-recovery, the 50x benchmark) lives in
test_native_bridge_live.py, gated on TRINITY_NATIVE_EXEC.
"""

import io
import json
import os
import tempfile
import threading
from pathlib import Path
from typing import Any

import pytest

from src.core.errors import EngineExecutionError
from src.engines import native_bridge as nb
from src.engines.native_bridge import (
    DAEMON_ENV_VAR,
    DAEMON_TMP_PREFIX,
    EXEC_ENV_VAR,
    NativeDaemon,
    daemon_disabled,
)

PONG = json.dumps({"pong": True, "success": True})


def envelope(request_id: str, value: int = 1) -> str:
    return json.dumps(
        {
            "success": True,
            "engine": "research",
            "operation": "search",
            "request_id": request_id,
            "result": {"hit_count": value},
            "artifacts": [],
            "pending_artifacts": [],
            "validation": {"status": "VALIDATED", "checks": {}},
            "errors": [],
            "job_id": "",
        }
    )


def script(*lines: str) -> list[str]:
    return list(lines)


class FakeStdin:
    def __init__(self, proc: "FakeProc") -> None:
        self.proc = proc
        self.closed = False

    def write(self, data: bytes) -> int:
        self.proc.written.append(data)
        return len(data)

    def flush(self) -> None:
        pass

    def close(self) -> None:
        self.closed = True


class FakeStdout:
    """Streams a fixed script line by line, EOF when exhausted.

    A child is dead to poll() only when `dies_after_script` is set and its
    output is exhausted — a real daemon keeps running after answering N
    requests, so a script ending must not look like a crash except in the
    tests that say so. `hang` makes readline() block until close(), the
    signature of an engine op that never returns.
    """

    def __init__(self, script_bytes: bytes, hang: bool = False) -> None:
        self._buf = io.BytesIO(script_bytes)
        self._closed = False
        self._hang = hang
        self._release = threading.Event()
        self.eof_reached = False

    def readline(self) -> bytes:
        line = b"" if self._closed else self._buf.readline()
        if line or not self._hang or self._closed:
            if not line:
                self.eof_reached = True
            return line
        self._release.wait()  # hang: block like a stuck engine op
        if self._closed:
            return b""
        line = self._buf.readline()
        if not line:
            self.eof_reached = True
        return line

    def close(self) -> None:
        self._closed = True
        self._release.set()
        self._buf.close()

    def at_eof(self) -> bool:
        return self.eof_reached


class FakeProc:
    def __init__(
        self, pid: int, script_bytes: bytes, dies_after_script: bool, hang: bool = False
    ) -> None:
        self.pid = pid
        self.stdout = FakeStdout(script_bytes, hang=hang)
        self.stdin = FakeStdin(self)
        self.written: list[bytes] = []
        self.dies_after_script = dies_after_script
        self.returncode: int | None = None
        self.killed = False
        self.waited = False

    def poll(self) -> int | None:
        if self.killed:
            return 1
        if self.dies_after_script and self.stdout.at_eof():
            return 1
        return None

    def kill(self) -> None:
        self.killed = True
        self.returncode = 1

    def terminate(self) -> None:
        self.kill()

    def wait(self, timeout: float | None = None) -> int | None:
        self.waited = True
        return self.returncode


class Popener:
    """Scripted subprocess.Popen replacement: one (lines, dies, hang) per spawn."""

    def __init__(self, scripts: list[tuple[list[str], bool, bool]]) -> None:
        self._scripts = [
            (b"\n".join(line.encode() for line in lines) + b"\n", dies, hang)
            for lines, dies, hang in scripts
        ]
        self.spawned = 0
        self.procs: list[FakeProc] = []
        self.cmds: list[list[str]] = []
        self.envs: list[dict[str, str]] = []

    def __call__(
        self,
        cmd: list[str],
        *,
        stdin: int | None = None,
        stdout: int | None = None,
        stderr: object = None,
        shell: bool = False,
        env: dict[str, str] | None = None,
        **kwargs: object,
    ) -> FakeProc:
        assert shell is False
        script_bytes, dies, hang = self._scripts[
            self.spawned
        ]  # IndexError = unexpected spawn
        proc = FakeProc(1000 + self.spawned, script_bytes, dies, hang)
        self.cmds.append(list(cmd))
        self.envs.append(dict(env) if env else {})
        self.procs.append(proc)
        self.spawned += 1
        return proc


LIVE_CHILD = (script(PONG, envelope("r1"), envelope("r2", 2)), False, False)
ONE_SHOT = (script(PONG, envelope("r1")), False, False)
CRASHED_CHILD = (script(PONG, envelope("r1")), True, False)  # dies after its one op
BOOT_SILENT = (script(), False, False)


@pytest.fixture(autouse=True)
def _isolated_singleton(monkeypatch):
    """Run each test against a clean daemon singleton with daemon enabled."""
    monkeypatch.delenv(DAEMON_ENV_VAR, raising=False)
    saved = (nb._daemon, nb._daemon_boot_failed, nb._atexit_hook_installed)
    with nb._daemon_lock:
        nb._daemon = None
        nb._daemon_boot_failed = set()
    yield
    running = nb._daemon
    with nb._daemon_lock:
        nb._daemon = saved[0]
        nb._daemon_boot_failed = set(saved[1])
        nb._atexit_hook_installed = saved[2]
    if running is not None:
        running.kill()  # a test that booted a scripted child


@pytest.fixture()
def popener(monkeypatch):
    """Builds daemons whose children come from the given scripts."""

    def make(
        scripts: list[tuple[list[str], bool]], **daemon_kwargs: Any
    ) -> tuple[NativeDaemon, Popener]:
        pop = Popener(scripts)
        monkeypatch.setattr(nb.subprocess, "Popen", pop)
        # The default exe path is Windows-shaped ("C:/fake/Trinity.exe"),
        # which is a RELATIVE path on POSIX — bridge_child_env would resolve
        # it against the CWD and corrupt the joined PATH. Anything asserting
        # the injected PATH must pass an absolute-on-every-OS exe instead.
        daemon = NativeDaemon(daemon_kwargs.pop("exe", "C:/fake/Trinity.exe"), **daemon_kwargs)
        return daemon, pop

    return make


@pytest.fixture()
def boot(popener):
    """A daemon already booted (one spawn + ping handshake consumed)."""

    def make(
        scripts: list[tuple[list[str], bool]], **daemon_kwargs: Any
    ) -> tuple[NativeDaemon, Popener]:
        daemon, pop = popener(scripts, **daemon_kwargs)
        daemon._ensure_live()
        return daemon, pop

    return make


def test_boot_handshake_sends_ping_and_uses_daemon_flag(popener):
    daemon, pop = popener([ONE_SHOT])
    daemon._ensure_live()
    assert pop.cmds == [["C:/fake/Trinity.exe", "--daemon"]]
    assert json.loads(pop.procs[0].written[0]) == {"ping": True}


def test_written_lines_end_with_lf_never_crlf(boot):
    """CRLF would defeat the child's `line == "exit"` check: std::getline
    strips only '\n', so "exit\r\n" reaches it as "exit\\r" and is executed
    as an op name instead of shutting the child down."""
    daemon, pop = boot([LIVE_CHILD])
    daemon.execute({"request_id": "r1", "engine": "research", "operation": "search"})
    for line in pop.procs[0].written:
        assert line.endswith(b"\n")
        assert not line.endswith(b"\r\n")


def test_spawn_env_isolates_storage_and_injects_qt_path(monkeypatch, tmp_path, popener):
    qt = tmp_path / "qtbin"
    qt.mkdir()
    (qt / "Qt6Core.dll").write_bytes(b"")
    # The fake engine exe must sit under tmp_path: on POSIX a "C:/..."
    # path is relative and would resolve against the CWD.
    exe = tmp_path / "fake_bin"
    exe.mkdir()
    (exe / "Trinity.exe").write_bytes(b"")
    monkeypatch.setenv("TRINITY_QT_BIN", str(qt))
    monkeypatch.setenv("PATH", str(tmp_path / "elsewhere"))
    daemon, pop = popener([ONE_SHOT], exe=str(exe / "Trinity.exe"))
    daemon._ensure_live()
    env = pop.envs[0]
    assert Path(env["TRINITY_STORAGE_ROOT"]).name.startswith("trinity_native_daemon_")
    parts = env["PATH"].replace("\\", "/").split(os.pathsep)
    assert parts[0].replace("\\", "/") == str(qt).replace("\\", "/") or parts[0] == str(
        qt
    )
    # The exe directory carries the app-local vcpkg DLLs; original PATH is kept.
    assert parts[1].replace("\\", "/") == str(exe).replace("\\", "/"), env["PATH"]
    assert env["PATH"].endswith(str(tmp_path / "elsewhere"))


def test_pid_is_stable_across_sequential_calls(popener):
    daemon, pop = popener([LIVE_CHILD])
    daemon.execute({"request_id": "r1", "engine": "research", "operation": "search"})
    pid = daemon.pid()
    assert pid != 0
    second = daemon.execute(
        {"request_id": "r2", "engine": "research", "operation": "search"}
    )
    assert second["request_id"] == "r2"
    assert daemon.pid() == pid  # process reused, never respawned
    assert pop.spawned == 1


def test_no_blank_lines_are_ever_sent(boot):
    """The daemon skips blank lines WITHOUT a response, so sending one
    would desync request/response pairing forever."""
    daemon, pop = boot([ONE_SHOT])
    daemon.execute({"request_id": "r1", "engine": "research", "operation": "search"})
    payloads = pop.procs[0].written
    assert len(payloads) == 2  # boot ping + exactly one op, nothing else
    for line in payloads:
        assert line.strip() != b""


def test_recovery_after_child_death_spawns_a_fresh_process(popener):
    first, pop = popener([CRASHED_CHILD, (script(PONG, envelope("r2")), False, False)])
    out = first.execute(
        {"request_id": "r1", "engine": "research", "operation": "search"}
    )
    assert out["request_id"] == "r1"
    first_pid = first.pid()

    out2 = first.execute(
        {"request_id": "r2", "engine": "research", "operation": "search"}
    )
    assert out2["request_id"] == "r2"
    assert pop.spawned == 2
    assert first.pid() != first_pid  # a new child took over


def test_mid_call_death_fails_that_call_and_not_the_next(popener):
    # Child 1 answers the boot ping then EOFs with no envelope: the client's
    # request arrives at an already-dead transport → fail fast.
    daemon, pop = popener(
        [(script(PONG), True, False), (script(PONG, envelope("r2")), False, False)]
    )
    with pytest.raises(EngineExecutionError, match="exited unexpectedly"):
        daemon.execute(
            {"request_id": "r1", "engine": "research", "operation": "search"}
        )
    out = daemon.execute(
        {"request_id": "r2", "engine": "research", "operation": "search"}
    )
    assert out["request_id"] == "r2"
    assert pop.spawned == 2


def test_timeout_kills_the_child(popener):
    # hang=True: stdout never delivers the envelope (a hung engine op).
    daemon, pop = popener([(script(PONG), False, True)])
    daemon._ensure_live()
    with pytest.raises(EngineExecutionError, match="timed out"):
        daemon.execute(
            {"request_id": "r1", "engine": "research", "operation": "search"},
            timeout_s=0.2,
        )
    assert pop.procs[0].killed is True  # a hung engine can only be stopped with it


def test_native_noise_before_envelope_is_tolerated(popener):
    noise = "[ INFO:0@0.043] cv::parallel::ParallelBackendRegistry core(parallel)"
    daemon, _ = popener([(script(PONG, noise, noise, envelope("r1")), False, False)])
    out = daemon.execute(
        {"request_id": "r1", "engine": "research", "operation": "search"}
    )
    assert out["request_id"] == "r1"


def test_noise_over_cap_is_unparseable_and_resets_transport(popener):
    noise = "[ INFO:0] chatter"
    daemon, _ = popener(
        [
            (script(PONG, *([noise] * 5), envelope("r1")), False, False),
            (script(PONG, envelope("r2")), False, False),
        ],
        noise_cap=4,
    )
    with pytest.raises(EngineExecutionError, match="unparseable"):
        daemon.execute(
            {"request_id": "r1", "engine": "research", "operation": "search"}
        )
    # Transport was reset: stale queued lines cannot poison the next call.
    out2 = daemon.execute(
        {"request_id": "r2", "engine": "research", "operation": "search"}
    )
    assert out2["request_id"] == "r2"


def test_shutdown_sends_exit_line_and_cleans_workspace(popener):
    daemon, pop = popener([ONE_SHOT])
    daemon._ensure_live()
    workspace = daemon.workspace()
    assert workspace
    daemon.shutdown()
    proc = pop.procs[0]
    assert proc.written[-1] == b"exit\n"
    assert proc.waited is True
    assert daemon.pid() == 0
    daemon.shutdown()  # idempotent


def test_daemon_disabled_env_is_respected(monkeypatch):
    for value in ("0", "off", "false", "no"):
        monkeypatch.setenv(DAEMON_ENV_VAR, value)
        assert daemon_disabled() is True
    monkeypatch.setenv(DAEMON_ENV_VAR, "1")
    assert daemon_disabled() is False
    monkeypatch.delenv(DAEMON_ENV_VAR)
    assert daemon_disabled() is False


def test_get_daemon_returns_none_and_remembers_boot_failure(monkeypatch):
    pop = Popener([BOOT_SILENT])  # no output at all: boot ping never answered
    monkeypatch.setattr(nb.subprocess, "Popen", pop)
    assert nb.get_daemon("C:/fake/Trinity.exe") is None
    assert nb.current_daemon() is None
    assert nb.get_daemon("C:/fake/Trinity.exe") is None  # remembered, not retried
    assert pop.spawned == 1


def test_get_daemon_caches_one_instance_per_exe(monkeypatch):
    pop = Popener([LIVE_CHILD])
    monkeypatch.setattr(nb.subprocess, "Popen", pop)
    daemon1 = nb.get_daemon("C:/fake/Trinity.exe")
    daemon2 = nb.get_daemon("C:/fake/Trinity.exe")
    assert daemon1 is daemon2
    out = daemon1.execute(
        {"request_id": "r1", "engine": "research", "operation": "search"}
    )
    assert out["request_id"] == "r1"


def test_artifact_copy_out_unlinks_inside_daemon_workspace(monkeypatch, tmp_path):
    """A persistent child must not accumulate scratch: once a reported file
    is copied into per-call bridge scratch, the daemon's own copy is
    deleted — but only for paths inside the daemon's workspace."""
    engine_exe = tmp_path / "Trinity.exe"
    engine_exe.write_bytes(b"")
    monkeypatch.setenv(EXEC_ENV_VAR, str(engine_exe))

    fake_home = tmp_path / "daemon_home"
    real_mkdtemp = nb.tempfile.mkdtemp
    calls = {"n": 0}

    def fake_mkdtemp(**kwargs: Any) -> str:
        if kwargs.get("prefix") == DAEMON_TMP_PREFIX:
            calls["n"] += 1
            fake_home.mkdir(parents=True, exist_ok=True)
            return str(fake_home)
        return real_mkdtemp(**kwargs)

    monkeypatch.setattr(nb.tempfile, "mkdtemp", fake_mkdtemp)

    payload = json.loads(envelope("r1"))
    native_file = fake_home / "board.kicad_pcb"
    native_file.parent.mkdir(parents=True, exist_ok=True)
    native_file.write_text("kicad")
    payload["pending_artifacts"] = [{"path": str(native_file), "type": "pcb"}]
    pop = Popener([(script(PONG, json.dumps(payload)), False, False)])
    monkeypatch.setattr(nb.subprocess, "Popen", pop)

    from src.engines.native_bridge import NativeBridgeEngine

    engine = NativeBridgeEngine("research", ["search"])
    out = engine.execute("search", {"query": "q"})
    dest, artifact_type = out.pending_artifacts[0]
    assert artifact_type == "pcb"
    assert Path(dest).name == "board.kicad_pcb" and Path(dest).read_text() == "kicad"
    assert not native_file.exists()  # daemon's own copy deleted post copy-out
    assert calls["n"] == 1
    assert (
        tempfile.mkdtemp is not fake_mkdtemp or True
    )  # no-op guard; patch undone by monkeypatch
