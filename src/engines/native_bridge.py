"""Native engine bridge (Item 1, Stage 4b; M8 persistent daemon).

Routes the six scaffolded engines (pcb, firmware, vision, simulation,
research, robotics) to the native Trinity.exe engine. M8 replaces the
M4 single-shot transport (`subprocess.run([exe, "--exec"])`, ~440 ms/op
because every request re-bootstraps Windows DLLs and Qt) with one
long-lived `Trinity.exe --daemon` child: one JSON request line in, one
JSON envelope line out, with a single EngineRegistry hot in memory for
the child's lifetime (~2 ms/op).

Design:
- D1: this is a BaseEngine subclass, so job_manager.run_sync() works
  unchanged — job rows, pending_artifacts persistence, validation
  storage, no cache pollution (caching is math/cad-only).
- D2: describe() reports status="bridged" (neither live-local nor stub).
- D3: unmapped ops raise CapabilityUnavailableError, exactly as the
  scaffold does today. No faked coverage.
- D4 (M8): every bridged op is fully parameterized, so a hot registry
  adds no cross-call state; the child's storage root is a private temp
  dir whose artifacts are copied out per call.

Fallback: the daemon is the default transport. TRINITY_NATIVE_DAEMON=0
(or a child that fails its boot ping handshake) keeps the single-shot
"--exec" path, so an older binary still works without recompiling.

Activation: TRINITY_NATIVE_EXEC must point at an existing Trinity.exe.
Otherwise registry.py keeps the ScaffoldEngine registrations.
"""

from __future__ import annotations

import atexit
import json
import logging
import os
import queue
import shutil
import subprocess
import tempfile
import threading
import time
import uuid
from pathlib import Path
from typing import Any

from src.core.errors import (
    CapabilityUnavailableError,
    EngineExecutionError,
    RequestValidationError,
)
from src.engines.base import BaseEngine, EngineResult, ValidationResult

EXEC_ENV_VAR = "TRINITY_NATIVE_EXEC"
EXEC_TIMEOUT_S = 120
EXEC_MAX_STDOUT = 50 * 1024 * 1024
STDERR_TAIL = 2000
LOGGER = logging.getLogger(__name__)

# ---- M8 persistent daemon ----
# Set to 0/off to force the legacy single-shot "--exec" transport.
DAEMON_ENV_VAR = "TRINITY_NATIVE_DAEMON"
DAEMON_OFF_VALUES = {"0", "off", "false", "no"}
# Cold boot (DLL + Qt bootstrap) gets a generous budget; per-request
# ops reuse EXEC_TIMEOUT_S for parity with the single-shot transport.
DAEMON_PING_TIMEOUT_S = 30.0
# OpenCV chatters a handful of plugin_loader INFO lines onto stdout
# before an envelope. Anything beyond this many consecutive non-envelope
# lines means the child is not speaking the protocol anymore.
DAEMON_NOISE_LINE_CAP = 200
DAEMON_TMP_PREFIX = "trinity_native_daemon_"
# Sentinel the reader thread enqueues when the child's stdout reaches EOF.
_DAEMON_EOF = object()

# Engines the bridge can serve, with the same capabilities the scaffolds
# advertise so describe() shape is unchanged apart from status.
NATIVE_ENGINES: dict[str, list[str]] = {
    # No "inspect": PcbEngine has no such operation. It used to be listed
    # here anyway, which made the capability list promise a call that could
    # only ever answer 501. Same trap as firmware's test/compile below.
    "pcb": ["validate", "generate", "export", "add_net"],
    # The firmware chain the native engine actually implements. `create` is
    # the entry point; the rest are the steps build needs before it can run.
    # The old ["create", "build", "test", "compile"] list advertised build
    # while leaving it unreachable, because build requires an MCU and
    # generated sources that no bridged op could supply.
    "firmware": [
        "create",
        "select_mcu",
        "configure_pin",
        "configure_peripheral",
        "generate",
        "validate",
        "build",
    ],
    # Exactly VisionEngine's C++ capability list. ocr, document_parse and
    # geometry_extract used to be advertised here and do not exist in the
    # engine at all -- there was nothing to map them to.
    "vision": [
        "image_inspect",
        "image_resize",
        "image_grayscale",
        "image_edges",
        "image_stats",
    ],
    "research": ["search"],
    "simulation": ["simulate"],
    "robotics": ["kinematics", "trajectory_plan"],
}

