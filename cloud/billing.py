"""Billing — Razorpay Subscriptions + Checkout, INR only.

Store gateway ids only. No custom payment code.

Item 11: Razorpay test-mode billing:
- Plans: trial, monthly, annual
- Usage limits per plan
- Webhook signature verification
- GST invoice (18%; CGST+SGST or IGST)
"""

import hashlib
import hmac
import json
import time
from dataclasses import dataclass, field
from datetime import UTC, datetime
from enum import Enum
from typing import Any, Optional


class PlanType(Enum):
    TRIAL = "trial"
    MONTHLY = "monthly"
    ANNUAL = "annual"


class GSTType(Enum):
    CGST_SGST = "CGST_SGST"  # 9% CGST + 9% SGST for intra-state
    IGST = "IGST"  # 18% IGST for inter-state


# Razorpay plan definitions (Item 11)
RAZORPAY_PLANS: dict[PlanType, dict[str, Any]] = {
    PlanType.TRIAL: {
        "name": "Trieval Trial",
        "amount_inr": 0,  # free trial
        "total_count": 1,  # 1 month duration
        "interval": "monthly",
        "gst_type": GSTType.CGST_SGST,
    },
    PlanType.MONTHLY: {
        "name": "Trieval Pro Monthly",
        "amount_inr": 299,  # INR 299/month
        "total_count": 1,  # recurring monthly
        "interval": "monthly",
        "gst_type": GSTType.CGST_SGST,
    },
    PlanType.ANNUAL: {
        "name": "Trieval Pro Annual",
        "amount_inr": 3299,  # INR 3299/year (≈16% discount)
        "total_count": 12,  # 12 months
        "interval": "yearly",
        "gst_type": GSTType.CGST_SGST,
    },
}


class PlanStatus(Enum):
    ACTIVE = "active"
    TRIALING = "traling"
    PAST_DUE = "past_due"
    CANCELED = "canceled"
    EXPIRED = "expired"


@dataclass
class PlanInfo:
    plan_type: PlanType
    razorpay_plan_id: str  # id created in Razorpay dashboard
    name: str
    amount_inr: int  # in paise for Razorpay
    total_count: int
    interval: str  # "monthly" or "yearly"
    gst_type: GSTType
    usage_limit: int = 0  # max concurrent jobs or executions


# Per-plan usage limits (Item 11)
PLAN_USAGE_LIMITS: dict[PlanType, int] = {
    PlanType.TRIAL: 1,  # 1 concurrent job max
    PlanType.MONTHLY: 5,  # 5 concurrent jobs max
    PlanType.ANNUAL: 10,  # 10 concurrent jobs max
}


# Simulated database of active subscriptions
_active_subscriptions: dict[str, dict[str, Any]] = {}


def get_plan_info(plan_type: PlanType) -> PlanInfo:
    """Get plan configuration for the given plan type."""
    plan_def = RAZORPAY_PLANS[plan_type]
    return PlanInfo(
        plan_type=plan_type,
        razorpay_plan_id=f"plan_{plan_type.value}",  # simulated Razorpay ID
        name=plan_def["name"],
        amount_inr=plan_def["amount_inr"] * 100,  # Razorpay uses paise
        total_count=plan_def["total_count"],
        interval=plan_def["interval"],
        gst_type=plan_def["gst_type"],
        usage_limit=PLAN_USAGE_LIMITS[plan_type],
    )


def calculate_gst(amount_inr: int, gst_type: GSTType) -> dict[str, int]:
    """Calculate GST breakdown for an amount.

    18% GST: either CGST(9%) + SGST(9%) for intra-state,
    or IGST(18%) for inter-state.
    """
    rate = gst_type.value  # will parse from string
    # gst_type is a GSTType enum, get the rate
    if gst_type == GSTType.CGST_SGST:
        cgst_rate = 9
        sgst_rate = 9
        # Split amount into CGST + SGST
        # amount_inr is total including GST
        # base = amount / 1.18
        # cgst = base * 9/100
        # sgst = base * 9/100
        import math
        base = math.floor(amount_inr / 118 * 100)
        cgst = math.floor(base * cgst_rate / 100)
        sgst = math.floor(base * sgst_rate / 100)
        return {"cgst": cgst, "sgst": sgst, "total_gst": cgst + sgst}
    elif gst_type == GSTType.IGST:
        igst = math.floor(amount_inr * 18 / 100)
        return {"igst": igst, "total_gst": igst}
    return {"total_gst": 0}


