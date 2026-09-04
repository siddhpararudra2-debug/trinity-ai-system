from __future__ import annotations

import os
from datetime import datetime, timedelta, UTC
from typing import Any

from fastapi import Depends, HTTPException, Request
from jose import JWTError, jwt
from passlib.context import CryptContext
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.database import get_db
from app.models import User

_ITERATIONS = 310_000
_INSECURE_SECRET = "local-development-auth-secret-change-me"
_ALGORITHM = "HS256"
_DEFAULT_TOKEN_LIFETIME = 86_400  # 24 hours

pwd_context = CryptContext(schemes=["argon2"], deprecated="auto")


def _auth_secret() -> str:
    value = os.getenv("TRINITY_AUTH_SECRET") or os.getenv("TRINITY_API_KEY") or _INSECURE_SECRET
    return value


def validate_auth_configuration() -> None:
    """Reject forgeable defaults whenever bearer auth is enabled."""
    # Warn even when auth not required: insecure default + placeholder secrets must be replaced
    placeholder = "replace-with"
    insecure = os.getenv("TRINITY_AUTH_SECRET") or os.getenv("TRINITY_API_KEY") or ""
    if insecure and placeholder in insecure.lower():
        raise RuntimeError("Placeholder secret detected — replace TRINITY_AUTH_SECRET / TRINITY_API_KEY with real random values")
    if os.getenv("TRINITY_AUTH_REQUIRED", "0") != "1":
        # In development, warn if using insecure default
        value = os.getenv("TRINITY_AUTH_SECRET") or os.getenv("TRINITY_API_KEY") or ""
        if not value or value == _INSECURE_SECRET or len(value) < 32:
            import logging
            logging.getLogger("trinity.api").warning("TRINITY_AUTH_SECRET not set or too short — using insecure default; set a 32+ char secret for any non-dev use")
        return
    value = os.getenv("TRINITY_AUTH_SECRET") or os.getenv("TRINITY_API_KEY")
    if not value or len(value) < 32 or value == _INSECURE_SECRET:
        raise RuntimeError(
            "TRINITY_AUTH_REQUIRED=1 requires TRINITY_AUTH_SECRET or TRINITY_API_KEY "
            "with at least 32 non-default characters"
        )


def hash_password(password: str) -> str:
    return pwd_context.hash(password)


def verify_password(password: str, encoded: str) -> bool:
    return pwd_context.verify(password, encoded)


def issue_access_token(user_id: int, expires_in: int = _DEFAULT_TOKEN_LIFETIME) -> str:
    now = datetime.now(UTC)
    payload = {
        "sub": str(user_id),
        "iat": now,
        "exp": now + timedelta(seconds=expires_in),
        "iss": "trinity-ai",
    }
    return jwt.encode(payload, _auth_secret(), algorithm=_ALGORITHM)


def decode_access_token(token: str) -> int:
    try:
        payload = jwt.decode(token, _auth_secret(), algorithms=[_ALGORITHM], issuer="trinity-ai")
        user_id = payload.get("sub")
        if user_id is None:
            raise ValueError("missing subject claim")
        return int(user_id)
    except (JWTError, ValueError, TypeError) as exc:
        raise HTTPException(status_code=401, detail="Invalid or expired access token") from exc


def bearer_token(request: Request) -> str | None:
    header = request.headers.get("authorization", "")
    if header.lower().startswith("bearer "):
        return header[7:].strip()
    return None


async def get_current_user(request: Request, db: AsyncSession = Depends(get_db)) -> User:
    token = bearer_token(request)
    if not token:
        raise HTTPException(status_code=401, detail="Bearer access token required")
    user_id = decode_access_token(token)
    result = await db.execute(select(User).where(User.id == user_id))
    user = result.scalar_one_or_none()
    if user is None:
        raise HTTPException(status_code=401, detail="User no longer exists")
    return user


async def get_current_admin(user: User = Depends(get_current_user)) -> User:
    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Administrator role required")
    return user


async def get_optional_user(request: Request, db: AsyncSession = Depends(get_db)) -> User | None:
    token = bearer_token(request)
    if token:
        try:
            return await get_current_user(request, db)
        except HTTPException:
            # When auth not required, treat invalid token as anonymous rather than 401
            if os.getenv("TRINITY_AUTH_REQUIRED", "0") == "1":
                raise
            return None
    if os.getenv("TRINITY_AUTH_REQUIRED", "0") == "1":
        raise HTTPException(status_code=401, detail="Bearer access token required")
    return None


def user_payload(user: User) -> dict[str, Any]:
    return {"id": user.id, "email": user.email, "role": user.role, "created_at": user.created_at.isoformat() if user.created_at else None}