# Python op -> native op. Scaffolds never had parameter contracts, so each
# mapped op defines a fresh one (mirroring --selftest shapes).
#
# Every op in NATIVE_ENGINES must appear here; the reverse also holds, and a
# test pins both directions. An advertised op with no mapping answers 501
# while the capability list claims it works, which is how pcb/inspect and
# three vision ops sat there for so long. Unmapped ops are simply not
# advertised, so a caller can trust what it is told.
OP_MAP: dict[str, dict[str, str]] = {
    "research": {"search": "search"},
    "robotics": {
        "kinematics": "forward_kinematics",
        "trajectory_plan": "plan_trajectory",
    },
    "simulation": {"simulate": "simulate_linear_motion"},
    # Single-shot by design: each step takes the project returned by the
    # previous one and returns it upgraded, so no cross-call state is kept.
    "firmware": {
        "create": "create_project",
        "select_mcu": "select_mcu",
        "configure_pin": "configure_pin",
        "configure_peripheral": "configure_peripheral",
        "generate": "generate_firmware",
        "validate": "validate_project",
        "build": "build",
    },
    "pcb": {
        "validate": "validate_design",
        "generate": "create_board",
        "export": "export",
        # Nets are the only way a board's power pins become DRC-clean, and
        # without this mapping no board with components could ever pass
        # validation -- so export was unreachable for every real design.
        "add_net": "add_net",
    },
    # The native vision engine implements four more image operations than were
    # reachable. They were written, tested C++ sitting behind a 501.
    "vision": {
        "image_inspect": "load_image",
        "image_resize": "resize_image",
        "image_grayscale": "grayscale",
        "image_edges": "edge_detect",
        "image_stats": "image_statistics",
    },
}

# Native ErrorCode names (Error.hpp) -> Python codes (errors.py). NOT
# mechanical: EngineNotFoundError -> "engine_not_found" (no _error suffix),
# so all eight are spelled out.
ERROR_CODE_MAP: dict[str, str] = {
    "TrinityError": "trinity_error",
    "RequestValidationError": "request_validation_error",
    "EngineNotFoundError": "engine_not_found",
    "EngineExecutionError": "engine_execution_error",
    "GeometryValidationError": "geometry_validation_error",
    "JobNotFoundError": "job_not_found",
    "ArtifactNotFoundError": "artifact_not_found",
    "CapabilityUnavailableError": "capability_unavailable",
}

# Native validation statuses -> Python ValidationLevel. Python has no
# INVALID (schemas.py), so it becomes FAILED.
STATUS_MAP: dict[str, str] = {
    "GENERATED": "GENERATED",
    "VALIDATED": "VALIDATED",
    "VERIFIED": "VERIFIED",
    "INVALID": "FAILED",
    "FAILED": "FAILED",
}


def native_exe_path() -> str | None:
    """Resolved Trinity.exe path, or None when the bridge is unavailable."""
    exe = os.getenv(EXEC_ENV_VAR, "")
    if exe and Path(exe).is_file():
        return exe
    return None


def daemon_disabled() -> bool:
    """True when TRINITY_NATIVE_DAEMON forces the single-shot transport."""
    return os.getenv(DAEMON_ENV_VAR, "").strip().lower() in DAEMON_OFF_VALUES


def _parse_json_line(line: str) -> Any | None:
    """Parse one stdout line, or None when it is not valid JSON."""
    try:
        return json.loads(line)
    except json.JSONDecodeError:
        return None


def _read_stderr_tail(stderr_path: str) -> str:
    """Last STDERR_TAIL chars of the child's stderr log (best effort)."""
    try:
        with open(stderr_path, encoding="utf-8", errors="replace") as fh:
            return fh.read()[-STDERR_TAIL:]
    except OSError:
        return ""


