# Authentication — email + password (argon2), email verification, reset, login rate limits
# Refresh token in Windows Credential Manager.

import hashlib
import secrets
from datetime import datetime, timedelta, timezone

# Argon2 password hashing — requires: pip install argon2-cryptography
# from argon2 import PasswordHasher
# password_hasher = PasswordHasher()

# JWT tokens — requires: pip install pyjwt
# import jwt

# Rate limiting — simple in-memory store; replace with Redis in production
# login_attempts = {}  # email -> count + timestamp

# Windows Credential Manager integration
# import ctypes
# def store_refresh_token(token): ...
# def get_refresh_token(): ...

# User model placeholder
# USER_TABLE = {}  # email -> {hashed_password, created_at, is_verified, ...}

# Password verification (argon2)
# def verify_password(password, hashed): 
#     return password_hasher.verify(password, hashed)

# Password hashing
# def hash_password(password): 
#     return password_hasher.hash(password)

# Token generation
# def create_jwt_token(email, role="user", exp_days=7):
#     issued_at = datetime.now(timezone.utc)
#     expires_at = issued_at + timedelta(days=exp_days)
#     token = jwt.encode({
#         "sub": email,
#         "iat": issued_at,
#         "exp": expires_at,
#         "role": role,
#     }, RAZORPAY_KEY_SECRET, algorithm="HS256")
#     return token

# Token decoding
# def decode_jwt_token(token):
#     return jwt.decode(token, RAZORPAY_KEY_SECRET, algorithms=["HS256"])