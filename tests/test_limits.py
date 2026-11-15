"""M33 plans and limits: per-day metering, plan tokens, honest 402s.

The contracts locked here: 0 means unmetered (the dummy deployment's
default), over-limit is a 402 naming the plan and the limit (never a
silent failure), signed-out use shares the anonymous free-tier scope
while signed-in use is scoped per user, trialing/active/past_due carry
the paid tables (past_due *is* the grace period), the no-model refusal
stays free, and a plan token verifies offline with no database.
"""

from __future__ import annotations

import json
import uuid
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from src.api import accounts, billing, limits, routes as routes_module
from src.core.errors import AuthError, PlanLimitError
from src.db.database import init_db
from src.main import app
from tests.pnggen import png_bytes as _png_bytes

TEST_SECRET = "limits-test-secret-0123456789abcdef"
PASSWORD = "correct horse battery staple"
T2 = 1_802_592_000


@pytest.fixture()
def db(tmp_path) -> Path:
    path = tmp_path / "limits-test.db"
    init_db(path)
    return path


@pytest.fixture()
def client(db: Path, monkeypatch) -> TestClient:
    monkeypatch.setattr(accounts.store, "db_path", str(db))
    monkeypatch.setattr(billing.store, "db_path", str(db))
    monkeypatch.setattr(limits.store, "db_path", str(db))
    monkeypatch.setattr(accounts, "AUTH_ENABLED", True)
    monkeypatch.setattr(accounts, "JWT_SECRET", TEST_SECRET)
    with TestClient(app) as test_client:
        yield test_client


def _signup_login(client: TestClient) -> tuple[dict[str, str], str]:
    email = f"limits-{uuid.uuid4().hex[:10]}@example.com"
    r = client.post(
        "/api/accounts/signup", json={"email": email, "password": PASSWORD}
    )
    assert r.status_code == 200, r.text
    client.post(
        "/api/accounts/verify",
        json={"token": r.json()["verification"]["token"]},
    )
    r = client.post(
        "/api/login", json={"username": email, "password": PASSWORD}
    )
    assert r.status_code == 200, r.text
    headers = {"Authorization": f"Bearer {r.json()['access_token']}"}
    user_id = client.get("/api/accounts/me", headers=headers).json()["user_id"]
    return headers, user_id


def _sse_events(text: str) -> list[dict]:
    events = []
    for line in text.splitlines():
        if line.startswith("data: "):
            events.append(json.loads(line[6:]))
    return events


# -- the metering primitive ------------------------------------------------


def test_dummy_deployment_ships_unmetered():
    """The committed .env.dummy sets every cap to 0 = unlimited."""
    assert limits.FREE_LIMITS == {
        "llm_calls": 0,
        "exports": 0,
        "saved_designs": 0,
    }
    assert limits.PAID_LIMITS == {
        "llm_calls": 0,
        "exports": 0,
        "saved_designs": 0,
    }


def test_zero_limit_is_unmetered_and_writes_nothing(client, monkeypatch):
    monkeypatch.setitem(limits.FREE_LIMITS, "llm_calls", 0)
    assert limits.check_and_increment("llm_calls", None) == 0
    assert limits.check_and_increment("llm_calls", "someone") == 0
    # Unlimited tiers are never metered: no counter row appears.
    assert limits.store.count("llm_calls", "anonymous") == 0
    assert limits.store.count("llm_calls", "someone") == 0


def test_free_limit_raises_naming_plan_and_limit(client, monkeypatch):
    monkeypatch.setitem(limits.FREE_LIMITS, "llm_calls", 2)
    assert limits.check_and_increment("llm_calls", None) == 1
    assert limits.check_and_increment("llm_calls", None) == 2
    with pytest.raises(PlanLimitError) as excinfo:
        limits.check_and_increment("llm_calls", None)
    message = str(excinfo.value)
    assert "free" in message  # the plan is named
    assert "2" in message  # the limit is named
    payload = excinfo.value.to_dict()
    assert payload["code"] == "plan_limit_exceeded"
    assert payload["details"]["metric"] == "llm_calls"
    assert payload["details"]["limit"] == 2
    assert payload["details"]["plan"] == "free"
    assert payload["details"]["used"] == 3


