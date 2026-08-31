from __future__ import annotations

import asyncio
import tempfile
import unittest
from pathlib import Path

from app.designs.artifacts import ArtifactStore
from app.firmware.jobs import FirmwareJobService, FirmwareJobStore
from app.firmware.models import FirmwareRequest
from app.firmware.registry import find_target, list_targets


class FirmwareEngineTests(unittest.TestCase):
    def test_registry_contains_mainstream_mcu_and_flight_targets(self):
        targets = list_targets()
        ids = {target.id for target in targets}
        self.assertIn("esp32-devkitc-esp32-idf", ids)
        self.assertIn("pixhawk-px4-fmu-v6c", ids)
        self.assertIn("pixhawk-ardupilot-cubeorange", ids)
        self.assertTrue(any(target.kind.value == "flight_controller" for target in targets))

    def test_target_resolution_is_explicit(self):
        self.assertEqual(find_target(None, "write ESP32 firmware with UART"), find_target("esp32-devkitc-esp32-idf", ""))
        self.assertIsNone(find_target(None, "write firmware for an unknown development board"))

    def test_firmware_job_persists_project_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            store = FirmwareJobStore(ArtifactStore(Path(directory)))
            service = FirmwareJobService(store)
            job = asyncio.run(service.create(FirmwareRequest(
                description="Write ESP32 firmware with UART and I2C",
                project_name="sensor_node",
                features=["uart", "i2c"],
            )))
            self.assertEqual(job.target.id, "esp32-devkitc-esp32-idf")
            self.assertEqual(job.validation.status, "warnings")
            self.assertTrue(any(artifact.filename == "sensor_node.zip" for artifact in job.artifacts))
            self.assertIsNotNone(store.get(job.job_id))

    def test_flight_controller_requires_review(self):
        with tempfile.TemporaryDirectory() as directory:
            service = FirmwareJobService(FirmwareJobStore(ArtifactStore(Path(directory))))
            job = asyncio.run(service.create(FirmwareRequest(
                target_id="pixhawk-px4-fmu-v6c",
                description="Generate a PX4 flight controller module",
                safety_mode="flight",
            )))
            self.assertEqual(job.status.value, "needs_review")
            self.assertTrue(any(check.name == "flight-safety" for check in job.validation.checks))

    def test_unknown_target_does_not_guess(self):
        with tempfile.TemporaryDirectory() as directory:
            service = FirmwareJobService(FirmwareJobStore(ArtifactStore(Path(directory))))
            job = asyncio.run(service.create(FirmwareRequest(
                target_id="made-up-board",
                description="Write firmware for a made-up board",
            )))
            self.assertEqual(job.status.value, "needs_input")
            self.assertFalse(job.artifacts)
            self.assertTrue(job.questions)


if __name__ == "__main__":
    unittest.main()
