"""
Structured logging for Trinity.

V1 uses the standard library only (per PRD section 46 — Observability).
Logs are emitted as single-line JSON so they're trivially parseable by
whatever ships later (OpenTelemetry / Prometheus / Grafana) without
changing this module's public surface.
"""
from __future__ import annotations

import json
import logging
import sys
import time
from typing import Any


class JSONFormatter(logging.Formatter):
    def format(self, record: logging.LogRecord) -> str:
        payload: dict[str, Any] = {
            "ts": round(time.time(), 6),
            "level": record.levelname,
            "logger": record.name,
            "message": record.getMessage(),
        }
        # Allow callers to attach structured context via `extra={"ctx": {...}}`
        ctx = getattr(record, "ctx", None)
        if ctx:
            payload["ctx"] = ctx
        if record.exc_info:
            payload["exc_info"] = self.formatException(record.exc_info)
        return json.dumps(payload, default=str)


def configure_logging(level: int = logging.INFO) -> None:
    root = logging.getLogger("trinity")
    if root.handlers:
        # Already configured (e.g. re-imported under uvicorn --reload)
        root.setLevel(level)
        return

    handler = logging.StreamHandler(stream=sys.stdout)
    handler.setFormatter(JSONFormatter())

    root.setLevel(level)
    root.addHandler(handler)
    root.propagate = False


def get_logger(name: str) -> logging.Logger:
    return logging.getLogger(f"trinity.{name}")
