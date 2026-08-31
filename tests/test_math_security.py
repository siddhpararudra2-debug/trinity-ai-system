from __future__ import annotations

import asyncio
import unittest

from app.engines.math_engine import MathEngine, _safe_parse
from app.unified_router.orchestrator import TrinityOrchestrator


class MathSecurityTests(unittest.TestCase):
    def test_safe_math_accepts_whitelisted_expression(self):
        result = _safe_parse("sin(x) + 2*pi")
        self.assertIn("sin(x)", str(result))

    def test_safe_math_rejects_unknown_identifier(self):
        with self.assertRaises(ValueError):
            _safe_parse("secret_function(x)")

    def test_safe_math_rejects_dynamic_execution_tokens(self):
        for expression in ("__import__(os)", "open(1)", "eval(x)", "x; import os"):
            with self.subTest(expression=expression):
                with self.assertRaises(ValueError):
                    _safe_parse(expression)

    def test_math_engine_returns_user_safe_error(self):
        result = asyncio.run(MathEngine().process("eval(x)"))
        self.assertEqual(result["engine"], "math")
        self.assertIn("Invalid expression", result["result"])

    def test_manual_engine_override_bypasses_keyword_detection(self):
        response = asyncio.run(TrinityOrchestrator().route("show a Bell state", engine_override="math"))
        self.assertEqual(response["engine"], "math")


if __name__ == "__main__":
    unittest.main()
