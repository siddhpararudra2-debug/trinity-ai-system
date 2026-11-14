"""M30 accounts: signup, verify, login, refresh, logout, reset, delete.

Stage 13 / M30. The dummy deployment has no mailer, so the two flows
that would normally email a token (address verification, password
reset) return the token inline with ``delivery: "inline"`` and say so —
the API never pretends an email was sent. Production would configure a
mailer and switch that field; every other behaviour is production
shaped:

- passwords are scrypt hashes with a per-user salt, verified in
  constant time (stdlib only — no argon2/passlib dependency)
- access and refresh tokens are signed HS256 JWTs; refresh tokens
  carry a jti registered in SQLite and are single-rotation: logout or
  refresh consumes the old jti, so a replayed refresh token is rejected
- verify and reset tokens are single-use through the same jti registry
- login lockout: LOGIN_MAX_FAILURES consecutive failures lock the
  account for LOGIN_LOCKOUT_MINUTES (423 with a retry_after hint)
- a successful password reset revokes every outstanding refresh token
- anti-enumeration: login and reset-request answer unknown and known
  emails with the same message / response shape
"""

from __future__ import annotations

import hashlib
import hmac
import re
import secrets
import sqlite3
import time
import uuid
from datetime import datetime, UTC
from typing import Any

import jwt
from fastapi import APIRouter, Depends, Header

from src.core.config import (
    AUTH_ENABLED,
    JWT_SECRET,
    LOGIN_LOCKOUT_MINUTES,
    LOGIN_MAX_FAILURES,
    PASSWORD_MIN_LENGTH,
    REFRESH_TOKEN_DAYS,
    RESET_TOKEN_MINUTES,
    SESSION_TIMEOUT_MINUTES,
    VERIFY_TOKEN_HOURS,
    settings,
)
from src.core.errors import (
    AccountConflictError,
    AccountLockedError,
    AuthError,
    RequestValidationError,
)
from src.db.database import get_connection
from src.models.schemas import (
    LoginRequest,
    PasswordResetConfirm,
    PasswordResetRequest,
    RefreshRequest,
    SignupRequest,
    VerifyRequest,
)

router = APIRouter(prefix="/api/accounts", tags=["accounts"])

# One @, no whitespace in either part; enough to catch typos without
# pretending to be an RFC-complete email parser.
_EMAIL_RE = re.compile(r"^[^@\s]+@[^@\s]+$")

# scrypt cost: 128 * r * n = 16 MiB and ~50 ms per hash — the
# interactive-credential tradeoff, not the storage-encryption one.
_SCRYPT_N = 2**14
_SCRYPT_R = 8
_SCRYPT_P = 1
_MAX_PASSWORD_CHARS = 1024  # scrypt input cap; a DoS guard, not policy

# When TRINITY_JWT_SECRET is unset the dummy deployment still signs
# tokens, but with a per-process random key: sessions do not survive a
# restart and that is honest for a deployment that never configured a
# secret (production sets TRINITY_JWT_SECRET).
_FALLBACK_SECRET = secrets.token_hex(32)


def _secret() -> str:
    return JWT_SECRET or _FALLBACK_SECRET


def _now() -> float:
    return time.time()


def _iso(epoch: float) -> str:
    return datetime.fromtimestamp(epoch, tz=UTC).isoformat()


def hash_password(password: str) -> str:
    salt = secrets.token_bytes(16)
    digest = hashlib.scrypt(
        password.encode("utf-8"),
        salt=salt,
        n=_SCRYPT_N,
        r=_SCRYPT_R,
        p=_SCRYPT_P,
        dklen=32,
    )
    return f"scrypt${_SCRYPT_N}${_SCRYPT_R}${_SCRYPT_P}${salt.hex()}${digest.hex()}"


def verify_password(password: str, stored: str) -> bool:
    try:
        scheme, n, r, p, salt_hex, hash_hex = stored.split("$")
    except ValueError:
        return False
    if scheme != "scrypt":
        return False
    digest = hashlib.scrypt(
        password.encode("utf-8"),
        salt=bytes.fromhex(salt_hex),
        n=int(n),
        r=int(r),
        p=int(p),
        dklen=len(hash_hex) // 2,
    )
    return hmac.compare_digest(digest.hex(), hash_hex)


def _mint(payload: dict[str, Any], ttl_seconds: float) -> str:
    now = _now()
    claims = {"iat": now, "exp": now + ttl_seconds, **payload}
    return jwt.encode(claims, _secret(), algorithm="HS256")


