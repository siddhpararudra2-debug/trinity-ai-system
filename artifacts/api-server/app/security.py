from __future__ import annotations

import hmac
import os

from fastapi import HTTPException, Request


_PUBLIC_PATHS = {"/", "/api/healthz", "/api/readyz", "/api/docs", "/api/redoc", "/api/openapi.json", "/api/auth/register", "/api/auth/login"}


def configured_api_key() -> str | None:
    key = os.getenv("TRINITY_API_KEY", "").strip()
    return key or None


def require_api_key_for_request(request: Request) -> None:
    if request.method == "OPTIONS":
        return
    configured = configured_api_key()
    if not configured or request.url.path in _PUBLIC_PATHS:
        return
    supplied = request.headers.get("x-trinity-api-key", "")
    if not supplied:
        authorization = request.headers.get("authorization", "").strip()
        scheme, _, credential = authorization.partition(" ")
        if scheme.lower() == "bearer":
            supplied = credential.strip()
    if hmac.compare_digest(supplied, configured):
        return
    authorization = request.headers.get("authorization", "")
    if authorization.lower().startswith("bearer "):
        try:
            from app.auth import decode_access_token
            decode_access_token(authorization[7:].strip())
            return
        except Exception:
            pass
    raise HTTPException(status_code=401, detail="A valid Trinity API key or bearer access token is required")


def validate_worker_secret(value: str | None) -> None:
    if not value or len(value) < 32:
        raise ValueError("Worker secrets must contain at least 32 characters")