def generate_gst_invoice(
    plan_type: PlanType,
    amount_inr: int,
    gst_type: GSTType,
    customer_email: str,
) -> dict[str, Any]:
    """Generate a GST invoice for a Razorpay transaction.

    Item 11: 18% GST; CGST+SGST or IGST based on gst_type.
    """
    gst_breakdown = calculate_gst(amount_inr, gst_type)

    # Determine GST label
    if gst_type == GSTType.CGST_SGST:
        gst_label = "18% GST (9% CGST + 9% SGST)"
    else:
        gst_label = "18% IGST"

    invoice = {
        "invoice_id": f"inv_{plan_type.value}_{int(time.time())}",
        "plan_type": plan_type.value,
        "customer_email": customer_email,
        "amount_breakdown": {
            "subtotal_inr": amount_inr - gst_breakdown.get("total_gst", 0),
            "gst_details": gst_breakdown,
            "total_inr": amount_inr,
        },
        "gst_rate": gst_label,
        "gst_number": "trinity-ai-system@example.com",  # placeholder
        "issue_date": datetime.now(UTC).strftime("%Y-%m-%d"),
        "status": "generated",
    }
    return invoice


def verify_webhook_signature(
    body: bytes, signature: str, webhook_secret: str
) -> bool:
    """Verify Razorpay webhook signature.

    Item 11: webhook signature verification using the shared secret.
    """
    from hmac import compare_digest

    expected = "sha256=" + hmac.new(
        webhook_secret.encode(), body, hashlib.sha256
    ).hexdigest()
    return compare_digest(expected, signature)


# Simulated checkout session creation
def create_checkout_session(
    plan_type: PlanType, customer_email: str, receipt: Optional[str] = None
) -> dict[str, Any]:
    """Create a Razorpay Checkout order for the given plan.

    Item 11: Returns order data suitable for Razorpay Checkout.
    """
    plan = get_plan_info(plan_type)
    amount_inr = plan.amount_inr // 100  # convert paise to INR

    # Calculate GST
    gst_breakdown = calculate_gst(amount_inr, plan.gst_type)

    # Total amount including GST
    total_amount_inr = amount_inr + gst_breakdown["total_gst"]

    # Create Razorpay order data
    order_data = {
        "amount": total_amount_inr * 100,  # paise (with GST)
        "currency": "INR",
        "receipt": receipt or f"receipt_{plan_type.value}_{int(time.time())}",
        "payment_capture": 1,
        "metadata": {
            "plan_type": plan_type.value,
            "customer_email": customer_email,
        },
    }

    # Simulated order response (in production, use razorpay_client.order.create)
    order = {
        "id": f"order_{plan_type.value}_{int(time.time())}",
        "amount": order_data["amount"],
        "currency": order_data["currency"],
        "status": "created",
        "gst_breakdown": gst_breakdown,
        "total_amount_inr": total_amount_inr,
        "gst_label": (
            "18% GST (9% CGST + 9% SGST)"
            if plan.gst_type == GSTType.CGST_SGST
            else "18% IGST"
        ),
    }

    return order


# Simulated subscription creation
def create_subscription(
    plan_type: PlanType, customer_email: str
) -> dict[str, Any]:
    """Create a Razorpay Subscription for the given plan.

    Item 11: Returns subscription data.
    """
    plan = get_plan_info(plan_type)

    # Simulated subscription
    subscription = {
        "id": f"sub_{plan_type.value}_{int(time.time())}",
        "plan_id": plan.razorpay_plan_id,
        "status": PlanStatus.TRIAL.value
        if plan_type == PlanType.TRIAL
        else PlanStatus.ACTIVE.value,
        "customer_email": customer_email,
        "total_count": plan.total_count,
        "interval": plan.interval,
        "amount_inr": plan.amount_inr,
        "gst_type": plan.gst_type,
        "usage_limit": plan.usage_limit,
        "start_date": datetime.now(UTC).strftime("%Y-%m-%d"),
    }

    # Store simulated subscription
    _active_subscriptions[subscription["id"]] = {
        "plan_type": plan_type,
        "customer_email": customer_email,
        "status": subscription["status"],
        "created_at": datetime.now(UTC).isoformat(),
    }

    return subscription


def get_subscription(subscription_id: str) -> Optional[dict[str, Any]]:
    """Retrieve a subscription by ID."""
    return _active_subscriptions.get(subscription_id)


def cancel_subscription(subscription_id: str) -> Optional[dict[str, Any]]:
    """Cancel a subscription.

    Item 11: Updates subscription status to CANCELED.
    """
    subscription = _active_subscriptions.get(subscription_id)
    if subscription is None:
        return None

    subscription["status"] = PlanStatus.CANCELED.value
    subscription["canceled_at"] = datetime.now(UTC).isoformat()
    return subscription


def list_plans() -> list[dict[str, Any]]:
    """List all available Razorpay plans (Item 11).

    Returns plan information suitable for displaying to users.
    """
    plans = []
    for plan_type in PlanType:
        plan = get_plan_info(plan_type)
        plans.append(
            {
                "plan_type": plan_type.value,
                "name": plan.name,
                "amount_inr": plan.amount_inr,
                "interval": plan.interval,
                "total_count": plan.total_count,
                "usage_limit": plan.usage_limit,
                "gst_type": plan.gst_type.value,
            }
        )
    return plans