def _find_qt_bin_dirs() -> list[str]:
    """Directories to prepend to the child's PATH so it can load Qt6.

    Trinity.exe links Qt6Core but ships no Qt DLLs beside itself, and a
    vcpkg build deploys only its OpenCV DLLs app-locally. Ports the
    discovery logic from desktop.py find_qt(): an explicit
    TRINITY_QT_BIN override wins; then whatever is already on PATH; then
    the standard C:/Qt layout.
    """
    override = os.getenv("TRINITY_QT_BIN", "")
    if override and (Path(override) / "Qt6Core.dll").is_file():
        return [override]
    for entry in os.getenv("Path", os.getenv("PATH", "")).split(os.pathsep):
        if entry and (Path(entry) / "Qt6Core.dll").is_file():
            return []  # resolvable already; nothing to inject
    root = Path("C:/Qt")
    if root.is_dir():
        for version in sorted(root.iterdir()):
            if not version.is_dir():
                continue
            for kit in sorted(version.iterdir()):
                candidate = kit / "bin"
                if (candidate / "Qt6Core.dll").is_file():
                    return [str(candidate)]
    return []


def bridge_child_env(
    exe: str | None = None, storage_root: str | None = None
) -> dict[str, str]:
    """Environment for ANY native child spawn (daemon or single-shot).

    Prepends the directories that make `Trinity.exe` able to load its
    runtime libraries (Qt6 from TRINITY_QT_BIN / an existing PATH entry /
    the C:/Qt layout, plus the exe's own app-local vcpkg DLL directory)
    and optionally isolates its storage root.
    """
    env = {**os.environ}
    if storage_root:
        env["TRINITY_STORAGE_ROOT"] = storage_root
    prepend = _find_qt_bin_dirs()
    if exe is not None:
        exe_dir = str(Path(exe).resolve().parent)
        if exe_dir not in prepend:
            prepend.append(exe_dir)
    if prepend:
        path_key = "PATH" if "PATH" in env else "Path"
        env[path_key] = os.pathsep.join([*prepend, env.get(path_key, "")])
    return env


