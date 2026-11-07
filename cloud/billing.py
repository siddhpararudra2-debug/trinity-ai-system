# Billing — Razorpay Subscriptions + Checkout, INR only
# Store gateway ids only. No custom payment code.

# Razorpay SDK — requires: pip install razorpay
# razorpay_client = razorpay.Client(auth=(RAZORPAY_KEY_ID, RAZORPAY_KEY_SECRET))

# Checkout session creation
# def create_checkout_session(amount_inr, currency="INR", receipt=None):
#     """Create a Razorpay Checkout order."""
#     order_data = {
#         "amount": amount_inr * 100,  # paise
#         "currency": currency,
#         "receipt": receipt,
#         "payment_capture": 1,
#     }
#     order = razorpay_client.order.create(data=order_data)
#     return order  # contains id, amount, currency, etc.

# Subscription creation
# def create_subscription(plan_id, total_count, interval="monthly"):
#     """Create a Razorpay Subscription."""
#     subscription_data = {
#         "plan_id": plan_id,
#         "total_count": total_count,
#         "interval": interval,
#     }
#     subscription = razorpay_client.subscription.create(data=subscription_data)
#     return subscription

# Webhook signature verification
# def verify_webhook_signature(body, signature, secret):
#     """Verify Razorpay webhook signature."""
#     from hmac import compare_digest
#     expected_signature = "sha256=" + hmac.new(secret.encode(), body, hashlib.sha256).hexdigest()
#     return compare_digest(expected_signature, signature)

# Webhook event types
# EVENT_PAYMENT_AUTHORIZED = "payment.authorized"
# EVENT_PAYMENT_CAPTURED = "payment.captured"
# EVENT_SUBSCRIPTION_CREATED = "subscription.created"
# EVENT_SUBSCRIPTION_PAUSED = "subscription.paused"
# EVENT_SUBSCRIPTION_CANCELED = "subscription.canceled"
# EVENT_SUBSCRIPTION_EXPIRED = "subscription.expired"

# Subscription states
# STATE_TRALING = "trialing"
# STATE_ACTIVE = "active"
# STATE_PAST_DUE = "past_due"
# STATE_CANCELED = "canceled"
# STATE_EXPIRED = "expired"

# Dedup webhooks by event id
# processed_events = set()  # event_id -> timestamp

# Refund processing
# def refund_payment(payment_id, amount=None):
#     """Process a refund via Razorpay."""
#     refund_data = {}
#     if amount:
#         refund_data["amount"] = amount  # paise
#     return razorpay_client.refund.create(payment_id=payment_id, data=refund_data)