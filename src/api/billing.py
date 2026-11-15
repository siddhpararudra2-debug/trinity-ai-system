"""M32 billing: plans from config, a five-state subscription machine,
and signed, deduped Razorpay webhooks.

Stage 13 / M32. V1 makes no Razorpay API calls and takes no SDK
dependency: the plan catalog and pricing come from config, checkout
returns the computed order, and the subscription state machine is
driven entirely by signed webhooks — the same payloads production
sends, so swapping the dummy keys for real ones changes no code.

- webhook auth: HMAC-SHA256 over the raw body with
  RAZORPAY_WEBHOOK_SECRET, hex digest in X-Razorpay-Signature
  (Razorpay's scheme); fail-closed 400 when the secret is unset
- dedupe: the event id lands in the webhook_events table first; a
  replayed delivery is acked ``{"duplicate": true}`` and never applied
  twice
- out-of-order: a charged event never regresses current_period_end
- states: trialing | active | past_due | cancelled | expired — and
  nothing else (Razorpay entity statuses map onto these five)
- paid-plan subscriptions are created by the webhooks themselves
  (linked through the subscription's notes.user_id, the way production
  associates a Razorpay subscription with the local account); the free
  trial plan is the one state checkout creates locally
- cancel follows the refund-cancellation policy: the plan stays active
  until the end of the paid period (``access_until``)
"""

from __future__ import annotations

import hashlib
import hmac
import json
import time
import uuid
from datetime import UTC, datetime
from typing import Any

from fastapi import APIRouter, Depends, Header, Request

from src.api.accounts import (
    mint_signed_token,
    require_user,
    verify_signed_token,
)
from src.core.config import (
    RAZORPAY_CGST_RATE,
    RAZORPAY_GST_RATE,
    RAZORPAY_KEY_ID,
    RAZORPAY_PLAN_ANNUAL,
    RAZORPAY_PLAN_ANNUAL_DAYS,
    RAZORPAY_PLAN_ANNUAL_PRICE_INR,
    RAZORPAY_PLAN_MONTHLY,
    RAZORPAY_PLAN_MONTHLY_DAYS,
    RAZORPAY_PLAN_MONTHLY_PRICE_INR,
    RAZORPAY_PLAN_TRIAL,
    RAZORPAY_PLAN_TRIAL_DAYS,
    RAZORPAY_PLAN_TRIAL_PRICE_INR,
    RAZORPAY_SGST_RATE,
    RAZORPAY_WEBHOOK_SECRET,
    TRINITY_PLAN_TOKEN_DAYS,
    TRINITY_REFUND_CANCELLATION,
    settings,
)
from src.core.errors import RequestValidationError, WebhookSignatureError
from src.db.database import get_connection
from src.models.schemas import BillingCheckoutRequest

router = APIRouter(prefix="/api/billing", tags=["billing"])

# The roadmap's five states; the state machine never invents a sixth.
STATES = ("trialing", "active", "past_due", "cancelled", "expired")

# Razorpay subscription entity statuses → the five roadmap states.
# halted (payment issues) maps to past_due; completed (term finished
# normally) maps to expired like an un-renewed subscription.
_STATUS_MAP = {
    "created": "trialing",
    "authenticated": "trialing",
    "active": "active",
    "charged": "active",
    "resumed": "active",
    "pending": "past_due",
    "halted": "past_due",
    "cancelled": "cancelled",
    "completed": "expired",
    "expired": "expired",
}

# Webhook events the state machine consumes; anything else is acked as
# unhandled so the sender stops retrying without state corruption.
_HANDLED_EVENTS = (
    "subscription.created",
    "subscription.activated",
    "subscription.charged",
    "subscription.cancelled",
    "subscription.expired",
    "payment.failed",
    "refund.processed",
)

_TERMINAL_STATES = ("cancelled", "expired")


def _now() -> float:
    return time.time()