class NativeDaemon:
    """One long-lived `Trinity.exe --daemon` child process (M8).

    The child keeps a single EngineRegistry hot in memory across every
    request, so an op costs one pipe round-trip instead of a full
    Windows DLL/Qt bootstrap per call.

    Wire protocol (app/native/src/cli/Exec.cpp, runDaemon):
    - one JSON request line -> one JSON envelope line, flushed per call
    - {"ping": true} -> {"pong": true, "success": true}
    - "exit"/"quit" ends the child; blank lines are skipped WITHOUT a
      response, so the client must never send one
    - invalid JSON yields an error envelope and the daemon stays alive
    - stdout may carry native noise before an envelope (OpenCV plugin
      loader lines) the same way --exec does

    Concurrency: every execute() holds one lock across write->read, so
    at most one op is in flight and responses pair 1:1 with requests in
    order. Engines run in the child process, so nothing is lost by not
    multiplexing writes.

    Recovery: a child that dies is detected at the next call and lazily
    respawned; the call that observed the death fails with an
    EngineExecutionError rather than retrying. A request that misses its
    deadline kills the child (a hung engine cannot be interrupted any
    other way) and raises, with the same recovery semantics.

    Errors: the child's stderr goes to a log file in its private temp
    workspace; failures surface the log tail in error details exactly
    like the single-shot transport does.
    """

    def __init__(self, exe: str, noise_cap: int = DAEMON_NOISE_LINE_CAP) -> None:
        self._exe = exe
        self._lock = threading.Lock()
        self._proc: subprocess.Popen[bytes] | None = None
        self._workspace = ""
        self._stderr_path = ""
        self._stderr_fh = None
        self._lines: queue.Queue = queue.Queue()
        self._pid = 0
        self._noise_cap = noise_cap

    # -- introspection (safe without holding the lock) -----------------

    def pid(self) -> int:
        """Child PID, 0 before first boot. Reused-pid persistence test hook."""
        return self._pid

    def workspace(self) -> str:
        """The daemon's private temp dir (its TRINITY_STORAGE_ROOT)."""
        return self._workspace

    # -- public requests ------------------------------------------------

    def execute(
        self, request: dict[str, Any], timeout_s: float = EXEC_TIMEOUT_S
    ) -> dict[str, Any]:
        """Send one request line, return its envelope dict.

        Raises EngineExecutionError on transport failure (child death,
        pipe error, unparseable output). The failed call is never
        retried; the NEXT call respawns the child transparently.
        """
        with self._lock:
            self._ensure_live()
            self._send(request)
            return self._wait_for(request, timeout_s)

    def shutdown(self) -> None:
        """Tell the child to exit, then reap. Safe to call repeatedly."""
        with self._lock:
            self._shutdown_locked()

    # -- internals (all assume the lock unless noted) --------------------

    def _shutdown_locked(self) -> None:
        proc = self._proc
        if proc is not None and proc.poll() is None:
            try:
                proc.stdin.write(b"exit\n")
                proc.stdin.flush()
            except (OSError, ValueError):
                pass
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        self._reap()

    def _reap(self) -> None:
        """Tear down the child, its pipes, stderr log and workspace."""
        proc, self._proc = self._proc, None
        stdout = proc.stdout if proc is not None else None
        stdin = proc.stdin if proc is not None else None
        try:
            if stdin is not None:
                stdin.close()
            if stdout is not None:
                stdout.close()
            if proc is not None:
                if proc.poll() is None:
                    proc.kill()
                proc.wait(timeout=10)
        except (OSError, ValueError):
            pass
        finally:
            fh, self._stderr_fh = self._stderr_fh, None
            if fh is not None:
                try:
                    fh.close()
                except OSError:
                    pass
            workspace, self._workspace = self._workspace, ""
            self._stderr_path, self._pid = "", 0
            if workspace:
                shutil.rmtree(workspace, ignore_errors=True)

    def kill(self) -> None:
        """Force-kill the child now. Recovery happens at the next call."""
        with self._lock:
            self._reap()

    def _ensure_live(self) -> None:
        """Boot or re-boot the child, then verify it with a ping."""
        if self._proc is not None and self._proc.poll() is None:
            return
        if self._proc is not None:
            self._reap()  # release the previous (dead) child's temp dir
        self._spawn()
        self._send({"ping": True})
        deadline = time.monotonic() + DAEMON_PING_TIMEOUT_S
        junk = 0
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                details = {"exe": self._exe}
                self._reap()
                raise EngineExecutionError(
                    "Native daemon did not answer its boot ping in "
                    f"{DAEMON_PING_TIMEOUT_S:.0f}s",
                    details=details,
                )
            try:
                line = self._lines.get(timeout=min(remaining, 1.0))
            except queue.Empty:
                continue
            if line is _DAEMON_EOF:
                details = self._tail_details()
                self._reap()
                raise EngineExecutionError(
                    "Native daemon exited during boot", details=details
                )
            obj = _parse_json_line(line)
            if isinstance(obj, dict) and obj.get("pong") is True:
                LOGGER.debug(
                    "native daemon up (pid %d, workspace %s)",
                    self._pid,
                    self._workspace,
                )
                return
            junk += 1  # boot chatter that parses but is not pong
            if junk >= self._noise_cap:
                details = self._tail_details()
                self._reap()
                raise EngineExecutionError(
                    "Native daemon produced unparseable boot output", details=details
                )

    def _spawn(self) -> None:
        """Create the child, its workspace and its reader thread."""
        workspace = tempfile.mkdtemp(prefix=DAEMON_TMP_PREFIX)
        stderr_path = os.path.join(workspace, "daemon_stderr.log")
        try:
            stderr_fh = open(stderr_path, "wb")
        except OSError:
            shutil.rmtree(workspace, ignore_errors=True)
            raise
        try:
            proc = subprocess.Popen(
                [self._exe, "--daemon"],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=stderr_fh,
                shell=False,
                env=self._child_env(workspace),
            )
        except OSError as exc:
            stderr_fh.close()
            shutil.rmtree(workspace, ignore_errors=True)
            raise EngineExecutionError(
                f"Native backend could not be launched: {exc}",
                details={"exe": self._exe},
            ) from exc
        self._proc = proc
        self._pid = proc.pid or self._pid  # FakeProc in tests may report 0
        self._workspace = workspace
        self._stderr_path = stderr_path
        self._stderr_fh = stderr_fh
        self._lines = queue.Queue()
        threading.Thread(
            target=self._reader,
            args=(proc, self._lines),
            daemon=True,
            name=f"trinity-native-daemon-{proc.pid}",
        ).start()

    @staticmethod
    def _reader(proc: subprocess.Popen[bytes], out_lines: queue.Queue) -> None:
        """Drain the child's stdout into the response queue until EOF."""
        stdout = proc.stdout
        try:
            if stdout is not None:
                for raw in iter(stdout.readline, b""):
                    out_lines.put(raw.decode("utf-8", errors="replace").rstrip("\r\n"))
        except (OSError, ValueError):
            pass
        finally:
            out_lines.put(_DAEMON_EOF)

    def _child_env(self, workspace: str) -> dict[str, str]:
        """Child environment: isolated storage + runtime-library PATH."""
        return bridge_child_env(self._exe, workspace)

    def _send(self, request: dict[str, Any]) -> None:
        """Write one raw \\n-terminated JSON line (never CRLF!).

        The daemon's C++ getline strips only '\n'; a text-mode pipe would
        inject "\r" and turn the shutdown line "exit" into "exit\\r",
        which it would execute as an op instead of exiting. Binary
        pipes keep every byte under our control.
        """
        proc = self._proc
        if proc is None or proc.stdin is None:
            raise EngineExecutionError(
                "Native daemon is not running", details={"exe": self._exe}
            )
        try:
            proc.stdin.write((json.dumps(request) + "\n").encode("utf-8"))
            proc.stdin.flush()
        except (OSError, ValueError) as exc:
            # Dead child; reset the transport so the next call respawns.
            details = {
                "exe": self._exe,
                "reason": str(exc),
                "stderr_tail": _read_stderr_tail(self._stderr_path),
            }
            self._reap()
            raise EngineExecutionError(
                "Native daemon pipe write failed", details=details
            ) from exc

    def _tail_details(self) -> dict[str, Any]:
        return {"exe": self._exe, "stderr_tail": _read_stderr_tail(self._stderr_path)}

    def _wait_for(self, request: dict[str, Any], timeout_s: float) -> dict[str, Any]:
        """Read queued lines until an envelope-shaped object arrives."""
        deadline = time.monotonic() + timeout_s
        junk = 0
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                # A hung engine can only be stopped by stopping the child
                # that runs it; the next call respawns transparently.
                self._reap()
                raise EngineExecutionError(
                    f"Native backend timed out after {timeout_s}s",
                    details={"exe": self._exe},
                )
            try:
                line = self._lines.get(timeout=min(remaining, 1.0))
            except queue.Empty:
                continue
            if line is _DAEMON_EOF:
                details = self._tail_details()
                self._reap()  # invariant: a failed call resets the transport
                raise EngineExecutionError(
                    "Native backend exited unexpectedly", details=details
                )
            obj = _parse_json_line(line)
            if obj is None or not isinstance(obj, dict) or "success" not in obj:
                junk += 1  # OpenCV chatter etc.
                if junk >= self._noise_cap:
                    # Giving up leaves the queue desynced from requests, so
                    # the transport resets here too; the next call respawns.
                    details = self._tail_details()
                    self._reap()
                    raise EngineExecutionError(
                        "Native backend returned unparseable output", details=details
                    )
                continue
            return obj


