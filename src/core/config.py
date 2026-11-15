"""
Trinity Core Configuration
--------------------------
Single source of truth for paths and runtime settings.
V1 intentionally avoids env-driven complexity: everything resolves
relative to the repo root so the system runs with zero setup.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

from dotenv import load_dotenv

REPO_ROOT = Path(__file__).resolve().parents[2]

# The deployment contract (Item 13) is "all real-world values live in
# .env.dummy at the repository root; swapping values requires zero code
# edits". Every setting below is read from os.getenv at *import* time,
# so the env file must be loaded here — before any module binds these
# values. main.py used to own this load, but anything importing config
# before main (src.api.accounts does exactly that) bound empty strings
# permanently; the M31 trust copy came up blank in the full suite while
# passing in isolation. load_dotenv never overrides variables the real
# environment already sets, so a production deployment keeps precedence
# over the dummy file, and a packaged install without the file simply
# runs on its real environment.
load_dotenv(str(REPO_ROOT / ".env.dummy"))

STORAGE_ROOT = Path(os.getenv("TRINITY_STORAGE_ROOT", str(REPO_ROOT / "data")))
DB_PATH = Path(os.getenv("TRINITY_DB_PATH", str(STORAGE_ROOT / "trinity.db")))

ENVIRONMENT = os.getenv("TRINITY_ENV", "development")
CORS_ORIGINS: tuple[str, ...] = tuple(
    origin.strip()
    for origin in os.getenv("TRINITY_CORS_ORIGINS", "*").split(",")
    if origin.strip()
)

# --- Authentication (Item 9) ---
AUTH_ENABLED = os.getenv("TRINITY_AUTH_ENABLED", "false").lower() == "true"
JWT_SECRET = os.getenv("TRINITY_JWT_SECRET", "")
SESSION_TIMEOUT_MINUTES = int(
    os.getenv("TRINITY_SESSION_TIMEOUT_MINUTES", "43200")
)
# M30 accounts: token lifetimes and login lockout. Access tokens keep
# SESSION_TIMEOUT_MINUTES; the refresh token outlives the access token so
# /api/accounts/refresh can mint a fresh pair, and the verify/reset
# tokens are short by design because they arrive by link.
REFRESH_TOKEN_DAYS = int(os.getenv("TRINITY_REFRESH_TOKEN_DAYS", "30"))
VERIFY_TOKEN_HOURS = int(os.getenv("TRINITY_VERIFY_TOKEN_HOURS", "24"))
RESET_TOKEN_MINUTES = int(os.getenv("TRINITY_RESET_TOKEN_MINUTES", "30"))
LOGIN_MAX_FAILURES = int(os.getenv("TRINITY_LOGIN_MAX_FAILURES", "5"))
LOGIN_LOCKOUT_MINUTES = int(os.getenv("TRINITY_LOGIN_LOCKOUT_MINUTES", "15"))
PASSWORD_MIN_LENGTH = int(os.getenv("TRINITY_PASSWORD_MIN_LENGTH", "8"))

# --- Safe Execution (Item 10) ---
SAFE_EXECUTION_ENABLED = os.getenv("SAFE_EXECUTION_ENABLED", "false").lower() == "true"
SAFE_EXECUTION_TIMEOUT_SECONDS = int(
    os.getenv("SAFE_EXECUTION_TIMEOUT_SECONDS", "300")
)
SAFE_EXECUTION_MAX_RETRIES = int(os.getenv("SAFE_EXECUTION_MAX_RETRIES", "3"))
SAFE_EXECUTION_PER_USER_LIMIT = int(os.getenv("SAFE_EXECUTION_PER_USER_LIMIT", "5"))

# --- Trust / business details (Item 12) ---
TRINITY_COMPANY_NAME = os.getenv("TRINITY_COMPANY_NAME", "Trinity AI Systems")
TRINITY_DOMAIN = os.getenv("TRINITY_DOMAIN", "example.com")
TRINITY_ADMIN_EMAIL = os.getenv("TRINITY_ADMIN_EMAIL", "admin@example.com")
TRINITY_SUPPORT_EMAIL = os.getenv("TRINITY_SUPPORT_EMAIL", "support@example.com")
TRINITY_ABOUT_TEXT = os.getenv("TRINITY_ABOUT_TEXT", "")
TRINITY_PRIVACY_POLICY = os.getenv("TRINITY_PRIVACY_POLICY", "")
TRINITY_TERMS_OF_SERVICE = os.getenv("TRINITY_TERMS_OF_SERVICE", "")
TRINITY_DESIGN_OWNERSHIP = os.getenv("TRINITY_DESIGN_OWNERSHIP", "")
# M31: refund/cancellation copy and the policy version stamped at signup.
TRINITY_REFUND_CANCELLATION = os.getenv("TRINITY_REFUND_CANCELLATION", "")
TRINITY_POLICY_VERSION = os.getenv("TRINITY_POLICY_VERSION", "draft-1")

# --- Billing (Item 11: Razorpay) ---
# Plans from config: ids, prices (INR) and period lengths are all
# env-driven so the deployment swaps them without code edits. The
# state machine lives in src/api/billing.py.
RAZORPAY_KEY_ID = os.getenv("RAZORPAY_KEY_ID", "")
RAZORPAY_KEY_SECRET = os.getenv("RAZORPAY_KEY_SECRET", "")
RAZORPAY_WEBHOOK_SECRET = os.getenv("RAZORPAY_WEBHOOK_SECRET", "")
RAZORPAY_GST_RATE = int(os.getenv("RAZORPAY_GST_RATE", "18"))
RAZORPAY_CGST_RATE = int(os.getenv("RAZORPAY_CGST_RATE", "9"))
RAZORPAY_SGST_RATE = int(os.getenv("RAZORPAY_SGST_RATE", "9"))
RAZORPAY_PLAN_TRIAL = os.getenv("RAZORPAY_PLAN_TRIAL", "")
RAZORPAY_PLAN_TRIAL_PRICE_INR = int(os.getenv("RAZORPAY_PLAN_TRIAL_PRICE_INR", "0"))
RAZORPAY_PLAN_TRIAL_DAYS = int(os.getenv("RAZORPAY_PLAN_TRIAL_DAYS", "30"))
RAZORPAY_PLAN_MONTHLY = os.getenv("RAZORPAY_PLAN_MONTHLY", "")
RAZORPAY_PLAN_MONTHLY_PRICE_INR = int(
    os.getenv("RAZORPAY_PLAN_MONTHLY_PRICE_INR", "499")
)
RAZORPAY_PLAN_MONTHLY_DAYS = int(os.getenv("RAZORPAY_PLAN_MONTHLY_DAYS", "30"))
RAZORPAY_PLAN_ANNUAL = os.getenv("RAZORPAY_PLAN_ANNUAL", "")
RAZORPAY_PLAN_ANNUAL_PRICE_INR = int(
    os.getenv("RAZORPAY_PLAN_ANNUAL_PRICE_INR", "4999")
)
RAZORPAY_PLAN_ANNUAL_DAYS = int(os.getenv("RAZORPAY_PLAN_ANNUAL_DAYS", "365"))

# --- Plans and limits (M33) ---
# Per-day limits per tier, from config (0 means unlimited: the dummy
# deployment ships unmetered and production sets real caps in its env
# file). Metering lives in src/api/limits.py.
TRINITY_LIMIT_FREE_LLM_CALLS_PER_DAY = int(
    os.getenv("TRINITY_LIMIT_FREE_LLM_CALLS_PER_DAY", "0")
)
TRINITY_LIMIT_FREE_EXPORTS_PER_DAY = int(
    os.getenv("TRINITY_LIMIT_FREE_EXPORTS_PER_DAY", "0")
)
TRINITY_LIMIT_FREE_SAVED_DESIGNS_PER_DAY = int(
    os.getenv("TRINITY_LIMIT_FREE_SAVED_DESIGNS_PER_DAY", "0")
)
TRINITY_LIMIT_PAID_LLM_CALLS_PER_DAY = int(
    os.getenv("TRINITY_LIMIT_PAID_LLM_CALLS_PER_DAY", "0")
)
TRINITY_LIMIT_PAID_EXPORTS_PER_DAY = int(
    os.getenv("TRINITY_LIMIT_PAID_EXPORTS_PER_DAY", "0")
)
TRINITY_LIMIT_PAID_SAVED_DESIGNS_PER_DAY = int(
    os.getenv("TRINITY_LIMIT_PAID_SAVED_DESIGNS_PER_DAY", "0")
)
# Offline grace: how long a signed plan token stays verifiable with no
# backend contact (M33 "offline verify, grace").
TRINITY_PLAN_TOKEN_DAYS = int(os.getenv("TRINITY_PLAN_TOKEN_DAYS", "7"))

# --- Feedback / analytics (Item 13) ---
# Empty TRINITY_FEEDBACK_ENDPOINT means feedback is stored in the local log only.
TRINITY_FEEDBACK_ENDPOINT = os.getenv("TRINITY_FEEDBACK_ENDPOINT", "")
TRINITY_LOGGING_ENABLED = os.getenv("TRINITY_LOGGING_ENABLED", "true").lower() == "true"
TRINITY_ANALYTICS_ENABLED = os.getenv("TRINITY_ANALYTICS_ENABLED", "false").lower() == "true"


@dataclass(frozen=True)
class Settings:
    repo_root: Path = REPO_ROOT
    storage_root: Path = STORAGE_ROOT

    db_path: Path = DB_PATH

    projects_dir: Path = STORAGE_ROOT / "projects"
    artifacts_dir: Path = STORAGE_ROOT / "artifacts"
    jobs_dir: Path = STORAGE_ROOT / "jobs"
    cad_dir: Path = STORAGE_ROOT / "cad"
    exports_dir: Path = STORAGE_ROOT / "exports"
    temp_dir: Path = STORAGE_ROOT / "temp"
    cache_dir: Path = STORAGE_ROOT / "cache"
    embeddings_dir: Path = STORAGE_ROOT / "embeddings"
    evaluation_dir: Path = STORAGE_ROOT / "evaluation"

    api_title: str = "Trinity AI — Engineering Operating System"
    api_version: str = "1.0.0"

    # Uploads land in artifact storage like any other engine output, so they
    # get a checksum and a lineage row. 5 MB is generous for an image
    # inspection input and keeps a stray video from filling the disk.
    max_upload_bytes: int = 5 * 1024 * 1024

    environment: str = ENVIRONMENT
    cors_origins: tuple[str, ...] = CORS_ORIGINS

    def all_storage_dirs(self) -> list[Path]:
        return [
            self.projects_dir,
            self.artifacts_dir,
            self.jobs_dir,
            self.cad_dir,
            self.exports_dir,
            self.temp_dir,
            self.cache_dir,
            self.embeddings_dir,
            self.evaluation_dir,
        ]


settings = Settings()


def ensure_storage_layout() -> None:
    """Create the on-disk storage tree if it doesn't exist yet. Idempotent."""
    for d in settings.all_storage_dirs():
        d.mkdir(parents=True, exist_ok=True)


def resolve_cors_origins(environment: str, origins: tuple[str, ...] | list[str]) -> list[str]:
    """Return the CORS origins allowed for this environment.

    Development defaults to the wildcard ("*") so the local frontend works
    with zero setup. Production refuses wildcard or empty origin lists at
    startup so an exposed deployment cannot ship with open CORS by accident.
    """
    resolved = list(origins)
    if environment == "production" and ("*" in resolved or not resolved):
        raise ValueError(
            "Wildcard or empty CORS origins are refused when TRINITY_ENV=production; "
            "set TRINITY_CORS_ORIGINS to explicit origins"
        )
    return resolved