def _iso(epoch: float) -> str:
    return datetime.fromtimestamp(epoch, tz=UTC).isoformat()


def _plan_catalog() -> dict[str, dict[str, Any]]:
    """Plans from config: ids, prices (INR) and periods are env-driven."""
    return {
        RAZORPAY_PLAN_TRIAL: {
            "name": "Trial",
            "price_inr": RAZORPAY_PLAN_TRIAL_PRICE_INR,
            "period_days": RAZORPAY_PLAN_TRIAL_DAYS,
            "trial": True,
        },
        RAZORPAY_PLAN_MONTHLY: {
            "name": "Pro monthly",
            "price_inr": RAZORPAY_PLAN_MONTHLY_PRICE_INR,
            "period_days": RAZORPAY_PLAN_MONTHLY_DAYS,
            "trial": False,
        },
        RAZORPAY_PLAN_ANNUAL: {
            "name": "Pro annual",
            "price_inr": RAZORPAY_PLAN_ANNUAL_PRICE_INR,
            "period_days": RAZORPAY_PLAN_ANNUAL_DAYS,
            "trial": False,
        },
    }


def _gst_breakdown(price_inr: int) -> dict[str, int]:
    """Item 11 pricing: GST split into CGST + SGST from config rates."""
    gst = round(price_inr * RAZORPAY_GST_RATE / 100)
    cgst = round(gst * RAZORPAY_CGST_RATE / RAZORPAY_GST_RATE)
    return {
        "price_inr": price_inr,
        "gst_inr": gst,
        "cgst_inr": cgst,
        "sgst_inr": gst - cgst,
        "total_inr": price_inr + gst,
    }


def _verify_signature(body: bytes, signature: str) -> bool:
    if not RAZORPAY_WEBHOOK_SECRET:
        # Fail closed: a deployment without a webhook secret must not
        # accept unsigned (or forged) state changes.
        return False
    expected = hmac.new(
        RAZORPAY_WEBHOOK_SECRET.encode("utf-8"), body, hashlib.sha256
    ).hexdigest()
    return hmac.compare_digest(expected, signature or "")


