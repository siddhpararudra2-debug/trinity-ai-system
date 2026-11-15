"""M33 plans and limits: per-day metering with plain over-limit messages.

Limits are per deployment via config; 0 means unlimited, so the dummy
deployment (a local-only LLM) ships unmetered and production sets real
caps in its env file. Enforcement is account-aware:

- signed-out use is the free tier (the M30 promise), metered under the
  shared ``anonymous`` scope
- trialing/active/past_due carry the paid tables — ``past_due`` is the
  grace period by construction (the state machine keeps it out of the
  terminal states; see billing.resolve_tier)
- over a limit the API answers 402 naming the plan and the limit —
  never a silent failure, and the free tier's Stage 12 features keep
  working until the configured cap is actually reached
"""

from __future__ import annotations

from datetime import UTC, datetime

from src.api.billing import resolve_tier
from src.core.config import (
    TRINITY_LIMIT_FREE_EXPORTS_PER_DAY,
    TRINITY_LIMIT_FREE_LLM_CALLS_PER_DAY,
    TRINITY_LIMIT_FREE_SAVED_DESIGNS_PER_DAY,
    TRINITY_LIMIT_PAID_EXPORTS_PER_DAY,
    TRINITY_LIMIT_PAID_LLM_CALLS_PER_DAY,
    TRINITY_LIMIT_PAID_SAVED_DESIGNS_PER_DAY,
    settings,
)
from src.core.errors import PlanLimitError
from src.db.database import get_connection

METRICS = ("llm_calls", "exports", "saved_designs")

# Plain-language names the over-limit message uses verbatim.
METRIC_LABELS = {
    "llm_calls": "model calls",
    "exports": "artifact downloads",
    "saved_designs": "design saves",
}

# Per-tier per-day limits from config; 0 = unlimited (unmetered).
FREE_LIMITS: dict[str, int] = {
    "llm_calls": TRINITY_LIMIT_FREE_LLM_CALLS_PER_DAY,
    "exports": TRINITY_LIMIT_FREE_EXPORTS_PER_DAY,
    "saved_designs": TRINITY_LIMIT_FREE_SAVED_DESIGNS_PER_DAY,
}
PAID_LIMITS: dict[str, int] = {
    "llm_calls": TRINITY_LIMIT_PAID_LLM_CALLS_PER_DAY,
    "exports": TRINITY_LIMIT_PAID_EXPORTS_PER_DAY,
    "saved_designs": TRINITY_LIMIT_PAID_SAVED_DESIGNS_PER_DAY,
}


def _today() -> str:
    return datetime.now(UTC).strftime("%Y-%m-%d")


class UsageStore:
    """SQLite per-day counters (plain sqlite3, PRD §8)."""

    def __init__(self, db_path: str | None = None):
        self.db_path = db_path  # None → settings.db_path at call time

    def _path(self):
        return self.db_path or settings.db_path

    def increment(self, metric: str, scope: str) -> int:
        """Count one use; returns the day's new total."""
        day = _today()
        with get_connection(self._path()) as conn:
            conn.execute(
                "INSERT INTO usage_counters (day, scope, metric, count) "
                "VALUES (?, ?, ?, 1) "
                "ON CONFLICT(day, scope, metric) DO UPDATE SET "
                "count = count + 1",
                (day, scope, metric),
            )
            row = conn.execute(
                "SELECT count FROM usage_counters "
                "WHERE day = ? AND scope = ? AND metric = ?",
                (day, scope, metric),
            ).fetchone()
            conn.commit()
        return int(row[0])

    def count(self, metric: str, scope: str) -> int:
        with get_connection(self._path()) as conn:
            row = conn.execute(
                "SELECT count FROM usage_counters "
                "WHERE day = ? AND scope = ? AND metric = ?",
                (_today(), scope, metric),
            ).fetchone()
        return int(row[0]) if row is not None else 0


store = UsageStore()


def check_and_increment(metric: str, user_id: str | None) -> int:
    """Meter one use; raise PlanLimitError past the tier's daily cap.

    Unlimited (0) tiers are never metered — nothing is written and no
    query runs. Returns the day's running count for tests and logs.
    """
    tier, _plan_id, _state = resolve_tier(user_id)
    table = PAID_LIMITS if tier == "paid" else FREE_LIMITS
    limit = table[metric]
    if limit <= 0:
        return 0
    scope = user_id or "anonymous"
    count = store.increment(metric, scope)
    if count > limit:
        raise PlanLimitError(
            f"daily limit of {limit} {METRIC_LABELS[metric]} reached on "
            f"the {tier} plan; it resets at UTC midnight — upgrade at "
            "/api/billing/plans or wait for the reset",
            details={
                "metric": metric,
                "limit": limit,
                "plan": tier,
                "used": count,
            },
        )
    return count