# ---- daemon singleton (one child per process, per exe) ----------------

_daemon: NativeDaemon | None = None
_daemon_lock = threading.Lock()
# Exes whose boot handshake failed once — remembered so every later call
# falls straight back to single-shot instead of re-paying the failure.
_daemon_boot_failed: set[str] = set()
_atexit_hook_installed = False


def _shutdown_daemon() -> None:
    """atexit hook: shut the shared daemon down if one is running."""
    global _daemon
    with _daemon_lock:
        daemon, _daemon = _daemon, None
    if daemon is not None:
        try:
            daemon.shutdown()
        except Exception:  # noqa: BLE001 — an exit hook must never raise
            pass


def get_daemon(exe: str) -> NativeDaemon | None:
    """The shared daemon for `exe`, booting it on first use.

    Returns None — without raising — when the daemon is forced off by
    env or its boot handshake failed, so the caller falls back to the
    single-shot transport. A handshake failure is remembered for the
    process lifetime: a binary that cannot speak the daemon protocol
    will not get better by retrying.
    """
    global _daemon, _atexit_hook_installed
    if daemon_disabled() or exe in _daemon_boot_failed:
        return None
    with _daemon_lock:
        if _daemon is not None and _daemon._exe == exe:
            return _daemon
        if _daemon is not None:
            _daemon.shutdown()
        daemon = NativeDaemon(exe)
        try:
            daemon._ensure_live()
        except EngineExecutionError:
            _daemon_boot_failed.add(exe)
            return None
        if not _atexit_hook_installed:
            _atexit_hook_installed = True
            atexit.register(_shutdown_daemon)
        _daemon = daemon
        return daemon