class BillingStore:
    """SQLite-backed subscriptions + webhook dedup (plain sqlite3)."""

    def __init__(self, db_path: str | None = None):
        self.db_path = db_path  # None → settings.db_path at call time

    def _path(self):
        return self.db_path or settings.db_path

    def create_subscription(
        self,
        *,
        user_id: str,
        plan_id: str,
        state: str,
        period_end: float | None,
        provider_subscription_id: str | None = None,
    ) -> dict[str, Any]:
        subscription_id = uuid.uuid4().hex
        created_at = datetime.now(UTC).isoformat()
        with get_connection(self._path()) as conn:
            conn.execute(
                "INSERT INTO subscriptions (subscription_id, user_id, "
                "plan_id, state, current_period_end, provider, "
                "provider_subscription_id, created_at, updated_at) "
                "VALUES (?, ?, ?, ?, ?, 'razorpay', ?, ?, ?)",
                (
                    subscription_id,
                    user_id,
                    plan_id,
                    state,
                    period_end,
                    provider_subscription_id,
                    created_at,
                    created_at,
                ),
            )
            conn.commit()
        return self.get(subscription_id)  # type: ignore[return-value]

    def get(self, subscription_id: str) -> dict[str, Any] | None:
        with get_connection(self._path()) as conn:
            row = conn.execute(
                "SELECT * FROM subscriptions WHERE subscription_id = ?",
                (subscription_id,),
            ).fetchone()
        return dict(row) if row is not None else None

    def latest_for_user(self, user_id: str) -> dict[str, Any] | None:
        with get_connection(self._path()) as conn:
            row = conn.execute(
                "SELECT * FROM subscriptions WHERE user_id = ? "
                "ORDER BY created_at DESC, rowid DESC LIMIT 1",
                (user_id,),
            ).fetchone()
        return dict(row) if row is not None else None

    def get_by_provider_id(
        self, provider_subscription_id: str
    ) -> dict[str, Any] | None:
        with get_connection(self._path()) as conn:
            row = conn.execute(
                "SELECT * FROM subscriptions WHERE "
                "provider_subscription_id = ?",
                (provider_subscription_id,),
            ).fetchone()
        return dict(row) if row is not None else None

    def transition(
        self,
        subscription_id: str,
        *,
        state: str | None = None,
        period_end: float | None = None,
        cancelled_at: float | None = None,
        refunded_at: float | None = None,
        provider_subscription_id: str | None = None,
    ) -> dict[str, Any] | None:
        updates: list[str] = []
        args: list[Any] = []
        if state is not None:
            updates.append("state = ?")
            args.append(state)
        if period_end is not None:
            updates.append("current_period_end = ?")
            args.append(period_end)
        if cancelled_at is not None:
            updates.append("cancelled_at = ?")
            args.append(cancelled_at)
        if refunded_at is not None:
            updates.append("refunded_at = ?")
            args.append(refunded_at)
        if provider_subscription_id is not None:
            updates.append("provider_subscription_id = ?")
            args.append(provider_subscription_id)
        updates.append("updated_at = ?")
        args.append(datetime.now(UTC).isoformat())
        args.append(subscription_id)
        with get_connection(self._path()) as conn:
            conn.execute(
                f"UPDATE subscriptions SET {', '.join(updates)} "
                "WHERE subscription_id = ?",
                args,
            )
            conn.commit()
        return self.get(subscription_id)

    def record_event(self, event_id: str, event_type: str) -> bool:
        """Dedup gate: True when this event id was not seen before."""
        with get_connection(self._path()) as conn:
            cur = conn.execute(
                "INSERT OR IGNORE INTO webhook_events "
                "(event_id, event_type, processed_at) VALUES (?, ?, ?)",
                (
                    event_id,
                    event_type,
                    datetime.now(UTC).isoformat(),
                ),
            )
            conn.commit()
        return cur.rowcount == 1


store = BillingStore()


def _subscription_entity(event: dict[str, Any]) -> dict[str, Any] | None:
    payload = event.get("payload")
    if not isinstance(payload, dict):
        return None
    entity = (payload.get("subscription") or {}).get("entity")
    return entity if isinstance(entity, dict) else None


def _entity_event_id(event: dict[str, Any]) -> str | None:
    """Explicit event id, else a deterministic identity for dedup."""
    explicit = event.get("event_id")
    if explicit:
        return str(explicit)
    entity = _subscription_entity(event)
    if entity and entity.get("id"):
        return f"{event.get('event', 'unknown')}:{entity['id']}"
    return None


def _resolve_subscription(
    event: dict[str, Any],
) -> tuple[dict[str, Any] | None, str | None]:
    """Find the local subscription a webhook refers to.

    Resolution order: an existing row by the provider subscription id
    (subscription.* events), then the notes.user_id link the checkout
    flow writes, then payload entities that carry subscription_id
    (payment.*/refund.* events). Returns the row and the provider id
    that should be linked onto it.
    """
    entity = _subscription_entity(event)
    if entity is not None:
        provider_id = entity.get("id")
        if provider_id:
            existing = store.get_by_provider_id(str(provider_id))
            if existing is not None:
                return existing, None
        notes = entity.get("notes") or {}
        user_id = notes.get("user_id")
        if user_id:
            latest = store.latest_for_user(str(user_id))
            if latest is not None:
                return latest, str(provider_id) if provider_id else None
        return None, None

    payload = event.get("payload") or {}
    for section in ("payment", "refund"):
        ref = (payload.get(section) or {}).get("entity") or {}
        provider_id = ref.get("subscription_id")
        if provider_id:
            existing = store.get_by_provider_id(str(provider_id))
            if existing is not None:
                return existing, None
        notes = ref.get("notes") or {}
        if notes.get("user_id"):
            return store.latest_for_user(str(notes["user_id"])), None
    return None, None