def test_scopes_are_isolated_per_user_and_shared_signed_out(
    client, monkeypatch
):
    monkeypatch.setitem(limits.FREE_LIMITS, "exports", 1)
    assert limits.check_and_increment("exports", None) == 1
    # The anonymous free-tier scope is shared across signed-out use.
    with pytest.raises(PlanLimitError):
        limits.check_and_increment("exports", None)
    # A signed-in user has their own scope, untouched by the shared one.
    assert limits.check_and_increment("exports", "user-a") == 1
    with pytest.raises(PlanLimitError):
        limits.check_and_increment("exports", "user-a")
    assert limits.check_and_increment("exports", "user-b") == 1


def _real_user(client: TestClient, label: str) -> str:
    """A real users row (subscriptions FK-reference real users)."""
    return accounts.store.create_user(
        email=f"{label}-{uuid.uuid4().hex[:8]}@example.com",
        password_hash="not-a-real-hash",
        policy_version="test",
    )["user_id"]


@pytest.mark.parametrize("state", ["trialing", "active", "past_due"])
def test_paid_states_carry_the_paid_table(
    client, monkeypatch, state: str
):
    user_id = _real_user(client, f"paid-{state}")
    billing.store.create_subscription(
        user_id=user_id,
        plan_id="plan-x",
        state=state,
        period_end=T2,
    )
    monkeypatch.setitem(limits.PAID_LIMITS, "llm_calls", 1)
    monkeypatch.setitem(limits.FREE_LIMITS, "llm_calls", 99)
    assert limits.check_and_increment("llm_calls", user_id) == 1
    with pytest.raises(PlanLimitError) as excinfo:
        limits.check_and_increment("llm_calls", user_id)
    assert "paid" in str(excinfo.value)


@pytest.mark.parametrize("state", ["cancelled", "expired", None])
def test_terminal_and_absent_subscriptions_are_free_tier(
    client, monkeypatch, state: str | None
):
    user_id = _real_user(client, f"gone-{state}")
    if state is not None:
        billing.store.create_subscription(
            user_id=user_id,
            plan_id="plan-x",
            state=state,
            period_end=T2,
        )
    monkeypatch.setitem(limits.FREE_LIMITS, "llm_calls", 1)
    monkeypatch.setitem(limits.PAID_LIMITS, "llm_calls", 99)
    assert limits.check_and_increment("llm_calls", user_id) == 1
    with pytest.raises(PlanLimitError) as excinfo:
        limits.check_and_increment("llm_calls", user_id)
    assert "free" in str(excinfo.value)


def test_resolve_tier_signed_out_is_free(client):
    assert billing.resolve_tier(None) == ("free", None, None)


# -- route-level enforcement ------------------------------------------------


def test_downloads_meter_and_402_past_the_cap(client, monkeypatch):
    monkeypatch.setitem(limits.FREE_LIMITS, "exports", 1)
    upload = client.post(
        "/api/uploads",
        files={"file": ("board.png", _png_bytes(), "image/png")},
    )
    assert upload.status_code == 200, upload.text
    artifact_id = upload.json()["artifact_id"]

    first = client.get(f"/api/artifacts/{artifact_id}")
    assert first.status_code == 200  # signed-out use works under the cap
    second = client.get(f"/api/artifacts/{artifact_id}")
    assert second.status_code == 402
    assert second.json()["errors"][0]["code"] == "plan_limit_exceeded"


def test_stream_plan_meters_model_backed_calls_via_sse(client, monkeypatch):
    class FakeLLM:
        def available(self):
            return True

        def stream(self, prompt, system=None):
            yield "I "
            yield "will plan."

    monkeypatch.setattr(routes_module, "_local_llm", lambda: FakeLLM())
    monkeypatch.setitem(limits.FREE_LIMITS, "llm_calls", 1)

    first = client.post(
        "/api/intelligence/stream-plan", json={"text": "do something"}
    )
    events = _sse_events(first.text)
    assert events
    assert events[-1]["type"] == "error"  # non-JSON reply → plan_invalid
    assert events[-1]["message"]["code"] == "plan_invalid"

    # The second model-backed call is over the cap: the honest SSE
    # error frame names the plan and the limit.
    second = client.post(
        "/api/intelligence/stream-plan", json={"text": "do something"}
    )
    events = _sse_events(second.text)
    assert events[-1]["type"] == "error"
    assert events[-1]["message"]["code"] == "plan_limit_exceeded"


