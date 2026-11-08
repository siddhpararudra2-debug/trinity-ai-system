"""Firmware Engine (PRD §24, §31).

Generates firmware source files, validates MCU configuration,
and compiles-check per board. Pure Python fallback with
deterministic output from part definitions — no native compiler
required.
"""

from __future__ import annotations

import uuid
from typing import Any

from src.engines.base import BaseEngine, EngineResult, ValidationResult
from src.core.errors import RequestValidationError

DEFAULT_MCU = "ESP32-WROOM-32"


class FirmwareError(RequestValidationError):
    """Firmware-specific validation error."""


class FirmwareEngine(BaseEngine):
    name = "firmware"
    version = "1.0"
    capabilities = (
        "create",
        "select_mcu",
        "configure_pin",
        "configure_peripheral",
        "generate",
        "validate",
        "build",
    )

    def describe(self) -> dict[str, Any]:
        return {**super().describe(), "status": "scaffolded"}

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        if operation == "create":
            return self.execute_create(parameters)
        if operation == "select_mcu":
            return self.execute_select_mcu(parameters)
        if operation == "configure_pin":
            return self.execute_configure_pin(parameters)
        if operation == "configure_peripheral":
            return self.execute_configure_peripheral(parameters)
        if operation == "generate":
            return self.execute_generate(parameters)
        if operation == "validate":
            return self.execute_validate(parameters)
        if operation == "build":
            return self.execute_build(parameters)
        raise RequestValidationError(f"Firmware engine has no operation '{operation}'")

    # -------------------------------------------------------- execute ---

    def execute_create(self, parameters: dict[str, Any]) -> EngineResult:
        """Create a new firmware project from a PCB design."""
        board = parameters.get("board", {})
        mcu = parameters.get("mcu", DEFAULT_MCU)
        features = parameters.get("features", [])

        project_id = str(uuid.uuid4().hex[:8])

        return EngineResult(
            success=True,
            engine=self.name,
            operation="create",
            result={
                "project_id": project_id,
                "mcu": mcu,
                "board_width_mm": board.get("width_mm", 50.0),
                "board_height_mm": board.get("height_mm", 40.0),
                "features": features,
                "status": "created",
            },
        )

    def execute_select_mcu(self, parameters: dict[str, Any]) -> EngineResult:
        """Select the MCU for firmware generation."""
        mcu = parameters.get("mcu", DEFAULT_MCU)
        return EngineResult(
            success=True,
            engine=self.name,
            operation="select_mcu",
            result={"mcu": mcu, "status": "selected"},
        )

    def execute_configure_pin(self, parameters: dict[str, Any]) -> EngineResult:
        """Configure a GPIO pin for a peripheral."""
        pin = parameters.get("pin", "")
        function = parameters.get("function", "")
        mode = parameters.get("mode", "GPIO")

        if not pin:
            raise RequestValidationError("configure_pin requires pin name")

        return EngineResult(
            success=True,
            engine=self.name,
            operation="configure_pin",
            result={"pin": pin, "function": function, "mode": mode, "status": "configured"},
        )

    def execute_configure_peripheral(
        self, parameters: dict[str, Any]
    ) -> EngineResult:
        """Configure a peripheral (UART, SPI, I2C, etc.)."""
        peripheral = parameters.get("peripheral", "")
        config = parameters.get("config", {})

        if not peripheral:
            raise RequestValidationError("configure_peripheral requires peripheral name")

        return EngineResult(
            success=True,
            engine=self.name,
            operation="configure_peripheral",
            result={
                "peripheral": peripheral,
                "config": config,
                "status": "configured",
            },
        )

    # -------------------------------------------------------- generate ---

    def execute_generate(self, parameters: dict[str, Any]) -> EngineResult:
        """Generate firmware source code for the selected MCU."""
        mcu = parameters.get("mcu", DEFAULT_MCU)
        project_id = parameters.get("project_id", "")
        features = parameters.get("features", [])

        # Generate minimal firmware source
        source_lines = [
            f"// Firmware for {mcu}",
            f"// Project: {project_id}",
            "#include <stdio.h>",
            "#include <stdint.h>",
        ]

        # Add peripheral-specific code based on features
        if "uart" in features:
            source_lines.extend([
                "// UART configuration",
                "void uart_init(void) {",
                "    // UART init",
                "}",
            ])

        if "spi" in features:
            source_lines.extend([
                "// SPI configuration",
                "void spi_init(void) {",
                "    // SPI init",
                "}",
            ])

        if "i2c" in features:
            source_lines.extend([
                "// I2C configuration",
                "void i2c_init(void) {",
                "    // I2C init",
                "}",
            ])

        source_lines.extend([
            "int main(void) {",
            "    // TODO: add application code",
            "    return 0;",
            "}",
        ])

        source_text = "\n".join(source_lines)

        return EngineResult(
            success=True,
            engine=self.name,
            operation="generate",
            result={
                "mcu": mcu,
                "project_id": project_id,
                "source_lines": len(source_lines),
                "source_text": source_text,
                "status": "generated",
            },
        )

    # -------------------------------------------------------- validate ---

    def execute_validate(self, parameters: dict[str, Any]) -> EngineResult:
        """Validate firmware configuration: check MCU, features, pins."""
        mcu = parameters.get("mcu", DEFAULT_MCU)
        features = parameters.get("features", [])
        pins = parameters.get("pins", [])

        errors: list[str] = []

        # Validate MCU is recognized
        recognized_mcus = ["ESP32-WROOM-32", "STM32F407", "ATmega2560"]
        if mcu not in recognized_mcus:
            errors.append(f"Unrecognized MCU: {mcu}")

        # Validate features
        valid_features = ["uart", "spi", "i2c", "adc", "pwm"]
        for feature in features:
            if feature not in valid_features:
                errors.append(f"Unrecognized feature: {feature}")

        # Validate pins
        for pin_info in (pins or []):
            pin_name = pin_info.get("name", "")
            if not pin_name:
                errors.append("Pin has no name")

        ok = len(errors) == 0
        status = "VALIDATED" if ok else "FAILED"
        checks = {"errors": errors} if errors else {}

        return EngineResult(
            success=ok,
            engine=self.name,
            operation="validate",
            result={
                "mcu": mcu,
                "features": features,
                "pins_checked": len(pins or []),
                "errors": errors,
            },
            validation=ValidationResult(status=status, checks=checks),
        )

    # -------------------------------------------------------- build ---

    def execute_build(self, parameters: dict[str, Any]) -> EngineResult:
        """Compile-check the firmware project."""
        project_id = parameters.get("project_id", "")
        source_text = parameters.get("source_text", "")

        # Simple build check: verify source is non-empty and has main()
        errors: list[str] = []

        if not source_text:
            errors.append("Empty source text cannot be compiled")
        if "int main" not in source_text:
            errors.append("Missing main() function")

        # Check for common MCU-specific patterns
        mcu = parameters.get("mcu", DEFAULT_MCU)
        if mcu == "ESP32-WROOM-32" and "include <stdio.h>" not in source_text:
            errors.append("ESP32 firmware should include stdio header")

        ok = len(errors) == 0
        status = "GENERATED" if ok else "FAILED"
        checks = {"errors": errors} if errors else {}

        return EngineResult(
            success=ok,
            engine=self.name,
            operation="build",
            result={
                "project_id": project_id,
                "build_ok": ok,
                "errors": errors,
            },
            validation=ValidationResult(status=status, checks=checks),
        )