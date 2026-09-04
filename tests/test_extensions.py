from __future__ import annotations

import asyncio
import unittest

from app.engines.general_ai import GeneralAIEngine
from app.pipeline.executor import PipelineExecutor
from app.pipelines.paper_to_code import PaperToCodePipeline
from app.sourcing.bom_service import BomSourcingService
from app.workflows import build_workflow_plan


class ExtensionFeatureTests(unittest.TestCase):
    def test_general_ai_offline_fallback(self):
        result = asyncio.run(GeneralAIEngine().process("Explain Ohm's law"))
        self.assertEqual(result["engine"], "general_ai")
        self.assertIn("content", result)

    def test_paper_to_code_extracts_equation(self):
        text = "The model uses $$E = mc^2$$ for energy."
        result = asyncio.run(PaperToCodePipeline().run(text))
        self.assertGreaterEqual(result["equations_found"], 1)
        self.assertTrue(result["blocks"])

    def test_bom_sourcing_matches_catalog_part(self):
        result = asyncio.run(
            BomSourcingService().from_components(
                [{"reference": "U1", "value": "ESP32-WROOM-32", "quantity": 1}]
            )
        )
        self.assertEqual(result["summary"]["matched"], 1)

    def test_pipeline_plan_includes_paper_to_code(self):
        plan = build_workflow_plan("find papers and convert equations to sympy code")
        engines = [step.engine for step in plan.steps]
        self.assertIn("literature", engines)
        self.assertIn("paper_to_code", engines)

    def test_pipeline_executor_runs_math_step(self):
        plan = {
            "objective": "solve x+1=0",
            "mode": "deterministic",
            "steps": [{"id": "step_1", "engine": "math", "objective": "solve x+1=0", "depends_on": []}],
        }
        result = asyncio.run(PipelineExecutor().execute_plan(plan))
        self.assertEqual(result["status"], "completed")
        self.assertEqual(result["steps"][0]["engine"], "math")


if __name__ == "__main__":
    unittest.main()