def test_agent_run_is_402_past_the_cap_and_refusals_stay_free(
    client, monkeypatch
):
    class AvailableLLM:
        def available(self):
            return True

    monkeypatch.setattr(
        routes_module, "_local_llm", lambda: AvailableLLM()
    )
    monkeypatch.setitem(limits.FREE_LIMITS, "llm_calls", 1)
    headers, user_id = _signup_login(client)

    # Fill the cap, then a model-backed run is refused with 402. The
    # refused attempt is itself metered (count increments before the
    # check), so the counter reads 2 afterwards.
    assert limits.store.increment("llm_calls", user_id) == 1
    r = client.post(
        "/api/agents/coder/run",
        headers=headers,
        json={"prompt": "do a thing"},
    )
    assert r.status_code == 402
    assert r.json()["errors"][0]["code"] == "plan_limit_exceeded"
    assert limits.store.count("llm_calls", user_id) == 2

    # No model attached: the refusal is free, even at the cap.
    class UnavailableLLM:
        def available(self):
            return False

    monkeypatch.setattr(
        routes_module, "_local_llm", lambda: UnavailableLLM()
    )
    r = client.post(
        "/api/agents/coder/run",
        headers=headers,
        json={"prompt": "do a thing"},
    )
    assert r.status_code == 200
    assert limits.store.count("llm_calls", user_id) == 2


# -- the offline plan token --------------------------------------------------


def test_plan_token_round_trips_and_verifies_offline(
    client, tmp_path, monkeypatch
):
    headers, user_id = _signup_login(client)
    r = client.get("/api/billing/plan-token", headers=headers)
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["tier"] == "free"
    assert body["offline_days"] == billing.TRINITY_PLAN_TOKEN_DAYS

    # Offline verify: signature + expiry only, no database contact.
    monkeypatch.setattr(
        billing.store, "db_path", str(tmp_path / "does-not-exist.db")
    )
    payload = billing.verify_plan_token(body["plan_token"])
    assert payload["typ"] == "plan"
    assert payload["sub"] == user_id
    assert payload["tier"] == "free"
    assert payload["plan_id"] is None


def test_plan_token_carries_a_paid_subscription(client):
    headers, _user_id = _signup_login(client)
    billing.store.create_subscription(
        user_id=client.get("/api/accounts/me", headers=headers).json()[
            "user_id"
        ],
        plan_id="plan-pro",
        state="active",
        period_end=T2,
    )
    body = client.get(
        "/api/billing/plan-token", headers=headers
    ).json()
    assert body["tier"] == "paid"
    assert body["state"] == "active"
    assert body["plan_id"] == "plan-pro"
    payload = billing.verify_plan_token(body["plan_token"])
    assert payload["tier"] == "paid"
    assert payload["plan_id"] == "plan-pro"


def test_tampered_plan_token_is_refused(client):
    headers, _user_id = _signup_login(client)
    token = client.get("/api/billing/plan-token", headers=headers).json()[
        "plan_token"
    ]
    tampered = token[:-2] + ("AA" if token[-2:] != "AA" else "BB")
    with pytest.raises(AuthError):
        billing.verify_plan_token(tampered)


def test_plan_token_expires_after_the_grace_window(client, monkeypatch):
    headers, _user_id = _signup_login(client)
    monkeypatch.setattr(billing, "TRINITY_PLAN_TOKEN_DAYS", -1)
    token = client.get("/api/billing/plan-token", headers=headers).json()[
        "plan_token"
    ]
    with pytest.raises(AuthError) as excinfo:
        billing.verify_plan_token(token)
    assert "expired" in str(excinfo.value)
