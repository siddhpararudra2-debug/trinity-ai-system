"""M32 billing: plans from config, checkout, and the webhook state machine.

The roadmap names the failure contracts to lock: duplicate deliveries,
out-of-order charges, bad signatures. Sample webhooks follow Razorpay's
payload shape (event, event_id, payload.<x>.entity) and are signed with
the deployment's webhook secret; every test runs against a temporary
SQLite file so nothing here touches the developer's data directory.
"""

from __future__ import annotations

import hashlib
import hmac
import json
import sqlite3
import uuid
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from src.api import accounts, billing
from src.db.database import init_db
from src.main import app

TEST_SECRET = "accounts-test-secret-0123456789abcdef"
WEBHOOK_SECRET = "whsec-test-0123456789abcdef"

PASSWORD = "correct horse battery staple"

# Fixed epoch seconds for period ends: T1 < T2 on purpose.
T1 = 1_800_000_000
T2 = 1_802_592_000  # T1 + 30 days


@pytest.fixture()
def db(tmp_path) -> Path:
    path = tmp_path / "billing-test.db"
    init_db(path)
    return path


@pytest.fixture()
def client(db: Path, monkeypatch) -> TestClient:
    monkeypatch.setattr(accounts.store, "db_path", str(db))
    monkeypatch.setattr(billing.store, "db_path", str(db))
    monkeypatch.setattr(accounts, "AUTH_ENABLED", True)
    monkeypatch.setattr(accounts, "JWT_SECRET", TEST_SECRET)
    monkeypatch.setattr(billing, "RAZORPAY_WEBHOOK_SECRET", WEBHOOK_SECRET)
    with TestClient(app) as test_client:
        yield test_client


def _email() -> str:
    return f"bill-{uuid.uuid4().hex[:10]}@example.com"


def _signup_login(client: TestClient) -> tuple[dict[str, str], str]:
    email = _email()
    r = client.post(
        "/api/accounts/signup", json={"email": email, "password": PASSWORD}
    )
    assert r.status_code == 200, r.text
    client.post(
        "/api/accounts/verify", json={"token": r.json()["verification"]["token"]}
    )
    r = client.post(
        "/api/login", json={"username": email, "password": PASSWORD}
    )
    assert r.status_code == 200, r.text
    headers = {"Authorization": f"Bearer {r.json()['access_token']}"}
    user_id = client.get("/api/accounts/me", headers=headers).json()["user_id"]
    return headers, user_id


def _webhook(
    client: TestClient,
    event: dict,
    *,
    secret: str = WEBHOOK_SECRET,
    signature: str | None = None,
):
    raw = json.dumps(event).encode()
    if signature is None:
        signature = hmac.new(secret.encode(), raw, hashlib.sha256).hexdigest()
    return client.post(
        "/api/billing/webhook",
        content=raw,
        headers={"X-Razorpay-Signature": signature},
    )


def _sub_event(
    event: str,
    *,
    event_id: str,
    status: str,
    user_id: str | None = None,
    sub_id: str = "sub_test1",
    current_end: int | None = None,
    plan_id: str | None = None,
) -> dict:
    entity: dict = {"id": sub_id, "status": status}
    if user_id is not None:
        entity["notes"] = {"user_id": user_id}
    if current_end is not None:
        entity["current_end"] = current_end
    if plan_id is not None:
        entity["plan_id"] = plan_id
    return {
        "event": event,
        "event_id": event_id,
        "payload": {"subscription": {"entity": entity}},
    }


def _pay_event(
    event: str,
    *,
    event_id: str,
    sub_id: str,
    user_id: str | None = None,
) -> dict:
    entity: dict = {"id": f"pay_{event_id}", "subscription_id": sub_id}
    if user_id is not None:
        entity["notes"] = {"user_id": user_id}
    return {
        "event": event,
        "event_id": event_id,
        "payload": {"payment": {"entity": entity}},
    }


# -- plans ---------------------------------------------------------------


