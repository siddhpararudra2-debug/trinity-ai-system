#!/usr/bin/env python3
"""Trinity Python engine host.

A line-delimited JSON RPC host over stdin/stdout that exposes the EXISTING
backend engine registry (backend/app.engines) to the native desktop core.
No changes are made to backend/app; this host imports it read-only.

Frame protocol (mirrors trinity::ipc::Frame):
    {"id": "...", "type": "request",  "method": "...", "payload": {...}}
    {"id": "...", "type": "response", "ok": true,  "result": {...}}
    {"id": "...", "type": "response", "ok": false, "error": {"code": "...", "message": "...", "details": {}}}

Methods:
    hello                -> host/version/engines info
    engines.list         -> engine descriptors
    engine.execute       -> run one engine operation, returns ToolResponse dict
    ping                 -> {"pong": true}

The host is started by the native process runner with sanitized argv:
    python -X utf8 engine_host.py --backend <path-to-backend>
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

HOST_VERSION = "1.0.0"


class HostError(Exception):
    def __init__(self, code: str, message: str, details: dict | None = None):
        self.code = code
        self.message = message
        self.details = details or {}
        super().__init__(message)

    def to_dict(self) -> dict:
        return {"code": self.code, "message": self.message, "details": self.details}


def emit(frame: dict) -> None:
    """Write one JSON frame; flush immediately (the native side blocks on it)."""
    sys.stdout.write(json.dumps(frame, ensure_ascii=True) + "\n")
    sys.stdout.flush()


def make_backend_importable(backend_path: str) -> None:
    resolved = Path(backend_path).resolve()
    if not (resolved / "app" / "main.py").exists():
        raise HostError(
            "path_validation_error",
            f"--backend does not point at the Trinity backend: {backend_path}",
        )
    if str(resolved) not in sys.path:
        sys.path.insert(0, str(resolved))


def bootstrap_registry():
    """Import the existing backend and bootstrap its engine registry."""
    try:
        from app.core.config import ensure_storage_layout  # noqa: F401
        from app.db.database import init_db
        from app.engines.registry import bootstrap_engines, registry

        init_db()
        bootstrap_engines()
        return registry
    except Exception as exc:  # pragma: no cover - import failures are fatal
        raise HostError("engine_execution_error", f"backend bootstrap failed: {exc}")


def describe_engines(registry) -> list[dict]:
    descriptors = []
    for description in registry.list():
        entry = {
            "id": description["name"],
            "name": description["name"],
            "version": description.get("version", "0"),
            "capabilities": list(description.get("capabilities", [])),
            "status": description.get("status", "available"),
            "source": "python-engine-host",
        }
        descriptors.append(entry)
    return descriptors


def execute_engine(registry, payload: dict) -> dict:
    engine_name = payload.get("engine")
    operation = payload.get("operation")
    parameters = payload.get("parameters") or {}
    if not engine_name or not operation:
        raise HostError("request_validation_error", "engine and operation are required")

    from app.core.errors import TrinityError
    from app.jobs.manager import job_manager

    try:
        return job_manager.run_sync(engine_name, operation, parameters)
    except TrinityError as exc:
        raise HostError(exc.code, exc.message, exc.details) from exc


def handle_request(registry, method: str, payload: dict) -> dict:
    if method == "ping":
        return {"pong": True}
    if method == "hello":
        return {
            "host": "trinity-python-engine-host",
            "version": HOST_VERSION,
            "python": sys.version.split()[0],
            "engines": len(describe_engines(registry)),
        }
    if method == "engines.list":
        return {"engines": describe_engines(registry)}
    if method == "engine.execute":
        return execute_engine(registry, payload)
    raise HostError("request_validation_error", f"no such method '{method}'")


def serve(backend_path: str) -> int:
    registry = None
    try:
        make_backend_importable(backend_path)
        registry = bootstrap_registry()
    except HostError as exc:
        emit({"id": "bootstrap", "type": "response", "ok": False, "error": exc.to_dict()})
        return 1

    emit(
        {
            "id": "bootstrap",
            "type": "event",
            "name": "host_ready",
            "payload": {"engines": len(describe_engines(registry))},
        }
    )

    for raw_line in sys.stdin:
        line = raw_line.strip()
        if not line:
            continue
        try:
            request = json.loads(line)
        except json.JSONDecodeError as exc:
            emit(
                {
                    "id": "unknown",
                    "type": "response",
                    "ok": False,
                    "error": HostError("ipc_protocol_error", f"bad JSON frame: {exc}").to_dict(),
                }
            )
            continue

        request_id = request.get("id", "unknown")
        if request.get("type") != "request":
            emit(
                {
                    "id": request_id,
                    "type": "response",
                    "ok": False,
                    "error": HostError("ipc_protocol_error", "expected a request frame").to_dict(),
                }
            )
            continue
        try:
            result = handle_request(registry, request.get("method", ""), request.get("payload") or {})
            emit({"id": request_id, "type": "response", "ok": True, "result": result})
        except HostError as exc:
            emit({"id": request_id, "type": "response", "ok": False, "error": exc.to_dict()})
        except Exception as exc:  # noqa: BLE001 - last-resort classified failure
            emit(
                {
                    "id": request_id,
                    "type": "response",
                    "ok": False,
                    "error": HostError("engine_execution_error", str(exc)).to_dict(),
                }
            )
    return 0


def selftest() -> int:
    """Offline protocol self-test (no backend import required)."""
    import io

    captured = io.StringIO()
    original_stdout = sys.stdout
    sys.stdout = captured
    try:
        emit({"id": "t1", "type": "response", "ok": True, "result": {"pong": True}})
    finally:
        sys.stdout = original_stdout
    line = captured.getvalue().strip()
    frame = json.loads(line)
    assert frame["id"] == "t1" and frame["ok"] and frame["result"]["pong"] is True
    print("engine_host selftest: OK")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Trinity Python engine host")
    parser.add_argument("--backend", help="path to the backend/ directory")
    parser.add_argument("--selftest", action="store_true", help="run protocol self-test")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    if not args.backend:
        print("--backend is required", file=sys.stderr)
        return 2

    # Line-buffered, unbuffered protocol stream.
    try:
        sys.stdout.reconfigure(line_buffering=True)  # type: ignore[union-attr]
    except Exception:  # noqa: BLE001
        pass

    return serve(args.backend)


if __name__ == "__main__":
    sys.exit(main())
