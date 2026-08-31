from __future__ import annotations

import asyncio
import tempfile
import unittest
from pathlib import Path

from app.designs.artifacts import ArtifactStore
from app.designs.jobs import DesignJobService, DesignJobStore
from app.designs.models import CadDesignRequest, PcbDesignRequest
from app.designs.parsers import parse_cad, parse_pcb


class DesignPipelineTests(unittest.TestCase):
    def test_cad_parser_creates_typed_spec(self):
        spec, questions, assumptions = parse_cad("design a 60 by 40 mm L-bracket, 5 mm thick")
        self.assertEqual(spec.part_type, "l_bracket")
        self.assertEqual(spec.dimensions_mm["arm1"], 60.0)
        self.assertEqual(spec.dimensions_mm["arm2"], 40.0)
        self.assertEqual(spec.material_thickness_mm, 5.0)
        self.assertTrue(assumptions)

    def test_pcb_parser_uses_explicit_components(self):
        spec, _, _ = parse_pcb("design a 4-layer ESP32 board 80 by 50 mm", ["ESP32-WROOM-32", "USB-UART"])
        self.assertEqual(spec.layers, 4)
        self.assertEqual(spec.width_mm, 80.0)
        self.assertEqual(spec.height_mm, 50.0)
        self.assertEqual([component.value for component in spec.components], ["ESP32-WROOM-32", "USB-UART"])

    def test_services_persist_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            store = DesignJobStore(ArtifactStore(Path(directory)))
            service = DesignJobService(store)
            cad_job = asyncio.run(service.create_cad(CadDesignRequest(description="60 40 5 mm L-bracket")))
            pcb_job = asyncio.run(
                service.create_pcb(
                    PcbDesignRequest(
                        description="4-layer ESP32 board 80 by 50 mm",
                        components=["ESP32-WROOM-32", "USB-UART"],
                    )
                )
            )

            self.assertIn(cad_job.status.value, {"ready", "needs_review"})
            self.assertIn(pcb_job.status.value, {"ready", "needs_review"})
            self.assertTrue(any(artifact.kind == "fusion_script" for artifact in cad_job.artifacts))
            self.assertTrue(any(artifact.kind == "kicad_schematic" for artifact in pcb_job.artifacts))
            pcb_artifact = next(artifact for artifact in pcb_job.artifacts if artifact.kind == "kicad_pcb")
            pcb_text = store.artifacts.read(pcb_artifact.id)[1].decode("utf-8")
            self.assertIn("Components: ESP32-WROOM-32, USB-UART", pcb_text)
            self.assertIn("(end 150.000 130.000)", pcb_text)
            self.assertTrue(any(artifact.kind == "kicad_project_bundle" for artifact in pcb_job.artifacts))
            self.assertIsNotNone(store.get(cad_job.job_id))
            self.assertIsNotNone(store.get(pcb_job.job_id))


if __name__ == "__main__":
    unittest.main()
