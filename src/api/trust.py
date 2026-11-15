"""M31 trust pages: the four policies, served from config, honestly draft.

Privacy, terms, refund/cancellation and design ownership all come from
env copy (zero code edits per the deployment contract). Every policy is
marked ``draft: true`` with a "needs lawyer review" note — the copy is
the user's own facts, not legal advice. The signup flow stamps
``TRINITY_POLICY_VERSION`` on the account (M30 accounts.agreed_policy_*).
"""

from __future__ import annotations

from typing import Any

from fastapi import APIRouter

from src.core.config import (
    TRINITY_COMPANY_NAME,
    TRINITY_DESIGN_OWNERSHIP,
    TRINITY_POLICY_VERSION,
    TRINITY_PRIVACY_POLICY,
    TRINITY_REFUND_CANCELLATION,
    TRINITY_SUPPORT_EMAIL,
    TRINITY_TERMS_OF_SERVICE,
)

router = APIRouter(prefix="/api", tags=["trust"])

_DRAFT_NOTE = "DRAFT - needs lawyer review"


def policies() -> list[dict[str, Any]]:
    """The four policies in fixed order; empty copy stays an empty draft."""
    items = (
        ("privacy", "Privacy policy", TRINITY_PRIVACY_POLICY),
        ("terms", "Terms of service", TRINITY_TERMS_OF_SERVICE),
        (
            "refund-cancellation",
            "Refund and cancellation",
            TRINITY_REFUND_CANCELLATION,
        ),
        ("design-ownership", "Design ownership", TRINITY_DESIGN_OWNERSHIP),
    )
    return [
        {
            "slug": slug,
            "title": title,
            "text": text,
            "version": TRINITY_POLICY_VERSION,
            "draft": True,
            "note": _DRAFT_NOTE,
        }
        for slug, title, text in items
    ]


@router.get("/trust")
def trust() -> dict[str, Any]:
    return {
        "company": TRINITY_COMPANY_NAME,
        "support_email": TRINITY_SUPPORT_EMAIL,
        "policy_version": TRINITY_POLICY_VERSION,
        "draft_note": _DRAFT_NOTE,
        "policies": policies(),
    }
