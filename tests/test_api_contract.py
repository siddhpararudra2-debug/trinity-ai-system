from __future__ import annotations

import unittest
from pathlib import Path

import yaml

from app.main import app


class ApiContractTests(unittest.TestCase):
    def test_openapi_contains_design_paths(self):
        contract_path = Path(__file__).parents[1] / "lib" / "api-spec" / "openapi.yaml"
        contract = yaml.safe_load(contract_path.read_text(encoding="utf-8"))
        for path in ("/designs/cad", "/designs/pcb", "/design-jobs/{job_id}", "/artifacts/{artifact_id}", "/firmware/targets", "/firmware/jobs", "/firmware/jobs/{job_id}", "/vision/ocr", "/collab/sessions/{session_id}", "/ws", "/workflows/plan", "/fusion/jobs/{job_id}/claim", "/fusion/jobs/{job_id}/artifacts", "/fusion/jobs/{job_id}/complete"):
            self.assertIn(path, contract["paths"])

    def test_fastapi_contains_design_routes(self):
        paths = {route.path for route in app.routes}
        self.assertIn("/api/designs/cad", paths)
        self.assertIn("/api/designs/pcb", paths)
        self.assertIn("/api/design-jobs/{job_id}", paths)
        self.assertIn("/api/artifacts/{artifact_id}", paths)
        self.assertIn("/api/firmware/targets", paths)
        self.assertIn("/api/firmware/jobs", paths)
        self.assertIn("/api/firmware/jobs/{job_id}", paths)
        self.assertIn("/api/vision/ocr", paths)
        self.assertIn("/api/collab/sessions/{session_id}", paths)
        self.assertIn("/api/ws", paths)
        self.assertIn("/api/workflows/plan", paths)
        self.assertIn("/api/fusion/jobs/{job_id}/claim", paths)
        self.assertIn("/api/fusion/jobs/{job_id}/artifacts", paths)
        self.assertIn("/api/fusion/jobs/{job_id}/complete", paths)


if __name__ == "__main__":
    unittest.main()
