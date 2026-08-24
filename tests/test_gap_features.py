from __future__ import annotations

import asyncio
import os
import unittest
from unittest.mock import patch

from app.engines.vision_engine import VisionEngine
from app.fusion_worker import FusionAuthError, make_worker_token, verify_worker_token
from app.security import require_api_key_for_request
from fastapi import HTTPException
from starlette.requests import Request
from app.workflows import build_workflow_plan


class GapFeatureTests(unittest.TestCase):
    @staticmethod
    def _request(path: str, headers: list[tuple[bytes, bytes]] | None = None) -> Request:
        return Request({"type": "http", "method": "GET", "path": path, "headers": headers or [], "query_string": b"", "scheme": "http", "server": ("testserver", 80), "client": ("testclient", 1)})

    def test_api_key_boundary_is_optional_in_development_and_enforced_when_configured(self):
        with patch.dict(os.environ, {"TRINITY_API_KEY": "secret"}):
            with self.assertRaises(HTTPException):
                require_api_key_for_request(self._request("/api/chat"))
            require_api_key_for_request(self._request("/api/chat", [(b"x-trinity-api-key", b"secret")]))
            require_api_key_for_request(self._request("/api/healthz"))

    def test_workflow_plan_orders_multiple_engines(self):
        plan = build_workflow_plan("research an ESP32 PCB and write its firmware")
        self.assertEqual([step.engine for step in plan.steps], ["literature", "maker_pcb", "firmware"])
        self.assertEqual(plan.steps[1].depends_on, ["step_1"])

    def test_fusion_worker_token_round_trip(self):
        with patch.dict(os.environ, {"TRINITY_FUSION_WORKER_SECRET": "x" * 48}):
            token = make_worker_token("cad_123", "worker_01", "a" * 64, 4102444800)
            self.assertEqual(verify_worker_token(token, "cad_123", "a" * 64), "worker_01")
            with self.assertRaises(FusionAuthError):
                verify_worker_token(token, "cad_123", "b" * 64)

    def test_vision_rejects_invalid_image_bytes(self):
        result = asyncio.run(VisionEngine().process(image_data=b"not an image"))
        self.assertEqual(result["status"], "error")
        self.assertIn("could not be decoded", result["message"])


if __name__ == "__main__":
    unittest.main()
