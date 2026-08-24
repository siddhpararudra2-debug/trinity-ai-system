from __future__ import annotations

import hmac
import os

from fastapi import HTTPException, Request


_PUBLIC_PATHS = {"/api/healthz", "/api/docs", "/api/redoc", "/api/openapi.json"}


def configured_api_key() -> str | None:
    key = os.getenv("TRINITY_API_KEY", "").strip()
    return key or None


def require_api_key_for_request(request: Request) -> None:
    configured = configured_api_key()
    if not configured or request.url.path in _PUBLIC_PATHS or request.url.path.startswith("/api/ws"):
        return
    supplied = request.headers.get("x-trinity-api-key", "")
    if not supplied:
        authorization = request.headers.get("authorization", "")
        supplied = authorization.removeprefix("Bearer ").strip()
    if not hmac.compare_digest(supplied, configured):
        raise HTTPException(status_code=401, detail="A valid Trinity API key is required")


def validate_worker_secret(value: str | None) -> None:
    if not value or len(value) < 32:
        raise ValueError("Worker secrets must contain at least 32 characters")
