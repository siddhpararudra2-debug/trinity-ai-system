from __future__ import annotations

import asyncio
import unittest

from app.firmware.analysis import analysis_checks, estimate_resources
from app.firmware.generator import FirmwareGenerator
from app.firmware.knowledge import extract_hardware_identifier, lookup_hardware
from app.firmware.llm import parse_structured_response
from app.firmware.models import FirmwareRequest, FirmwareSpec
from app.firmware.registry import find_target
from app.firmware.universal import FirmwareEngine


class UniversalFirmwareTests(unittest.TestCase):
    def test_hardware_identifier_and_knowledge_base(self):
        self.assertEqual(extract_hardware_identifier("generate UART firmware for STM32F407VG"), "STM32F407VG")
        facts = lookup_hardware("STM32F407VG")
        self.assertEqual(facts["architecture"], "ARM Cortex-M4")
        self.assertGreater(facts["flash_bytes"], 0)

    def test_paradigm_detection_creates_supported_targets(self):
        cases = {
            "write MicroPython code for an ESP32": "generic-micropython",
            "create a synthesizable Verilog FPGA module": "generic-fpga",
            "write an embedded Rust no_std driver": "generic-embedded-rust",
            "create a Linux kernel module driver": "generic-linux-driver",
            "write ARM Thumb assembly startup": "generic-assembly",
        }
        for description, target_id in cases.items():
            target = find_target(None, description)
            self.assertIsNotNone(target)
            self.assertEqual(target.id, target_id)

    def test_generator_supports_universal_paradigms(self):
        generator = FirmwareGenerator()
        for description in (
            "write MicroPython code for an ESP32",
            "create a synthesizable Verilog FPGA module",
            "write an embedded Rust no_std driver",
            "create a Linux kernel module driver",
            "write ARM Thumb assembly startup",
        ):
            target = find_target(None, description)
            spec = FirmwareSpec(target_id=target.id, description=description, framework=target.framework, language=target.language)
            project = generator.generate(target, spec)
            self.assertIn("README.md", project.files)
            self.assertGreaterEqual(len(project.files), 4)

    def test_structured_llm_output_is_parsed_without_execution(self):
        parsed = parse_structured_response('{"files":{"src/main.c":"int main(void) {}"},"dependencies":["vendor-hal"]}')
        self.assertEqual(parsed["files"]["src/main.c"], "int main(void) {}")
        marked = parse_structured_response("FILE: main.py\nprint('ok')\nDEPENDENCIES: micropython")
        self.assertEqual(marked["files"]["main.py"], "print('ok')\n")

    def test_analysis_finds_critical_security_pattern_and_estimates_resources(self):
        target = find_target(None, "write STM32F407VG bare-metal firmware")
        spec = FirmwareSpec(target_id=target.id, description="write STM32F407VG bare-metal firmware", framework=target.framework, language=target.language)
        files = {"src/main.c": 'const char *api_key = "hardcoded-secret";\nint main(void) { while (1) {} }\n'}
        checks, findings, dependencies = analysis_checks(spec, target, files)
        self.assertTrue(any(item.severity == "critical" for item in findings))
        self.assertEqual(len(checks), 3)
        estimate = estimate_resources(files, target, spec)
        self.assertGreater(estimate.flash_bytes, 0)
        self.assertGreater(estimate.ram_bytes, 0)

    def test_engine_prompt_includes_previous_code_context(self):
        engine = FirmwareEngine()
        target = find_target(None, "write MicroPython code for an ESP32")
        request = FirmwareRequest(description="add a watchdog", previous_code="print('previous')")
        _system, user_prompt = engine.build_prompt(request, target)
        self.assertIn("previous", user_prompt)
        self.assertEqual(engine.detect_submodule(request, target), "micropython")


if __name__ == "__main__":
    unittest.main()
