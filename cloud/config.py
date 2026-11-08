# Cloud configuration — fill these in from your Razorpay dashboard and legal docs
# Never commit real secrets; use environment variables in production.

# Razorpay configuration
RAZORPAY_KEY_ID = ""  # Razorpay key ID
RAZORPAY_KEY_SECRET = ""  # Razorpay key secret
RAZORPAY_WEBHOOK_SECRET = ""  # Webhook signature secret

# Plan definitions — user fills in plan names, prices (INR), and limits
# Format: plan_name -> dict with price_inr (in paise, e.g. 499 = ₹4.99),
#   daily_llm_calls, daily_exports, daily_saved_designs,
#   monthly_llm_calls, monthly_exports, monthly_saved_designs,
#   trial_days (0 = no trial), yearly_discount_months (2 = 2 months free when yearly),
#   is_free (True/False)
PLANS = {
    "explorer": {
        "price_inr": 0,
        "daily_llm_calls": 10,
        "daily_exports": 5,
        "daily_saved_designs": 3,
        "monthly_llm_calls": 0,
        "monthly_exports": 0,
        "monthly_saved_designs": 3,
        "trial_days": 0,
        "yearly_discount_months": 0,
        "is_free": True,
    },
    "maker": {
        "price_inr": 499,  # ₹4.99 in paise
        "daily_llm_calls": 100,
        "daily_exports": 100,
        "daily_saved_designs": 25,
        "monthly_llm_calls": 0,  # Unlimited or set a number
        "monthly_exports": 100,
        "monthly_saved_designs": 25,
        "trial_days": 7,
        "yearly_discount_months": 2,  # Yearly price is 2 months free
        "is_free": False,
    },
    "pro": {
        "price_inr": 1499,  # ₹14.99 in paise
        "daily_llm_calls": 500,
        "daily_exports": 1000,
        "daily_saved_designs": 500,
        "monthly_llm_calls": 0,  # Unlimited or set a number
        "monthly_exports": 1000,
        "monthly_saved_designs": 500,
        "trial_days": 7,
        "yearly_discount_months": 2,  # Yearly price is 2 months free
        "is_free": False,
    },
}

# Business details — user fills in
BUSINESS_NAME = "Trinity Labs (DEMO)"  # e.g., "Trinity AI Systems"
BUSINESS_ADDRESS = ""  # e.g., "123 Engineering Way, Bengaluru 560001, India"
BUSINESS_GSTIN = ""  # GST identification number if applicable

# Domain and email
DEFAULT_DOMAIN = "example.com"  # e.g., "trinity.ai"
DEFAULT_SENDER_EMAIL = "demo@trinity.ai"  # e.g., "no-reply@trinity.ai"

# Triny API
TRINITY_API_BASE = "http://localhost:8000/api"

# Offline grace period for plan tokens (days)
PLAN_TOKEN_OFFLINE_GRACE_DAYS = 7

# LLM proxy daily limit (0 = no limit, -1 = unlimited in dev)
LLM_PROXY_DAILY_LIMIT = 0

# Whether cloud-side limits are enforced or just deterrents
CLOUD_LIMITS_ENFORCED = True  # Set False if local feature locks only (deterrents)

# Documentation note: "Cloud-side LLM proxy limits are real and enforced.
# Local feature locks are deterrents only; they do not gate functionality."

# Demo mode configuration
DEMO_MODE = True  # When True, shows DEMO banner on pricing/billing/legal pages
DEMO_ACCOUNTS = {
    "explorer": {"password": "${TRINITY_DEMO_PW_explorer}"},
    "maker": {"password": "${TRINITY_DEMO_PW_maker}"},
    "pro": {"password": "${TRINITY_DEMO_PW_pro}"},
}
# Demo accounts are seeded only when TRINITY_DEMO=1 environment variable is set
# Passwords are read from environment variables and never written to the repo