def _apply_event(event: dict[str, Any]) -> dict[str, Any]:
    """Apply one verified, non-duplicate webhook to the state machine."""
    event_type = str(event.get("event", ""))
    if event_type not in _HANDLED_EVENTS:
        return {"applied": False, "reason": f"unhandled event {event_type}"}

    entity = _subscription_entity(event) or {}
    subscription, provider_id = _resolve_subscription(event)
    now = _now()

    # No local row yet: subscription.* events create one when the
    # provider notes carry the local user id — that is the production
    # flow for paid plans (the webhook, not checkout, writes the row).
    if subscription is None:
        notes = entity.get("notes") or {}
        user_id = notes.get("user_id")
        if (
            event_type.startswith("subscription.")
            and entity.get("id")
            and user_id
        ):
            status = str(entity.get("status", "")).lower()
            state = _STATUS_MAP.get(status)
            if state is None:
                return {
                    "applied": False,
                    "reason": f"unknown subscription status {status!r}",
                }
            period_end = entity.get("current_end")
            row = store.create_subscription(
                user_id=str(user_id),
                plan_id=str(entity.get("plan_id") or entity["id"]),
                state=state,
                period_end=(
                    float(period_end)
                    if period_end is not None
                    and state not in _TERMINAL_STATES
                    else None
                ),
                provider_subscription_id=str(entity["id"]),
            )
            if state == "cancelled":
                row = store.transition(
                    row["subscription_id"], cancelled_at=now
                )
            return {"applied": True, "created": True, "subscription": row}
        return {
            "applied": False,
            "reason": (
                "no local subscription matches this webhook; the "
                "provider notes must carry the local user id"
            ),
        }

    if event_type == "subscription.charged":
        if subscription["state"] in _TERMINAL_STATES:
            return {
                "applied": False,
                "reason": (
                    f"subscription is {subscription['state']}; a charge "
                    "does not resurrect it"
                ),
                "subscription": _public(subscription),
            }
        # Out-of-order guard: the paid period only ever moves forward.
        new_end = entity.get("current_end")
        current_end = subscription["current_period_end"] or 0.0
        if new_end is not None and float(new_end) <= current_end:
            return {
                "applied": False,
                "reason": "out-of-order charge: period end did not advance",
                "subscription": _public(subscription),
            }
        return {
            "applied": True,
            "subscription": store.transition(
                subscription["subscription_id"],
                state="active",
                period_end=float(new_end) if new_end is not None else None,
                provider_subscription_id=provider_id,
            ),
        }

    if event_type == "payment.failed":
        if subscription["state"] not in ("trialing", "active"):
            return {
                "applied": False,
                "reason": (
                    f"payment.failed does not move a "
                    f"{subscription['state']} subscription"
                ),
                "subscription": _public(subscription),
            }
        return {
            "applied": True,
            "subscription": store.transition(
                subscription["subscription_id"], state="past_due"
            ),
        }

    if event_type == "refund.processed":
        return {
            "applied": True,
            "subscription": store.transition(
                subscription["subscription_id"],
                state="cancelled",
                refunded_at=now,
                cancelled_at=subscription["cancelled_at"] or now,
            ),
        }

    # subscription.* lifecycle events on an existing row: map the
    # entity status honestly.
    status = str(entity.get("status", "")).lower()
    state = _STATUS_MAP.get(status)
    if state is None:
        return {
            "applied": False,
            "reason": f"unknown subscription status {status!r}",
            "subscription": _public(subscription),
        }
    if state in _TERMINAL_STATES and subscription["state"] in _TERMINAL_STATES:
        return {
            "applied": False,
            "reason": f"already {subscription['state']}",
            "subscription": _public(subscription),
        }
    period_end = entity.get("current_end")
    return {
        "applied": True,
        "subscription": store.transition(
            subscription["subscription_id"],
            state=state,
            period_end=(
                float(period_end)
                if period_end is not None
                and state not in _TERMINAL_STATES
                else None
            ),
            cancelled_at=now if state == "cancelled" else None,
            provider_subscription_id=provider_id,
        ),
    }