def test_plans_come_from_config_with_gst_breakdown(client):
    r = client.get("/api/billing/plans")
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["provider"] == "razorpay"
    assert body["key_id"] == billing.RAZORPAY_KEY_ID
    assert body["gst"]["rate"] == billing.RAZORPAY_GST_RATE
    assert (
        body["gst"]["cgst_rate"] + body["gst"]["sgst_rate"]
        == body["gst"]["rate"]
    )

    by_id = {p["plan_id"]: p for p in body["plans"]}
    assert set(by_id) == {
        billing.RAZORPAY_PLAN_TRIAL,
        billing.RAZORPAY_PLAN_MONTHLY,
        billing.RAZORPAY_PLAN_ANNUAL,
    }
    monthly = by_id[billing.RAZORPAY_PLAN_MONTHLY]
    assert monthly["price_inr"] == billing.RAZORPAY_PLAN_MONTHLY_PRICE_INR
    assert monthly["gst_inr"] == round(
        monthly["price_inr"] * billing.RAZORPAY_GST_RATE / 100
    )
    assert (
        monthly["cgst_inr"] + monthly["sgst_inr"] == monthly["gst_inr"]
    )
    assert monthly["total_inr"] == monthly["price_inr"] + monthly["gst_inr"]
    assert by_id[billing.RAZORPAY_PLAN_TRIAL]["trial"] is True


# -- checkout ------------------------------------------------------------


def test_trial_checkout_activates_a_trialing_subscription(client):
    headers, _user_id = _signup_login(client)

    r = client.post(
        "/api/billing/checkout",
        json={"plan_id": billing.RAZORPAY_PLAN_TRIAL},
    )
    assert r.status_code == 401  # checkout is for signed-in users

    r = client.post(
        "/api/billing/checkout",
        headers=headers,
        json={"plan_id": billing.RAZORPAY_PLAN_TRIAL},
    )
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["mode"] == "activated"
    assert body["subscription"]["state"] == "trialing"
    assert body["subscription"]["current_period_end"]
    assert body["refund_policy"] == billing.TRINITY_REFUND_CANCELLATION

    r = client.get("/api/billing/subscription", headers=headers)
    assert r.status_code == 200
    assert r.json()["tier"] == "trial"
    assert r.json()["subscription"]["state"] == "trialing"


def test_unknown_plan_is_refused_with_the_known_catalog(client):
    headers, _user_id = _signup_login(client)
    r = client.post(
        "/api/billing/checkout", headers=headers, json={"plan_id": "nope"}
    )
    assert r.status_code == 422
    assert billing.RAZORPAY_PLAN_MONTHLY in r.json()["errors"][0]["details"][
        "known"
    ]


def test_paid_checkout_is_an_inline_order_intent_without_api_calls(client):
    headers, _user_id = _signup_login(client)
    r = client.post(
        "/api/billing/checkout",
        headers=headers,
        json={"plan_id": billing.RAZORPAY_PLAN_MONTHLY},
    )
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["mode"] == "inline"
    assert body["pricing"]["total_inr"] == (
        body["pricing"]["price_inr"] + body["pricing"]["gst_inr"]
    )
    assert "no Razorpay API call" in body["note"]

    # Nothing was created: the signed webhook writes paid subscriptions.
    r = client.get("/api/billing/subscription", headers=headers)
    assert r.json() == {"subscription": None, "tier": "free"}


# -- webhook auth + dedupe ------------------------------------------------


