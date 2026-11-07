# Cloud configuration — fill these in from your Razorpay dashboard and legal docs
# Never commit real secrets; use environment variables in production.

# Razorpay configuration
RAZORPAY_KEY_ID = ""  # Razorpay key ID
RAZORPAY_KEY_SECRET = ""  # Razorpay key secret
RAZORPAY_WEBHOOK_SECRET = ""  # Webhook signature secret

# Plan definitions — user fills in plan names, prices (INR), and limits
# Format: plan_name -> dict with price_inr, daily/monthly LLM/export/design limits
PLANS = {}

# Business details — user fills in
BUSINESS_NAME = ""  # e.g., "Trinity AI Systems"
BUSINESS_ADDRESS = ""  # e.g., "123 Engineering Way, Bengaluru 560001, India"
BUSINESS_GSTIN = ""  # GST identification number if applicable

# Domain and email
DEFAULT_DOMAIN = ""  # e.g., "trinity.ai"
DEFAULT_SENDER_EMAIL = ""  # e.g., "no-reply@trinity.ai"

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