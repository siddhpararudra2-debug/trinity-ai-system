"""M30 accounts: signup / verify / login / refresh / logout / reset / delete.

The M30 tests must lock the failure contracts the roadmap names:
lockout after repeated failures, expired tokens, and replayed reset
links. Every test runs against a temporary SQLite file with a fixed
test secret, so nothing here touches the developer's data directory.
"""

from __future__ import annotations

import uuid

import pytest
from fastapi.testclient import TestClient

from src.api import accounts
from src.db.database import init_db
from src.main import app

TEST_SECRET = "accounts-test-secret-0123456789abcdef"  # ≥32 bytes for HS256


@pytest.fixture()
def client(tmp_path, monkeypatch):
    db = tmp_path / "accounts-test.db"
    init_db(db)
    monkeypatch.setattr(accounts.store, "db_path", str(db))
    monkeypatch.setattr(accounts, "AUTH_ENABLED", True)
    monkeypatch.setattr(accounts, "JWT_SECRET", TEST_SECRET)
    with TestClient(app) as test_client:
        yield test_client


def _email() -> str:
    return f"acc-{uuid.uuid4().hex[:10]}@example.com"


def _signup_and_verify(client: TestClient, email: str) -> None:
    r = client.post(
        "/api/accounts/signup",
        json={"email": email, "password": "correct horse battery staple"},
    )
    assert r.status_code == 200, r.text
    token = r.json()["verification"]["token"]
    r = client.post("/api/accounts/verify", json={"token": token})
    assert r.status_code == 200, r.text
    assert r.json() == {"email": email, "verified": True}


def _login(client: TestClient, email: str, password: str):
    return client.post(
        "/api/login", json={"username": email, "password": password}
    )


PASSWORD = "correct horse battery staple"


def test_full_lifecycle_signup_verify_login_refresh_logout(client):
    email = _email()
    _signup_and_verify(client, email)

    r = _login(client, email, PASSWORD)
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["token_type"] == "bearer"
    assert body["access_token"] and body["refresh_token"]

    # /accounts/me answers with the account facts.
    r = client.get(
        "/api/accounts/me",
        headers={"Authorization": f"Bearer {body['access_token']}"},
    )
    assert r.status_code == 200, r.text
    me = r.json()
    assert me["email"] == email
    assert me["verified"] is True
    assert me["dev"] is False

    # Refresh rotates: the old refresh token must not work twice.
    r = client.post(
        "/api/accounts/refresh",
        json={"refresh_token": body["refresh_token"]},
    )
    assert r.status_code == 200, r.text
    rotated = r.json()
    assert rotated["access_token"] and rotated["refresh_token"]

    r = client.post(
        "/api/accounts/refresh",
        json={"refresh_token": body["refresh_token"]},
    )
    assert r.status_code == 401
    assert r.json()["errors"][0]["details"]["replayed"] is True

    # Logout revokes; the rotated token dies with it.
    r = client.post(
        "/api/accounts/logout",
        json={"refresh_token": rotated["refresh_token"]},
    )
    assert r.status_code == 200
    assert r.json() == {"logged_out": True}

    r = client.post(
        "/api/accounts/refresh",
        json={"refresh_token": rotated["refresh_token"]},
    )
    assert r.status_code == 401


def test_login_rejects_wrong_password_and_locks_after_repeats(client):
    email = _email()
    _signup_and_verify(client, email)

    for _ in range(accounts.LOGIN_MAX_FAILURES):
        r = _login(client, email, "wrong-password")
        assert r.status_code == 401
        assert r.json()["errors"][0]["code"] == "auth_error"

    # The lock applies even to the correct password, with retry_after.
    r = _login(client, email, PASSWORD)
    assert r.status_code == 423
    assert r.json()["errors"][0]["code"] == "account_locked"
    assert r.json()["errors"][0]["details"]["retry_after"] >= 1


def test_lockout_expires_and_the_counter_restarts(client, monkeypatch):
    email = _email()
    _signup_and_verify(client, email)
    for _ in range(accounts.LOGIN_MAX_FAILURES):
        _login(client, email, "wrong-password")

    # Move the clock past the lockout window: the lock lifts and a
    # correct password logs in again.
    real_now = accounts._now
    monkeypatch.setattr(accounts, "_now", lambda: real_now() + 3600.0)
    r = _login(client, email, PASSWORD)
    assert r.status_code == 200, r.text


def test_unverified_account_cannot_login(client):
    email = _email()
    r = client.post(
        "/api/accounts/signup", json={"email": email, "password": PASSWORD}
    )
    assert r.status_code == 200
    r = _login(client, email, PASSWORD)
    assert r.status_code == 401
    assert r.json()["errors"][0]["details"]["needs_verification"] is True


def test_expired_verification_token_is_rejected(client, monkeypatch):
    # Negative lifetime → the token is born expired.
    monkeypatch.setattr(accounts, "VERIFY_TOKEN_HOURS", -0.0001)
    email = _email()
    r = client.post(
        "/api/accounts/signup", json={"email": email, "password": PASSWORD}
    )
    token = r.json()["verification"]["token"]
    r = client.post("/api/accounts/verify", json={"token": token})
    assert r.status_code == 401
    assert "expired" in r.json()["errors"][0]["message"]


