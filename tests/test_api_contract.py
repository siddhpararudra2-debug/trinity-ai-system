from __future__ import annotations

import unittest
from pathlib import Path

import yaml

from app.main import app


class ApiContractTests(unittest.TestCase):
    def test_openapi_contains_design_paths(self):
        contract_path = Path(__file__).parents[1] / "lib" / "api-spec" / "openapi.yaml"
        contract = yaml.safe_load(contract_path.read_text(encoding="utf-8"))
        for path in ("/designs/cad", "/designs/pcb", "/design-jobs/{job_id}", "/artifacts/{artifact_id}"):
            self.assertIn(path, contract["paths"])

    def test_fastapi_contains_design_routes(self):
        paths = {route.path for route in app.routes}
        self.assertIn("/api/designs/cad", paths)
        self.assertIn("/api/designs/pcb", paths)
        self.assertIn("/api/design-jobs/{job_id}", paths)
        self.assertIn("/api/artifacts/{artifact_id}", paths)


if __name__ == "__main__":
    unittest.main()
