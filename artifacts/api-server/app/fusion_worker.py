"""Shared-secret protocol helpers for the trusted Fusion desktop worker."""
from __future__ import annotations

import base64
import hashlib
import hmac
import os
import time
from dataclasses import dataclass


class FusionAuthError(ValueError):
    pass


def _secret() -> bytes:
    value = os.getenv("TRINITY_FUSION_WORKER_SECRET", "")
    if len(value) < 32:
        raise FusionAuthError("TRINITY_FUSION_WORKER_SECRET must be at least 32 characters")
    return value.encode("utf-8")


def _signature(message: str) -> str:
    return base64.urlsafe_b64encode(hmac.new(_secret(), message.encode("utf-8"), hashlib.sha256).digest()).decode("ascii").rstrip("=")


def make_worker_token(job_id: str, worker_id: str, script_sha256: str, expires_at: int) -> str:
    message = f"{job_id}|{worker_id}|{script_sha256}|{expires_at}"
    return f"{worker_id}.{expires_at}.{_signature(message)}"


def verify_worker_token(token: str, job_id: str, script_sha256: str) -> str:
    try:
        worker_id, expiry_text, signature = token.split(".", 2)
        expires_at = int(expiry_text)
    except (ValueError, TypeError) as err:
        raise FusionAuthError("Malformed Fusion worker token") from err
    if expires_at < int(time.time()):
        raise FusionAuthError("Fusion worker token has expired")
    message = f"{job_id}|{worker_id}|{script_sha256}|{expires_at}"
    expected = _signature(message)
    if not hmac.compare_digest(signature, expected):
        raise FusionAuthError("Invalid Fusion worker token")
    return worker_id


def script_hash(script: bytes) -> str:
    return hashlib.sha256(script).hexdigest()


@dataclass(frozen=True)
class FusionWorkerCapability:
    worker_id: str
    fusion_version: str
    addin_version: str
    exports: tuple[str, ...]