def shutdown_daemon() -> None:
    """Explicit shared-daemon shutdown (also wired as the atexit hook)."""
    _shutdown_daemon()


def current_daemon() -> NativeDaemon | None:
    """The running shared daemon, or None (test/introspection hook)."""
    return _daemon


def _map_error(entry: dict[str, Any]) -> dict[str, Any]:
    raw_code = str(entry.get("code", ""))
    code = ERROR_CODE_MAP.get(raw_code, "engine_execution_error")
    details = entry.get("details")
    details = dict(details) if isinstance(details, dict) else {}
    if raw_code not in ERROR_CODE_MAP:
        details["native_code"] = raw_code
    return {
        "code": code,
        "message": str(entry.get("message", "native engine failed")),
        "details": details,
    }


def _map_validation(envelope: dict[str, Any]) -> ValidationResult | None:
    raw = envelope.get("validation")
    if not isinstance(raw, dict):
        return None
    status = STATUS_MAP.get(str(raw.get("status", "INVALID")), "FAILED")
    checks = raw.get("checks")
    return ValidationResult(
        status=status, checks=dict(checks) if isinstance(checks, dict) else {}
    )


class NativeBridgeEngine(BaseEngine):
    """Executes one operation via Trinity.exe --exec (stdin JSON -> stdout JSON)."""

    def __init__(self, name: str, capabilities: list[str]) -> None:
        self.name, self.capabilities = name, capabilities
        self.version = "0.1"

    def describe(self) -> dict[str, Any]:
        # "transport" is what this engine WILL use: daemon by default,
        # oneshot when forced off by env. It does not boot the child —
        # an introspection call must stay free of side effects. An
        # in-flight handshake fallback (rare) flips the effective value
        # for calls only, never advertised here.
        transport = "oneshot" if daemon_disabled() else "daemon"
        return {**super().describe(), "status": "bridged", "transport": transport}

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        native_op = OP_MAP.get(self.name, {}).get(operation)
        if native_op is None:
            raise CapabilityUnavailableError(
                f"The {self.name} engine has no bridged implementation for "
                f"'{operation}' yet",
                details={
                    "engine": self.name,
                    "operation": operation,
                    "status": "bridged",
                },
            )
        exe = native_exe_path()
        if exe is None:
            raise EngineExecutionError(
                "Native backend is not available",
                details={"env_var": EXEC_ENV_VAR, "engine": self.name},
            )
        request = {
            "request_id": uuid.uuid4().hex,
            "engine": self.name,
            "operation": native_op,
            "parameters": self._resolve_inputs(operation, parameters),
        }
        daemon = get_daemon(exe)
        if daemon is not None:
            # Isolated storage root lives for the daemon's lifetime; the
            # child must never touch Python's SQLite files (schema-mismatch
            # risk). Its DB/logs die with that temp dir at shutdown.
            envelope = daemon.execute(request)
            pending = self._collect_artifacts(envelope, operation, daemon.workspace())
        else:
            # Single-shot fallback (legacy / forced): every op fully
            # parameterized, so successive --exec processes share nothing.
            native_tmp = tempfile.mkdtemp(prefix="trinity_native_")
            try:
                proc = self._invoke(exe, request, native_tmp, operation)
                envelope = self._parse_envelope(proc, operation)
                pending = self._collect_artifacts(envelope, operation, native_tmp)
            finally:
                shutil.rmtree(native_tmp, ignore_errors=True)
        raw_errors = envelope.get("errors")
        errors = (
            [_map_error(e) for e in raw_errors if isinstance(e, dict)]
            if isinstance(raw_errors, list)
            else []
        )
        return EngineResult(
            success=bool(envelope.get("success", False)),
            engine=self.name,
            operation=operation,
            result=(
                dict(envelope["result"])
                if isinstance(envelope.get("result"), dict)
                else {}
            ),
            pending_artifacts=pending,
            validation=_map_validation(envelope),
            errors=errors,
        )

    def _resolve_inputs(
        self, operation: str, parameters: dict[str, Any]
    ) -> dict[str, Any]:
        """Turn API-level handles into the concrete values native expects.

        The native vision engine deliberately refuses an artifact_id on its
        own — it has no database to resolve one against. Resolving here keeps
        that refusal intact for anyone calling the binary directly, while
        letting the HTTP surface speak in artifact ids.
        """
        artifact_id = parameters.get("artifact_id")
        if not isinstance(artifact_id, str) or parameters.get("path"):
            return parameters

        from src.artifacts.manager import artifact_manager  # noqa: PLC0415
        from src.core.errors import ArtifactNotFoundError  # noqa: PLC0415

        try:
            ref = artifact_manager.get(artifact_id)
        except ArtifactNotFoundError as exc:
            raise RequestValidationError(
                f"No artifact '{artifact_id}' for {self.name}/{operation}",
                details={"artifact_id": artifact_id, "engine": self.name},
            ) from exc

        resolved = Path(ref.path)
        if not resolved.is_file():
            raise RequestValidationError(
                f"Artifact '{artifact_id}' has no file on disk",
                details={"artifact_id": artifact_id, "engine": self.name},
            )
        return {**parameters, "path": str(resolved)}

    def _invoke(
        self, exe: str, request: dict[str, Any], native_tmp: str, operation: str
    ):
        """Run the child with an isolated storage root. No shell, timeout, cap."""
        env = bridge_child_env(exe, native_tmp)
        try:
            proc = subprocess.run(
                [exe, "--exec"],
                input=json.dumps(request),
                capture_output=True,
                text=True,
                # Native stdout is UTF-8 JSON. Without this, text=True
                # decodes with the Windows locale codec (cp1252), which
                # raises UnicodeDecodeError on the first non-ASCII byte —
                # firmware source generation emits plenty — and the whole
                # envelope is lost. "replace" keeps a bad byte from taking
                # down the call; it can only affect one string value.
                encoding="utf-8",
                errors="replace",
                timeout=EXEC_TIMEOUT_S,
                shell=False,
                env=env,
            )
        except subprocess.TimeoutExpired as exc:
            raise EngineExecutionError(
                f"Native backend timed out after {EXEC_TIMEOUT_S}s",
                details={"engine": self.name, "operation": operation},
            ) from exc
        except OSError as exc:
            raise EngineExecutionError(
                f"Native backend could not be launched: {exc}",
                details={"engine": self.name, "exe": exe},
            ) from exc
        if len(proc.stdout) > EXEC_MAX_STDOUT:
            raise EngineExecutionError(
                "Native backend output exceeded size cap",
                details={"engine": self.name, "bytes": len(proc.stdout)},
            )
        return proc

    def _parse_envelope(self, proc, operation: str) -> dict[str, Any]:
        # stdout is supposed to be exactly one JSON envelope, but native C++
        # dependencies log to it too. OpenCV prints seven
        # "ParallelBackendRegistry / plugin_loader" INFO lines the first time
        # an op touches cv::parallel -- which is edge_detect, since Canny is
        # the first op to reach the parallel backend. json.loads() on the
        # whole stream then fails and the real, valid envelope behind the
        # noise is thrown away.
        #
        # The envelope is always the last line. Scan backwards for it, and
        # only forgive the noise if a parseable object actually turns up --
        # an engine that printed garbage and no result is still an error,
        # not a pass.
        lines = [ln for ln in proc.stdout.splitlines() if ln.strip()]
        envelope = None
        noise = 0
        for line in reversed(lines):
            try:
                envelope = json.loads(line)
                break
            except json.JSONDecodeError:
                noise += 1
                continue

        if envelope is None:
            # Nothing parsed at any line. Report the first line as the cause,
            # since that is what the engine actually emitted first.
            first = lines[0][:STDERR_TAIL] if lines else ""
            raise EngineExecutionError(
                "Native backend returned unparseable output",
                details={
                    "engine": self.name,
                    "operation": operation,
                    "exit_code": proc.returncode,
                    "stdout_head": first,
                    "stderr_tail": proc.stderr[-STDERR_TAIL:],
                },
            )
        if noise:
            LOGGER.debug(
                "%s/%s: skipped %d line(s) of native stdout noise before the envelope",
                self.name,
                operation,
                noise,
            )
        if not isinstance(envelope, dict):
            raise EngineExecutionError(
                "Native backend returned a non-object envelope",
                details={"engine": self.name, "operation": operation},
            )
        return envelope

    def _collect_artifacts(
        self, envelope: dict[str, Any], operation: str, native_root: str | None = None
    ) -> list[tuple[str, str]]:
        """Copy native files into one bridge scratch dir.

        run_sync() persists pending_artifacts via the artifact manager and
        then removes the first entry's parent dir (manager.py), so a single
        dir here means zero litter with no extra cleanup step.

        When `native_root` is the daemon's private workspace, each copied
        file is also deleted at its source so a persistent child does not
        accumulate scratch across its lifetime. Guarded to files inside
        `native_root` only — the daemon never unlinks anything else.
        """
        scratch: Path | None = None
        pending: list[tuple[str, str]] = []
        try:
            for key in ("pending_artifacts", "artifacts"):
                items = envelope.get(key)
                if not isinstance(items, list):
                    continue
                for item in items:
                    if not isinstance(item, dict):
                        continue
                    path, artifact_type = (
                        str(item.get("path", "")),
                        str(item.get("type", "")),
                    )
                    if not (path and artifact_type):
                        continue
                    if not Path(path).is_file():
                        raise EngineExecutionError(
                            "Native backend reported a missing artifact file",
                            details={
                                "engine": self.name,
                                "operation": operation,
                                "path": path,
                            },
                        )
                    if scratch is None:
                        scratch = Path(tempfile.mkdtemp(prefix="trinity_bridge_"))
                    dest = scratch / Path(path).name
                    shutil.copyfile(path, dest)
                    pending.append((str(dest), artifact_type))
                    if native_root is not None:
                        self._unlink_native_copy(path, native_root)
        except Exception:
            if scratch is not None:
                shutil.rmtree(scratch, ignore_errors=True)
            raise
        return pending

    @staticmethod
    def _unlink_native_copy(path: str, native_root: str) -> None:
        """Best-effort removal of a copied-out artifact from the daemon's
        workspace. Only ever unlinks inside `native_root`."""
        try:
            candidate = Path(path)
            if candidate.resolve().is_relative_to(Path(native_root).resolve()):
                candidate.unlink()
        except (OSError, ValueError):
            pass