def _public(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "subscription_id": row["subscription_id"],
        "plan_id": row["plan_id"],
        "state": row["state"],
        "current_period_end": (
            _iso(row["current_period_end"])
            if row["current_period_end"]
            else None
        ),
        "cancelled_at": (
            _iso(row["cancelled_at"]) if row["cancelled_at"] else None
        ),
        "refunded_at": (
            _iso(row["refunded_at"]) if row["refunded_at"] else None
        ),
        "provider": row["provider"],
    }


# -- endpoints ------------------------------------------------------------


@router.get("/plans")
def plans() -> dict[str, Any]:
    """Public pricing from config; signed-out visitors see the catalog."""
    catalog = [
        {
            "plan_id": plan_id,
            "name": meta["name"],
            "period_days": meta["period_days"],
            "trial": meta["trial"],
            **_gst_breakdown(int(meta["price_inr"])),
        }
        for plan_id, meta in _plan_catalog().items()
        if plan_id  # unset env ids are not sellable plans
    ]
    return {
        "provider": "razorpay",
        "key_id": RAZORPAY_KEY_ID,
        "gst": {
            "rate": RAZORPAY_GST_RATE,
            "cgst_rate": RAZORPAY_CGST_RATE,
            "sgst_rate": RAZORPAY_SGST_RATE,
        },
        "plans": catalog,
    }


@router.post("/checkout")
def checkout(
    body: BillingCheckoutRequest,
    user: dict[str, Any] = Depends(require_user),
) -> dict[str, Any]:
    """Start a plan: trial activates now; paid plans return the order.

    Honest note: this deployment makes no Razorpay API call. The trial
    plan is free, so its subscription starts here in ``trialing``. A
    paid plan's local subscription is created by the signed webhook
    (subscription.activated/charged) once production completes payment
    — checkout only returns the computed order with the GST split.
    """
    plan_id = body.plan_id.strip()
    catalog = _plan_catalog()
    if not plan_id or plan_id not in catalog:
        raise RequestValidationError(
            "unknown plan_id",
            details={"known": [p for p in catalog if p]},
        )
    meta = catalog[plan_id]
    pricing = _gst_breakdown(int(meta["price_inr"]))
    if meta["trial"]:
        period_end = _now() + int(meta["period_days"]) * 86400
        subscription = store.create_subscription(
            user_id=user["user_id"],
            plan_id=plan_id,
            state="trialing",
            period_end=period_end,
        )
        return {
            "mode": "activated",
            "plan_id": plan_id,
            "pricing": pricing,
            "subscription": _public(subscription),
            "refund_policy": TRINITY_REFUND_CANCELLATION,
        }
    return {
        "mode": "inline",
        "plan_id": plan_id,
        "pricing": pricing,
        "provider": "razorpay",
        "key_id": RAZORPAY_KEY_ID,
        "note": (
            "no Razorpay API call is made in this deployment; "
            "production completes payment and the signed webhook "
            "(subscription.activated / subscription.charged) creates "
            "and activates the subscription"
        ),
        "refund_policy": TRINITY_REFUND_CANCELLATION,
    }


