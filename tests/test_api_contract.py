from __future__ import annotations

import unittest
from pathlib import Path

import yaml

from app.main import app


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


class ApiContractTests(unittest.TestCase):
    def test_openapi_contains_design_paths(self):
        contract_path = Path(__file__).parents[1] / "lib" / "api-spec" / "openapi.yaml"
        contract = yaml.safe_load(contract_path.read_text(encoding="utf-8"))
        for path in ("/auth/register", "/auth/login", "/auth/me", "/auth/users", "/metrics", "/readyz", "/jobs", "/jobs/{job_id}", "/jobs/{job_id}/cancel", "/designs/cad", "/designs/pcb", "/design-jobs/{job_id}", "/artifacts/{artifact_id}", "/firmware/targets", "/firmware/jobs", "/firmware/jobs/{job_id}", "/vision/ocr", "/collab/sessions/{session_id}", "/ws", "/workflows/plan", "/workflows/execute", "/workflows/runs/{run_id}", "/workflows/runs/{run_id}/approve", "/workflows/runs/{run_id}/cancel", "/fusion/jobs/{job_id}/claim", "/fusion/jobs/{job_id}/artifacts", "/fusion/jobs/{job_id}/complete", "/kicad/jobs/{job_id}/artifacts", "/kicad/jobs/{job_id}/complete"):
            self.assertIn(path, contract["paths"])

    def test_fastapi_contains_design_routes(self):
        paths = set(route_paths(app.routes))
        self.assertIn("/api/auth/register", paths)
        self.assertIn("/api/auth/login", paths)
        self.assertIn("/api/auth/me", paths)
        self.assertIn("/api/auth/users", paths)
        self.assertIn("/api/metrics", paths)
        self.assertIn("/api/readyz", paths)
        self.assertIn("/api/jobs", paths)
        self.assertIn("/api/jobs/{job_id}", paths)
        self.assertIn("/api/jobs/{job_id}/cancel", paths)
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
        self.assertIn("/api/workflows/execute", paths)
        self.assertIn("/api/workflows/runs/{run_id}", paths)
        self.assertIn("/api/workflows/runs/{run_id}/approve", paths)
        self.assertIn("/api/workflows/runs/{run_id}/cancel", paths)
        self.assertIn("/api/fusion/jobs/{job_id}/claim", paths)
        self.assertIn("/api/fusion/jobs/{job_id}/artifacts", paths)
        self.assertIn("/api/fusion/jobs/{job_id}/complete", paths)
        self.assertIn("/api/kicad/jobs/{job_id}/artifacts", paths)
        self.assertIn("/api/kicad/jobs/{job_id}/complete", paths)


if __name__ == "__main__":
    unittest.main()
