"""Whiteboard Diagram-to-PCB — Vision recognition bridged to PCB generation intent."""
from __future__ import annotations

import re
from typing import Any

from app.engines.vision_engine import VisionEngine


_COMPONENT_HINTS = {
    "r": "resistor",
    "c": "capacitor",
    "l": "inductor",
    "d": "diode",
    "q": "transistor",
    "u": "ic",
    "esp32": "ESP32 module",
    "stm32": "STM32 MCU",
    "arduino": "Arduino-compatible board",
}


class WhiteboardPcbPipeline:
    def __init__(self) -> None:
        self._vision = VisionEngine()

    async def run(self, image_data: bytes, objective: str = "") -> dict[str, Any]:
        vision = await self._vision.process(objective or "Extract circuit diagram text and labels", image_data=image_data)
        if vision.get("status") == "error":
            return {
                "engine": "whiteboard_pcb",
                "status": "failed",
                "message": vision.get("message", "Vision extraction failed"),
                "vision": vision,
            }
        text_blob = " ".join(
            filter(
                None,
                [
                    vision.get("recognized_text", ""),
                    vision.get("latex", ""),
                    vision.get("message", ""),
                ],
            )
        )
        components = self._infer_components(text_blob)
        connectivity_notes = self._infer_connectivity(text_blob)
        pcb_description = self._build_pcb_description(objective, components, connectivity_notes, text_blob)
        uncertainties = []
        if not components:
            uncertainties.append("No recognizable component references were extracted; manual review required.")
        if vision.get("confidence", 1.0) < 0.6:
            uncertainties.append("Low OCR confidence — verify symbol identities before fabrication.")
        return {
            "engine": "whiteboard_pcb",
            "status": "completed",
            "vision": vision,
            "circuit_graph": {
                "components": components,
                "connectivity_notes": connectivity_notes,
            },
            "pcb_description": pcb_description,
            "uncertainties": uncertainties,
            "next_step": "maker_pcb",
        }

    def _infer_components(self, text: str) -> list[dict[str, str]]:
        lower = text.lower()
        refs = re.findall(r"\b([RUCLDQJ]\d+)\b", text, flags=re.IGNORECASE)
        components: list[dict[str, str]] = []
        seen: set[str] = set()
        for ref in refs:
            key = ref.upper()
            if key in seen:
                continue
            seen.add(key)
            prefix = key[0].lower()
            components.append({"ref": key, "kind": _COMPONENT_HINTS.get(prefix, "component")})
        for keyword, kind in _COMPONENT_HINTS.items():
            if keyword in lower and keyword.upper() not in seen:
                components.append({"ref": keyword.upper(), "kind": kind})
        return components

    @staticmethod
    def _infer_connectivity(text: str) -> list[str]:
        notes: list[str] = []
        nets = re.findall(r"\b(VCC|GND|3V3|5V|SDA|SCL|MISO|MOSI|SCK|TX|RX)\b", text, flags=re.IGNORECASE)
        for net in sorted(set(n.upper() for n in nets)):
            notes.append(f"Net label detected: {net}")
        if "->" in text or "—" in text or "-" in text:
            notes.append("Wire segments inferred from diagram strokes (requires human confirmation).")
        return notes

    @staticmethod
    def _build_pcb_description(objective: str, components: list[dict], notes: list[str], raw_text: str) -> str:
        parts = [objective or "Whiteboard-derived KiCad schematic"]
        if components:
            parts.append("Components: " + ", ".join(f"{c['ref']} ({c['kind']})" for c in components))
        if notes:
            parts.append("Connectivity: " + "; ".join(notes))
        if raw_text.strip():
            parts.append("Extracted labels: " + raw_text.strip()[:400])
        return ". ".join(parts)
