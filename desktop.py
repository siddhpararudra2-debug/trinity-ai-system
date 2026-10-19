#!/usr/bin/env python3
"""Trinity desktop shell.

Starts the API and the frontend, waits until both genuinely answer, then
opens a native window pointed at the app. Closing the window shuts both
processes down.

This is deliberately the thin option. The work is starting two processes and
showing a URL, so the shell does exactly that and nothing more -- there is
no bundler, no packaging pipeline and no second copy of the UI. Electron or
Tauri would buy a more native feel for a large increase in build surface,
and Electron alone would add a full Node runtime to ship.

pywebview is optional. Without it the shell still starts the stack and falls
back to the system browser, so this is never a hard dependency:

    pip install -r requirements-desktop.txt   # to get the window

Usage:
    python desktop.py
    python desktop.py --no-window            # start the stack, open nothing
    python desktop.py --api-port 8100 --web-port 3100
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import webbrowser
from pathlib import Path

ROOT = Path(__file__).resolve().parent
FRONTEND = ROOT / "frontend"
QT_HINT = "TRINITY_QT_BIN"


def step(msg: str) -> None:
    print(f"==> {msg}", flush=True)


def ok(msg: str) -> None:
    print(f"    {msg}", flush=True)


def warn(msg: str) -> None:
    print(f"    ! {msg}", flush=True)


def find_native() -> Path | None:
    """Prefer the vcpkg build; the plain one has no OpenCV."""
    build = ROOT / "app" / "native" / "build"
    if not build.is_dir():
        return None
    found = sorted(
        build.rglob("Trinity.exe"),
        # vcpkg leg first, then the shallowest path
        key=lambda p: (0 if "vcpkg" in str(p).lower() else 1, len(str(p))),
    )
    return found[0] if found else None


def find_qt() -> tuple[Path | None, bool]:
    """Resolve the Qt runtime.

    Returns (directory_to_add, ok). These are separate answers because
    "already on PATH" and "not installed" both need no directory to add but
    mean opposite things -- conflating them made the shell warn that the
    native engine would not start, and then watch it answer a call.

    Trinity.exe links Qt6Core but ships no Qt DLLs beside itself.
    """
    override = os.getenv(QT_HINT)
    if override and (Path(override) / "Qt6Core.dll").exists():
        return Path(override), True
    for entry in os.getenv("Path", os.getenv("PATH", "")).split(os.pathsep):
        if entry and (Path(entry) / "Qt6Core.dll").exists():
            return None, True  # resolvable already
    root = Path("C:/Qt")
    if root.is_dir():
        for version in sorted(root.iterdir()):
            if not version.is_dir():
                continue
            for kit in sorted(version.iterdir()):
                candidate = kit / "bin"
                if (candidate / "Qt6Core.dll").exists():
                    return candidate, True
    return None, False


def healthy(url: str, timeout: float = 3.0) -> bool:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            return resp.status == 200
    except (urllib.error.URLError, OSError, ValueError):
        return False


def wait_healthy(url: str, proc: subprocess.Popen, seconds: int, label: str) -> bool:
    deadline = time.time() + seconds
    while time.time() < deadline:
        if proc.poll() is not None:
            warn(f"{label} exited with code {proc.returncode}")
            return False
        if healthy(url):
            return True
        time.sleep(0.4)
    return False


def spawn(cmd: list[str], cwd: Path, log: Path) -> subprocess.Popen:
    handle = log.open("w", encoding="utf-8", errors="replace")
    return subprocess.Popen(
        cmd,
        cwd=str(cwd),
        stdout=handle,
        stderr=subprocess.STDOUT,
        # A process group so we can take the whole tree down at once;
        # `next start` and uvicorn both spawn children.
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP
        if os.name == "nt"
        else 0,
        start_new_session=os.name != "nt",
    )


def terminate(proc: subprocess.Popen | None, label: str) -> None:
    if proc is None or proc.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/PID", str(proc.pid), "/T", "/F"],
            capture_output=True,
            check=False,
        )
    else:
        try:
            os.killpg(os.getpgid(proc.pid), 15)
        except (ProcessLookupError, PermissionError):
            proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        pass
    ok(f"stopped {label}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Trinity in a desktop window.")
    parser.add_argument("--api-port", type=int, default=8000)
    parser.add_argument("--web-port", type=int, default=3000)
    parser.add_argument("--no-window", action="store_true", help="do not open a window")
    parser.add_argument(
        "--window-timeout",
        type=float,
        default=0,
        help="close the window after N seconds (0 = wait for the user). "
        "Lets the shutdown path be tested rather than asserted.",
    )
    args = parser.parse_args()

    api_url = f"http://127.0.0.1:{args.api_port}"
    web_url = f"http://127.0.0.1:{args.web_port}"
    run_dir = ROOT / ".run"
    run_dir.mkdir(exist_ok=True)

    step("Trinity desktop")

    native = find_native()
    if native:
        os.environ["TRINITY_NATIVE_EXEC"] = str(native)
        kind = "OpenCV build" if "vcpkg" in str(native).lower() else "plain build, no OpenCV"
        ok(f"native: {native} ({kind})")
        qt, qt_ok = find_qt()
        if qt is not None:
            os.environ["Path"] = f"{qt};{os.environ.get('Path', '')}"
            os.environ["PATH"] = os.environ["Path"]
            ok(f"qt:     {qt}")
        elif qt_ok:
            ok("qt:     already on PATH")
        else:
            warn("qt:     Qt6Core.dll not found; the native engine will not start")
            ok(f"set {QT_HINT} to the directory holding it")
    else:
        warn("native: no Trinity.exe under app/native/build")
        ok("the six bridged consoles will report the backend unavailable")

    os.environ["TRINITY_STORAGE_ROOT"] = str(ROOT / "data")
    # Inlined at build time, so it must be set before `next build`.
    os.environ["NEXT_PUBLIC_TRINITY_API_URL"] = f"{api_url}/api"
    ok(f"api target: {api_url}/api")

    api: subprocess.Popen | None = None
    web: subprocess.Popen | None = None
    try:
        step("starting frontend")
        built = (FRONTEND / ".next" / "BUILD_ID").exists()
        if not built and not (FRONTEND / "node_modules").exists():
            warn("frontend has no node_modules; run 'npm ci' in frontend/ first")
        # On Windows npm is a .cmd batch shim, which CreateProcess cannot
        # execute directly -- resolving the real name avoids needing
        # shell=True, which would detach the process group we rely on for
        # clean shutdown.
        npm = "npm.cmd" if os.name == "nt" else "npm"
        web_cmd = (
            [npm, "run", "dev", "--", "-p", str(args.web_port)]
            if os.getenv("TRINITY_DESKTOP_DEV")
            else [npm, "run", "start", "--", "-p", str(args.web_port)]
        )
        web = spawn(web_cmd, FRONTEND, run_dir / "web.log")

        step("starting api")
        api = spawn(
            [sys.executable, "-m", "uvicorn", "src.main:app",
             "--host", "127.0.0.1", "--port", str(args.api_port)],
            ROOT,
            run_dir / "api.log",
        )

        step("waiting for both to answer")
        api_ok = wait_healthy(f"{api_url}/api/health", api, 90, "api")
        web_ok = wait_healthy(f"{web_url}/", web, 120, "frontend")
        if not (api_ok and web_ok):
            warn("the stack did not come up; see .run/api.log and .run/web.log")
            return 1

        ok(f"api      {api_url}  (health ok)")
        ok(f"frontend {web_url}  (200 ok)")
        ok(f"logs     {run_dir}")

        # /api/health only proves the process is up. Every engine here is
        # native, so a dead bridge means a window full of failure messages.
        if native:
            try:
                import json

                body = json.dumps(
                    {"engine": "research", "operation": "search",
                     "parameters": {"query": "desktop startup probe"}}
                ).encode()
                req = urllib.request.Request(
                    f"{api_url}/api/execute", data=body,
                    headers={"Content-Type": "application/json"},
                )
                with urllib.request.urlopen(req, timeout=30) as resp:
                    if json.loads(resp.read()).get("success"):
                        ok("engine:  native bridge answered a real call")
                    else:
                        warn("engine:  native bridge refused the probe")
            except Exception as exc:  # noqa: BLE001 - report, never crash the shell
                warn(f"engine:  native bridge probe failed -- {exc}")

        if args.no_window:
            step("running. Press Ctrl+C to stop both.")
            while api.poll() is None and web.poll() is None:
                time.sleep(0.5)
            return 0

        step("opening window")
        try:
            import webview  # type: ignore[import-not-found]
        except ImportError:
            warn("pywebview is not installed; opening the system browser instead")
            ok("pip install -r requirements-desktop.txt for a native window")
            webbrowser.open(web_url)
            while api.poll() is None and web.poll() is None:
                time.sleep(0.5)
            return 0

        webview.create_window("Trinity AI", web_url, width=1440, height=900)

        if args.window_timeout > 0:
            # webview.start() blocks until every window closes, so the
            # timeout has to fire a close on another thread. Without this
            # flag, "closing the window shuts both processes down" is a
            # claim only a human can verify.
            def _auto_close() -> None:
                time.sleep(args.window_timeout)
                try:
                    webview.windows[0].destroy()
                except Exception:  # noqa: BLE001 - teardown is best-effort
                    pass

            threading.Thread(target=_auto_close, daemon=True).start()

        webview.start()  # blocks until the window is closed
        return 0
    except KeyboardInterrupt:
        return 0
    finally:
        step("shutting down")
        terminate(web, "frontend")
        terminate(api, "api")


if __name__ == "__main__":
    raise SystemExit(main())