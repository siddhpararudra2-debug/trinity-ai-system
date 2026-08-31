"""Small, explicit hardware knowledge base used to enrich firmware generation."""
from __future__ import annotations

import re
from typing import Any

HARDWARE_KNOWLEDGE: dict[str, dict[str, Any]] = {
    "STM32F407VG": {"architecture": "ARM Cortex-M4", "flash_bytes": 1_048_576, "ram_bytes": 196_608, "clock_hz": 168_000_000, "peripherals": ["gpio", "uart", "i2c", "spi", "adc", "timers", "dma", "can"]},
    "STM32F411CEU6": {"architecture": "ARM Cortex-M4", "flash_bytes": 524_288, "ram_bytes": 131_072, "clock_hz": 100_000_000, "peripherals": ["gpio", "uart", "i2c", "spi", "adc", "timers", "dma"]},
    "STM32F103C8T6": {"architecture": "ARM Cortex-M3", "flash_bytes": 65_536, "ram_bytes": 20_480, "clock_hz": 72_000_000, "peripherals": ["gpio", "uart", "i2c", "spi", "adc", "timers"]},
    "STM32F746ZG": {"architecture": "ARM Cortex-M7", "flash_bytes": 1_048_576, "ram_bytes": 320_000, "clock_hz": 216_000_000, "peripherals": ["gpio", "uart", "i2c", "spi", "adc", "timers", "dma", "usb"]},
    "STM32H743": {"architecture": "ARM Cortex-M7", "flash_bytes": 2_097_152, "ram_bytes": 1_048_576, "clock_hz": 480_000_000, "peripherals": ["gpio", "uart", "i2c", "spi", "adc", "timers", "dma", "can", "usb"]},
    "ESP32": {"architecture": "Xtensa LX6", "flash_bytes": 4_194_304, "ram_bytes": 520_192, "clock_hz": 240_000_000, "peripherals": ["gpio", "uart", "i2c", "spi", "adc", "pwm", "wifi", "bluetooth"]},
    "ESP32-S3": {"architecture": "Xtensa LX7", "flash_bytes": 8_388_608, "ram_bytes": 524_288, "clock_hz": 240_000_000, "peripherals": ["gpio", "uart", "i2c", "spi", "adc", "pwm", "wifi", "bluetooth", "usb"]},
    "ATMEGA328P": {"architecture": "AVR 8-bit", "flash_bytes": 32_768, "ram_bytes": 2_048, "clock_hz": 16_000_000, "peripherals": ["gpio", "uart", "i2c", "spi", "adc", "pwm"]},
    "RP2040": {"architecture": "ARM Cortex-M0+", "flash_bytes": None, "ram_bytes": 264_192, "clock_hz": 133_000_000, "peripherals": ["gpio", "uart", "i2c", "spi", "adc", "pwm", "pio", "usb"]},
    "NRF52840": {"architecture": "ARM Cortex-M4F", "flash_bytes": 1_048_576, "ram_bytes": 262_144, "clock_hz": 64_000_000, "peripherals": ["gpio", "uart", "i2c", "spi", "adc", "pwm", "ble", "usb"]},
    "PIC16F877A": {"architecture": "PIC 8-bit", "flash_bytes": 14_336, "ram_bytes": 368, "clock_hz": 20_000_000, "peripherals": ["gpio", "uart", "i2c", "spi", "adc", "timers"]},
    "LPC1768": {"architecture": "ARM Cortex-M3", "flash_bytes": 524_288, "ram_bytes": 32_768, "clock_hz": 100_000_000, "peripherals": ["gpio", "uart", "i2c", "spi", "adc", "pwm", "can"]},
}

_IDENTIFIER_PATTERNS = (
    r"STM32[A-Z]?\d{3}[A-Z0-9]{2,}", r"ESP32(?:-[A-Z0-9]+)?", r"ATMEGA\d+[A-Z0-9]*",
    r"RP2040", r"NRF\d{4,6}", r"LPC\d+", r"PIC\d+[A-Z]?\d+", r"ICE40[A-Z0-9-]+", r"XC7[A-Z0-9-]+",
)


def normalize_identifier(identifier: str | None) -> str | None:
    if not identifier:
        return None
    return re.sub(r"[^A-Z0-9-]", "", identifier.upper()) or None


def extract_hardware_identifier(text: str) -> str | None:
    for pattern in _IDENTIFIER_PATTERNS:
        match = re.search(pattern, text, re.IGNORECASE)
        if match:
            value = normalize_identifier(match.group(0))
            if value:
                return value
    return None


def lookup_hardware(identifier: str | None) -> dict[str, Any] | None:
    normalized = normalize_identifier(identifier)
    if not normalized:
        return None
    exact = HARDWARE_KNOWLEDGE.get(normalized)
    if exact:
        return {"id": normalized, **exact}
    for known, value in HARDWARE_KNOWLEDGE.items():
        if normalized.startswith(known) or known.startswith(normalized):
            return {"id": known, **value}
    return None


def validate_pin_assignments(pin_assignments: list[Any], target: dict[str, Any] | None) -> list[dict[str, Any]]:
    findings: list[dict[str, Any]] = []
    seen_pins: dict[str, str] = {}
    for assignment in pin_assignments:
        pin = str(assignment.pin).upper()
        if pin in seen_pins:
            findings.append({"rule": "pin-conflict", "severity": "critical", "message": f"Pin {pin} is assigned to both {seen_pins[pin]} and {assignment.name}.", "remediation": "Assign each physical pin to one function."})
        seen_pins[pin] = assignment.name
        if target and target.get("pin_count") is not None:
            numeric = re.fullmatch(r"(?:GPIO)?(\d+)", pin)
            if numeric and int(numeric.group(1)) >= int(target["pin_count"]):
                findings.append({"rule": "pin-range", "severity": "critical", "message": f"Pin {pin} is outside the known package pin range.", "remediation": "Verify the exact package pin map before flashing."})
    return findings