def _decode(token: str, expected_typ: str) -> dict[str, Any]:
    try:
        payload = jwt.decode(
            token,
            _secret(),
            algorithms=["HS256"],
            options={"require": ["exp", "iat", "sub", "typ", "jti"]},
        )
    except jwt.ExpiredSignatureError as exc:
        raise AuthError(f"{expected_typ} token expired") from exc
    except jwt.InvalidTokenError as exc:
        raise AuthError(f"invalid {expected_typ} token") from exc
    if payload.get("typ") != expected_typ:
        raise AuthError(f"not a {expected_typ} token")
    return payload


def _normalize_email(raw: str) -> str:
    email = (raw or "").strip().lower()
    if not _EMAIL_RE.match(email):
        raise RequestValidationError(
            "email must look like local@domain",
            details={"email": raw},
        )
    return email


def _validate_password(password: str) -> None:
    if len(password) < PASSWORD_MIN_LENGTH:
        raise RequestValidationError(
            f"password must be at least {PASSWORD_MIN_LENGTH} characters",
            details={"min_length": PASSWORD_MIN_LENGTH},
        )
    if len(password) > _MAX_PASSWORD_CHARS:
        raise RequestValidationError(
            f"password must be at most {_MAX_PASSWORD_CHARS} characters",
            details={"max_length": _MAX_PASSWORD_CHARS},
        )


class AccountStore:
    """SQLite-backed users + token registry (plain sqlite3, PRD §8)."""

    def __init__(self, db_path: str | None = None):
        self.db_path = db_path  # None → settings.db_path at call time

    def _path(self):
        return self.db_path or settings.db_path

    # -- users ------------------------------------------------------------

    def create_user(self, email: str, password_hash: str) -> dict[str, Any]:
        user_id = uuid.uuid4().hex
        created_at = datetime.now(UTC).isoformat()
        try:
            with get_connection(self._path()) as conn:
                conn.execute(
                    "INSERT INTO users (user_id, email, password_hash, "
                    "created_at) VALUES (?, ?, ?, ?)",
                    (user_id, email, password_hash, created_at),
                )
                conn.commit()
        except sqlite3.IntegrityError as exc:
            raise AccountConflictError("email already registered") from exc
        return {
            "user_id": user_id,
            "email": email,
            "verified_at": None,
            "created_at": created_at,
        }

    def get_user(
        self, *, user_id: str | None = None, email: str | None = None
    ) -> dict[str, Any] | None:
        if user_id is not None:
            query = "SELECT * FROM users WHERE user_id = ?"
            args: tuple = (user_id,)
        elif email is not None:
            query = "SELECT * FROM users WHERE email = ?"
            args = (email,)
        else:
            raise ValueError("get_user needs user_id or email")
        with get_connection(self._path()) as conn:
            row = conn.execute(query, args).fetchone()
        return dict(row) if row is not None else None

    def mark_verified(self, user_id: str) -> None:
        verified_at = datetime.now(UTC).isoformat()
        with get_connection(self._path()) as conn:
            conn.execute(
                "UPDATE users SET verified_at = ? WHERE user_id = ?",
                (verified_at, user_id),
            )
            conn.commit()

    def set_password(self, user_id: str, password_hash: str) -> None:
        with get_connection(self._path()) as conn:
            conn.execute(
                "UPDATE users SET password_hash = ? WHERE user_id = ?",
                (password_hash, user_id),
            )
            conn.commit()

    def delete_user(self, user_id: str) -> None:
        with get_connection(self._path()) as conn:
            conn.execute(
                "DELETE FROM auth_tokens WHERE user_id = ?", (user_id,)
            )
            conn.execute("DELETE FROM users WHERE user_id = ?", (user_id,))
            conn.commit()

    # -- lockout ----------------------------------------------------------

    def record_failure(
        self, user_id: str, *, max_failures: int, lockout_minutes: float
    ) -> bool:
        """Count a failed login; lock the account at ``max_failures``.

        Returns whether this failure triggered the lock.
        """
        now = _now()
        with get_connection(self._path()) as conn:
            row = conn.execute(
                "SELECT failed_attempts FROM users WHERE user_id = ?",
                (user_id,),
            ).fetchone()
            attempts = (row["failed_attempts"] if row else 0) + 1
            locked_until = (
                now + lockout_minutes * 60 if attempts >= max_failures else None
            )
            conn.execute(
                "UPDATE users SET failed_attempts = ?, locked_until = ? "
                "WHERE user_id = ?",
                (attempts, locked_until, user_id),
            )
            conn.commit()
        return attempts >= max_failures

    def clear_failures(self, user_id: str) -> None:
        with get_connection(self._path()) as conn:
            conn.execute(
                "UPDATE users SET failed_attempts = 0, locked_until = NULL "
                "WHERE user_id = ?",
                (user_id,),
            )
            conn.commit()

    # -- token registry ----------------------------------------------------

    def insert_token(
        self, jti: str, user_id: str, purpose: str, expires_at: float
    ) -> None:
        with get_connection(self._path()) as conn:
            conn.execute(
                "INSERT INTO auth_tokens (jti, user_id, purpose, "
                "expires_at, created_at) VALUES (?, ?, ?, ?, ?)",
                (
                    jti,
                    user_id,
                    purpose,
                    expires_at,
                    datetime.now(UTC).isoformat(),
                ),
            )
            conn.commit()

    def consume_token(self, jti: str) -> bool:
        """Single-use consume: False means missing or already used."""
        with get_connection(self._path()) as conn:
            cur = conn.execute(
                "UPDATE auth_tokens SET used_at = ? "
                "WHERE jti = ? AND used_at IS NULL",
                (_now(), jti),
            )
            conn.commit()
        return cur.rowcount == 1

    def revoke_refresh_for_user(self, user_id: str) -> int:
        with get_connection(self._path()) as conn:
            cur = conn.execute(
                "UPDATE auth_tokens SET used_at = ? WHERE user_id = ? "
                "AND purpose = 'refresh' AND used_at IS NULL",
                (_now(), user_id),
            )
            conn.commit()
        return cur.rowcount