def test_webhook_rejects_a_bad_signature(client, db):
    _headers, user_id = _signup_login(client)
    event = _sub_event(
        "subscription.created",
        event_id="evt_bad_sig",
        status="created",
        user_id=user_id,
        current_end=T2,
    )
    raw = json.dumps(event).encode()
    forged = hmac.new(b"not-the-secret", raw, hashlib.sha256).hexdigest()
    r = client.post(
        "/api/billing/webhook",
        content=raw,
        headers={"X-Razorpay-Signature": forged},
    )
    assert r.status_code == 400
    assert r.json()["errors"][0]["code"] == "webhook_signature"

    # Nothing was recorded by the forged attempt: the same event, now
    # correctly signed, applies as new rather than as a duplicate.
    r = _webhook(client, event)
    assert r.status_code == 200
    assert r.json().get("duplicate") is not True
    assert r.json()["applied"] is True

    with sqlite3.connect(db) as conn:
        (count,) = conn.execute(
            "SELECT COUNT(*) FROM subscriptions"
        ).fetchone()
    assert count == 1


def test_duplicate_webhook_delivery_is_acked_and_never_reapplied(client, db):
    _headers, user_id = _signup_login(client)
    event = _sub_event(
        "subscription.created",
        event_id="evt_dup",
        status="created",
        user_id=user_id,
        current_end=T2,
        plan_id=billing.RAZORPAY_PLAN_TRIAL,
    )
    r = _webhook(client, event)
    assert r.status_code == 200
    assert r.json()["applied"] is True and r.json()["created"] is True

    r = _webhook(client, event)
    assert r.status_code == 200
    assert r.json() == {"duplicate": True, "event_id": "evt_dup"}

    with sqlite3.connect(db) as conn:
        (count,) = conn.execute(
            "SELECT COUNT(*) FROM subscriptions"
        ).fetchone()
    assert count == 1


# -- the state machine ----------------------------------------------------


def test_paid_webhooks_create_and_link_the_subscription(client):
    headers, user_id = _signup_login(client)
    r = _webhook(
        client,
        _sub_event(
            "subscription.created",
            event_id="evt_c1",
            status="created",
            user_id=user_id,
            current_end=T2,
            plan_id=billing.RAZORPAY_PLAN_MONTHLY,
        ),
    )
    assert r.status_code == 200 and r.json()["created"] is True

    r = client.get("/api/billing/subscription", headers=headers)
    body = r.json()
    assert body["tier"] == "trial"
    assert body["subscription"]["plan_id"] == billing.RAZORPAY_PLAN_MONTHLY
    assert body["subscription"]["provider"] == "razorpay"


def test_out_of_order_charges_never_regress_the_period(client):
    _headers, user_id = _signup_login(client)
    _webhook(
        client,
        _sub_event(
            "subscription.created",
            event_id="evt_o1",
            status="created",
            user_id=user_id,
        ),
    )
    r = _webhook(
        client,
        _sub_event(
            "subscription.charged",
            event_id="evt_o2",
            status="charged",
            user_id=user_id,
            current_end=T2,
        ),
    )
    assert r.json()["applied"] is True
    assert r.json()["subscription"]["state"] == "active"

    # An older delivery (T1 < T2) must not move the period backwards.
    r = _webhook(
        client,
        _sub_event(
            "subscription.charged",
            event_id="evt_o3",
            status="charged",
            user_id=user_id,
            current_end=T1,
        ),
    )
    assert r.status_code == 200
    assert r.json()["applied"] is False
    assert "out-of-order" in r.json()["reason"]
    assert r.json()["subscription"]["current_period_end"] == billing._iso(T2)