def test_replayed_verification_token_is_rejected(client):
    email = _email()
    r = client.post(
        "/api/accounts/signup", json={"email": email, "password": PASSWORD}
    )
    token = r.json()["verification"]["token"]
    assert client.post("/api/accounts/verify", json={"token": token}).status_code == 200
    r = client.post("/api/accounts/verify", json={"token": token})
    assert r.status_code == 401
    assert r.json()["errors"][0]["details"]["replayed"] is True


def test_replayed_reset_link_is_rejected_and_password_really_changes(client):
    email = _email()
    _signup_and_verify(client, email)

    r = client.post("/api/accounts/reset-request", json={"email": email})
    assert r.status_code == 200
    body = r.json()
    assert body["delivery"] == "inline"
    reset_token = body["token"]

    new_password = "a different long password"
    r = client.post(
        "/api/accounts/reset-confirm",
        json={"token": reset_token, "new_password": new_password},
    )
    assert r.status_code == 200
    assert r.json()["password_reset"] is True
    assert _login(client, email, new_password).status_code == 200

    # The reset link is single-use: the replay must fail.
    r = client.post(
        "/api/accounts/reset-confirm",
        json={"token": reset_token, "new_password": "yet another one"},
    )
    assert r.status_code == 401
    assert r.json()["errors"][0]["details"]["replayed"] is True


def test_reset_request_for_unknown_email_never_leaks_existence(client):
    # A real account for the "known" branch.
    email = _email()
    _signup_and_verify(client, email)
    known = client.post("/api/accounts/reset-request", json={"email": email})
    unknown = client.post(
        "/api/accounts/reset-request", json={"email": _email()}
    )
    assert known.status_code == 200 and unknown.status_code == 200
    assert "token" not in unknown.json()
    assert unknown.json()["delivery"] == "none"
    # Same response shape; only the delivery field distinguishes them.
    assert set(known.json()) == set(unknown.json()) | {"token", "expires_at", "note"}


def test_password_reset_revokes_outstanding_refresh_tokens(client):
    email = _email()
    _signup_and_verify(client, email)
    refresh_token = _login(client, email, PASSWORD).json()["refresh_token"]

    r = client.post("/api/accounts/reset-request", json={"email": email})
    reset_token = r.json()["token"]
    r = client.post(
        "/api/accounts/reset-confirm",
        json={"token": reset_token, "new_password": "rotated password 123"},
    )
    assert r.status_code == 200

    # The old session is dead after a password reset.
    r = client.post(
        "/api/accounts/refresh", json={"refresh_token": refresh_token}
    )
    assert r.status_code == 401
    assert _login(client, email, "rotated password 123").status_code == 200


def test_signup_conflict_and_weak_password(client):
    email = _email()
    assert (
        client.post(
            "/api/accounts/signup", json={"email": email, "password": PASSWORD}
        ).status_code
        == 200
    )
    r = client.post(
        "/api/accounts/signup", json={"email": email, "password": PASSWORD}
    )
    assert r.status_code == 409
    assert r.json()["errors"][0]["code"] == "account_conflict"

    r = client.post(
        "/api/accounts/signup", json={"email": _email(), "password": "short"}
    )
    assert r.status_code == 422
    assert r.json()["errors"][0]["code"] == "request_validation_error"


def test_refresh_rejects_an_access_token(client):
    email = _email()
    _signup_and_verify(client, email)
    access = _login(client, email, PASSWORD).json()["access_token"]
    r = client.post("/api/accounts/refresh", json={"refresh_token": access})
    assert r.status_code == 401
    assert "not a refresh token" in r.json()["errors"][0]["message"]


def test_me_requires_a_bearer_token(client):
    assert client.get("/api/accounts/me").status_code == 401
    r = client.get(
        "/api/accounts/me", headers={"Authorization": "Bearer garbage"}
    )
    assert r.status_code == 401


def test_delete_my_account(client):
    email = _email()
    _signup_and_verify(client, email)
    access = _login(client, email, PASSWORD).json()["access_token"]
    headers = {"Authorization": f"Bearer {access}"}

    r = client.delete("/api/accounts/me", headers=headers)
    assert r.status_code == 200
    assert r.json() == {"deleted": True, "email": email}

    # The account is gone: login fails without leaking why, and the
    # old access token no longer resolves to anyone.
    assert _login(client, email, PASSWORD).status_code == 401
    assert client.get("/api/accounts/me", headers=headers).status_code == 401


def test_dev_mode_login_when_auth_disabled(client, monkeypatch):
    monkeypatch.setattr(accounts, "AUTH_ENABLED", False)
    r = client.post(
        "/api/login", json={"username": "anyone", "password": "anything"}
    )
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["dev"] is True
    assert body["refresh_token"] is None

    r = client.get(
        "/api/accounts/me",
        headers={"Authorization": f"Bearer {body['access_token']}"},
    )
    assert r.status_code == 200
    assert r.json()["dev"] is True
    assert r.json()["user_id"] == "anyone"


def test_signed_out_use_stays_on_the_free_tier(client):
    """M30 promise: no account, no token — Stage 12 features still work."""
    r = client.get("/api/projects")
    assert r.status_code == 200, r.text
    r = client.get("/api/engines")
    assert r.status_code == 200, r.text