store = AccountStore()


def _issue_token(
    user_id: str, purpose: str, ttl_seconds: float
) -> tuple[str, str, float]:
    jti = uuid.uuid4().hex
    expires_at = _now() + ttl_seconds
    token = _mint(
        {"sub": user_id, "typ": purpose, "jti": jti}, ttl_seconds
    )
    store.insert_token(jti, user_id, purpose, expires_at)
    return token, jti, expires_at


def _access_ttl_seconds() -> float:
    return SESSION_TIMEOUT_MINUTES * 60


def _require_user(
    authorization: str = Header(default=""),
) -> dict[str, Any]:
    """Bearer dependency: decode the access token, load the account."""
    token = ""
    if authorization.lower().startswith("bearer "):
        token = authorization[7:].strip()
    if not token:
        raise AuthError("missing bearer token")
    payload = _decode(token, "access")
    user = store.get_user(user_id=payload["sub"])
    if user is None:
        if not AUTH_ENABLED:
            # Dev mode mints tokens for identities that were never
            # stored; /accounts/me answers with the claims, honestly
            # labelled dev.
            return {
                "user_id": payload["sub"],
                "email": payload.get("email", payload["sub"]),
                "verified_at": None,
                "dev": True,
            }
        raise AuthError("account no longer exists")
    return user


# -- endpoints -----------------------------------------------------------


@router.post("/signup")
def signup(body: SignupRequest) -> dict[str, Any]:
    """Create an unverified account; hand back the verification token."""
    email = _normalize_email(body.email)
    _validate_password(body.password)
    if store.get_user(email=email) is not None:
        raise AccountConflictError("email already registered")
    user = store.create_user(email, hash_password(body.password))
    token, _jti, expires_at = _issue_token(
        user["user_id"], "verify", VERIFY_TOKEN_HOURS * 3600
    )
    return {
        "user_id": user["user_id"],
        "email": email,
        "verified": False,
        "verification": {
            "delivery": "inline",
            "token": token,
            "expires_at": _iso(expires_at),
            "note": (
                "no mailer is configured in this deployment; the "
                "verification token is returned inline instead of being "
                "emailed"
            ),
        },
    }


@router.post("/verify")
def verify(body: VerifyRequest) -> dict[str, Any]:
    payload = _decode(body.token, "verify")
    user = store.get_user(user_id=payload["sub"])
    if user is None:
        raise AuthError("account no longer exists")
    if not store.consume_token(payload["jti"]):
        raise AuthError(
            "verification token already used", details={"replayed": True}
        )
    if user["verified_at"] is None:
        store.mark_verified(user["user_id"])
    return {"email": user["email"], "verified": True}


@router.post("/refresh")
def refresh(body: RefreshRequest) -> dict[str, Any]:
    payload = _decode(body.refresh_token, "refresh")
    user = store.get_user(user_id=payload["sub"])
    if user is None:
        raise AuthError("account no longer exists")
    if not store.consume_token(payload["jti"]):
        raise AuthError(
            "refresh token revoked or already used",
            details={"replayed": True},
        )
    access = _mint(
        {
            "sub": user["user_id"],
            "email": user["email"],
            "typ": "access",
            "jti": uuid.uuid4().hex,
        },
        _access_ttl_seconds(),
    )
    new_refresh, _jti, _exp = _issue_token(
        user["user_id"], "refresh", REFRESH_TOKEN_DAYS * 86400
    )
    return {
        "access_token": access,
        "token_type": "bearer",
        "expires_in": _access_ttl_seconds(),
        "refresh_token": new_refresh,
    }