def test_state_machine_walks_the_roadmap_states(client):
    _headers, user_id = _signup_login(client)

    # created → trialing
    r = _webhook(
        client,
        _sub_event(
            "subscription.created",
            event_id="evt_s1",
            status="created",
            user_id=user_id,
        ),
    )
    assert r.json()["subscription"]["state"] == "trialing"

    # activated → active
    r = _webhook(
        client,
        _sub_event(
            "subscription.activated",
            event_id="evt_s2",
            status="active",
            user_id=user_id,
            current_end=T2,
        ),
    )
    assert r.json()["subscription"]["state"] == "active"

    # payment.failed → past_due
    r = _webhook(
        client,
        _pay_event(
            "payment.failed", event_id="evt_s3", sub_id="sub_test1"
        ),
    )
    assert r.json()["applied"] is True
    assert r.json()["subscription"]["state"] == "past_due"

    # a successful charge recovers → active (a new period, T3 > T2)
    r = _webhook(
        client,
        _sub_event(
            "subscription.charged",
            event_id="evt_s4",
            status="charged",
            user_id=user_id,
            current_end=1_805_184_000,
        ),
    )
    assert r.json()["subscription"]["state"] == "active"

    # cancelled → cancelled, access_until survives
    r = _webhook(
        client,
        _sub_event(
            "subscription.cancelled",
            event_id="evt_s5",
            status="cancelled",
            user_id=user_id,
        ),
    )
    assert r.json()["subscription"]["state"] == "cancelled"
    assert r.json()["subscription"]["cancelled_at"]

    # terminal protection: expired cannot rewrite a cancelled row
    r = _webhook(
        client,
        _sub_event(
            "subscription.expired",
            event_id="evt_s6",
            status="expired",
            user_id=user_id,
        ),
    )
    assert r.json()["applied"] is False
    assert r.json()["subscription"]["state"] == "cancelled"


def test_expired_and_refunded_follow_their_webhooks(client):
    _headers, user_id = _signup_login(client)
    _webhook(
        client,
        _sub_event(
            "subscription.created",
            event_id="evt_x1",
            status="created",
            user_id=user_id,
        ),
    )
    r = _webhook(
        client,
        _sub_event(
            "subscription.expired",
            event_id="evt_x2",
            status="expired",
            user_id=user_id,
        ),
    )
    assert r.json()["applied"] is True
    assert r.json()["subscription"]["state"] == "expired"

    r = _webhook(
        client,
        _pay_event(
            "refund.processed", event_id="evt_x3", sub_id="sub_test1"
        ),
    )
    assert r.json()["applied"] is True
    body = r.json()["subscription"]
    assert body["state"] == "cancelled"
    assert body["refunded_at"]


def test_states_are_exactly_the_roadmap_five(client):
    assert billing.STATES == (
        "trialing",
        "active",
        "past_due",
        "cancelled",
        "expired",
    )
    _headers, user_id = _signup_login(client)
    _webhook(
        client,
        _sub_event(
            "subscription.created",
            event_id="evt_u1",
            status="created",
            user_id=user_id,
        ),
    )
    # An unmapped provider status changes nothing.
    r = _webhook(
        client,
        _sub_event(
            "subscription.activated",
            event_id="evt_u2",
            status="paused",
            user_id=user_id,
        ),
    )
    assert r.json()["applied"] is False
    assert "unknown subscription status" in r.json()["reason"]


# -- cancel follows the refund page ---------------------------------------


def test_cancel_follows_the_refund_page(client):
    headers, _user_id = _signup_login(client)
    r = client.post(
        "/api/billing/checkout",
        headers=headers,
        json={"plan_id": billing.RAZORPAY_PLAN_TRIAL},
    )
    period_end = r.json()["subscription"]["current_period_end"]

    r = client.post("/api/billing/cancel", headers=headers)
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["cancelled"] is True
    # The plan stays active until the end of the paid period.
    assert body["access_until"] == period_end
    assert body["policy"] == billing.TRINITY_REFUND_CANCELLATION
    assert body["subscription"]["state"] == "cancelled"

    # Cancelling again is refused honestly.
    r = client.post("/api/billing/cancel", headers=headers)
    assert r.status_code == 422


def test_account_deletion_removes_subscription_rows(client, db):
    headers, _user_id = _signup_login(client)
    r = client.post(
        "/api/billing/checkout",
        headers=headers,
        json={"plan_id": billing.RAZORPAY_PLAN_TRIAL},
    )
    assert r.status_code == 200

    r = client.delete("/api/accounts/me", headers=headers)
    assert r.status_code == 200

    with sqlite3.connect(db) as conn:
        (count,) = conn.execute(
            "SELECT COUNT(*) FROM subscriptions"
        ).fetchone()
    assert count == 0