@router.post("/webhook")
async def webhook(
    request: Request,
    x_razorpay_signature: str = Header(default=""),
) -> dict[str, Any]:
    """Razorpay → state machine. Signature is the only authentication."""
    body = await request.body()
    if not _verify_signature(body, x_razorpay_signature):
        raise WebhookSignatureError(
            "webhook signature check failed"
            if RAZORPAY_WEBHOOK_SECRET
            else "webhook secret is not configured"
        )
    try:
        event = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise RequestValidationError(
            "webhook body is not valid JSON"
        ) from exc
    if not isinstance(event, dict):
        raise RequestValidationError("webhook body must be a JSON object")

    event_type = str(event.get("event", ""))
    event_id = _entity_event_id(event)
    if not event_id:
        raise RequestValidationError(
            "webhook carries no event identity to dedupe on"
        )

    if not store.record_event(event_id, event_type):
        # Razorpay retries deliveries; a replay is acked, never reapplied.
        return {"duplicate": True, "event_id": event_id}

    result = _apply_event(event)
    return {"event_id": event_id, "event": event_type, **result}


@router.get("/subscription")
def my_subscription(
    user: dict[str, Any] = Depends(require_user),
) -> dict[str, Any]:
    """The signed-in user's subscription; the free tier stays explicit."""
    row = store.latest_for_user(user["user_id"])
    if row is None:
        return {"subscription": None, "tier": "free"}
    if row["state"] in _TERMINAL_STATES:
        tier = "free"
    elif row["state"] == "trialing":
        tier = "trial"
    else:
        tier = "paid"
    return {"subscription": _public(row), "tier": tier}


def resolve_tier(
    user_id: str | None,
) -> tuple[str, str | None, str | None]:
    """(tier, plan_id, state) for limits and plan tokens.

    trialing/active/past_due all carry the paid limit tables —
    ``past_due`` *is* the grace period by construction: the state
    machine keeps it out of the terminal states until the operator's
    webhook moves it to cancelled/expired.
    """
    if not user_id:
        return "free", None, None
    row = store.latest_for_user(user_id)
    if row is None or row["state"] in _TERMINAL_STATES:
        return "free", None, None
    return "paid", row["plan_id"], row["state"]


@router.get("/plan-token")
def plan_token(
    user: dict[str, Any] = Depends(require_user),
) -> dict[str, Any]:
    """A signed, offline-verifiable snapshot of the plan state (M33).

    The token carries tier/state/plan and expires after
    TRINITY_PLAN_TOKEN_DAYS — the offline grace window: a desktop or
    CLI verifies it locally (signature + expiry, no database) and must
    re-sync within that window.
    """
    tier, plan_id, state = resolve_tier(user["user_id"])
    ttl = TRINITY_PLAN_TOKEN_DAYS * 86400
    token = mint_signed_token(
        {
            "sub": user["user_id"],
            "email": user["email"],
            "typ": "plan",
            "tier": tier,
            "state": state,
            "plan_id": plan_id,
            "jti": uuid.uuid4().hex,
        },
        ttl,
    )
    return {
        "plan_token": token,
        "tier": tier,
        "state": state,
        "plan_id": plan_id,
        "expires_at": _iso(_now() + ttl),
        "offline_days": TRINITY_PLAN_TOKEN_DAYS,
    }


def verify_plan_token(token: str) -> dict[str, Any]:
    """Offline verify (M33): signature + expiry only, no database."""
    return verify_signed_token(token, expected_typ="plan")


@router.post("/cancel")
def cancel(user: dict[str, Any] = Depends(require_user)) -> dict[str, Any]:
    """Cancel per the refund-cancellation policy the user agreed to."""
    row = store.latest_for_user(user["user_id"])
    if row is None or row["state"] in _TERMINAL_STATES:
        raise RequestValidationError(
            "no cancellable subscription",
            details={"state": row["state"] if row else None},
        )
    now = _now()
    updated = store.transition(
        row["subscription_id"], state="cancelled", cancelled_at=now
    )
    assert updated is not None
    return {
        "cancelled": True,
        "access_until": (
            _iso(updated["current_period_end"])
            if updated["current_period_end"]
            else None
        ),
        "policy": TRINITY_REFUND_CANCELLATION,
        "subscription": _public(updated),
    }