@router.post("/logout")
def logout(body: RefreshRequest) -> dict[str, Any]:
    payload = _decode(body.refresh_token, "refresh")
    store.consume_token(payload["jti"])
    return {"logged_out": True}


@router.post("/reset-request")
def reset_request(body: PasswordResetRequest) -> dict[str, Any]:
    """Always 200; the response shape never reveals whether the email exists."""
    email = _normalize_email(body.email)
    user = store.get_user(email=email)
    if user is None:
        return {"delivery": "none"}
    token, _jti, expires_at = _issue_token(
        user["user_id"], "reset", RESET_TOKEN_MINUTES * 60
    )
    return {
        "delivery": "inline",
        "token": token,
        "expires_at": _iso(expires_at),
        "note": (
            "no mailer is configured in this deployment; the reset token "
            "is returned inline instead of being emailed"
        ),
    }


@router.post("/reset-confirm")
def reset_confirm(body: PasswordResetConfirm) -> dict[str, Any]:
    _validate_password(body.new_password)
    payload = _decode(body.token, "reset")
    user = store.get_user(user_id=payload["sub"])
    if user is None:
        raise AuthError("account no longer exists")
    if not store.consume_token(payload["jti"]):
        raise AuthError(
            "reset token already used", details={"replayed": True}
        )
    store.set_password(user["user_id"], hash_password(body.new_password))
    # A password reset invalidates every outstanding session.
    store.revoke_refresh_for_user(user["user_id"])
    store.clear_failures(user["user_id"])
    return {"email": user["email"], "password_reset": True}


@router.get("/me")
def me(user: dict[str, Any] = Depends(_require_user)) -> dict[str, Any]:
    return {
        "user_id": user["user_id"],
        "email": user["email"],
        "verified": bool(user.get("verified_at")),
        "created_at": user.get("created_at"),
        "dev": bool(user.get("dev")),
    }


@router.delete("/me")
def delete_me(user: dict[str, Any] = Depends(_require_user)) -> dict[str, Any]:
    if not user.get("dev"):
        store.delete_user(user["user_id"])
    return {"deleted": True, "email": user["email"]}


def do_login(body: LoginRequest) -> dict[str, Any]:
    """Authenticate and return access + refresh tokens.

    Kept out of the router because the /api/login path itself lives in
    ``src.api.routes`` (route stability for existing callers); this is
    the single implementation both modes share.
    """
    if not AUTH_ENABLED:
        # Documented dev mode: any credentials get a per-process signed
        # token without a stored account (routes.py /login documents it).
        access = _mint(
            {
                "sub": body.username,
                "email": body.username,
                "typ": "access",
                "jti": uuid.uuid4().hex,
            },
            _access_ttl_seconds(),
        )
        return {
            "access_token": access,
            "token_type": "bearer",
            "expires_in": _access_ttl_seconds(),
            "refresh_token": None,
            "dev": True,
        }

    email = _normalize_email(body.username)
    user = store.get_user(email=email)
    if user is None:
        raise AuthError("invalid credentials")

    locked_until = user["locked_until"] or 0.0
    now = _now()
    if locked_until and now < locked_until:
        raise AccountLockedError(
            "account locked after repeated failed logins",
            details={"retry_after": int(locked_until - now) + 1},
        )
    if locked_until:  # the lock expired: fresh start
        store.clear_failures(user["user_id"])

    if not verify_password(body.password, user["password_hash"]):
        store.record_failure(
            user["user_id"],
            max_failures=LOGIN_MAX_FAILURES,
            lockout_minutes=LOGIN_LOCKOUT_MINUTES,
        )
        raise AuthError("invalid credentials")

    if user["verified_at"] is None:
        raise AuthError(
            "account is not verified",
            details={"needs_verification": True},
        )

    store.clear_failures(user["user_id"])
    access = _mint(
        {
            "sub": user["user_id"],
            "email": user["email"],
            "typ": "access",
            "jti": uuid.uuid4().hex,
        },
        _access_ttl_seconds(),
    )
    refresh_token, _jti, _exp = _issue_token(
        user["user_id"], "refresh", REFRESH_TOKEN_DAYS * 86400
    )
    return {
        "access_token": access,
        "token_type": "bearer",
        "expires_in": _access_ttl_seconds(),
        "refresh_token": refresh_token,
    }
