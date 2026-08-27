from __future__ import annotations

import os
import unittest
from datetime import datetime
from unittest.mock import patch

from app.auth import decode_access_token, hash_password, issue_access_token, verify_password
from app.main import app
from app.models import User


def route_paths(routes, prefix=""):
    for route in routes:
        included_router = getattr(route, "original_router", None)
        if included_router is not None:
            context = getattr(route, "include_context", None)
            yield from route_paths(included_router.routes, prefix + getattr(context, "prefix", ""))
            continue
        path = getattr(route, "path", None)
        if path:
            yield prefix + path


class AuthTests(unittest.TestCase):
    def test_password_hash_is_salted_and_verifies(self):
        first = hash_password("correct horse battery staple")
        second = hash_password("correct horse battery staple")
        self.assertNotEqual(first, second)
        self.assertTrue(verify_password("correct horse battery staple", first))
        self.assertFalse(verify_password("wrong password", first))

    def test_bearer_token_round_trip_and_tamper_rejection(self):
        with patch.dict(os.environ, {"TRINITY_AUTH_SECRET": "unit-test-secret"}):
            token = issue_access_token(42, expires_in=300)
            self.assertEqual(decode_access_token(token), 42)
            encoded, signature = token.split(".", 1)
            with self.assertRaises(Exception):
                decode_access_token(encoded + "x." + signature)

    def test_user_payload_exposes_no_password_hash(self):
        from app.auth import user_payload

        user = User(id=7, email="user@example.com", role="user", created_at=datetime(2026, 1, 1))
        payload = user_payload(user)
        self.assertEqual(payload["id"], 7)
        self.assertNotIn("password_hash", payload)

    def test_auth_routes_are_mounted(self):
        paths = set(route_paths(app.routes))
        self.assertTrue({"/api/auth/register", "/api/auth/login", "/api/auth/me"}.issubset(paths))


if __name__ == "__main__":
    unittest.main()

