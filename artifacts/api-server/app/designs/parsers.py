"""Small deterministic parsers for the first CAD/PCB design workflow.

The parsers intentionally return clarification questions for safety-critical
omissions instead of claiming that a production design is fully specified.
"""
from __future__ import annotations

import re
from typing import Any

from app.designs.models import (
    CadDesignSpec,
    ComponentSpec,
    HoleSpec,
    NetSpec,
    PcbDesignSpec,
)

_NUMBER = r"(\d+(?:\.\d+)?)"


def _numbers(text: str) -> list[float]:
    return [float(value) for value in re.findall(_NUMBER, text)]


def parse_cad(description: str, overrides: dict[str, Any] | None = None) -> tuple[CadDesignSpec, list[str], list[str]]:
    text = description.lower()
    nums = _numbers(text)
    if "housing" in text or "enclosure" in text or "box" in text:
        part_type = "housing"
    elif "gear" in text or "sprocket" in text:
        part_type = "gear"
    elif "shaft" in text or "axle" in text or "rod" in text:
        part_type = "shaft"
    else:
        part_type = "l_bracket"

    units = "in" if re.search(r"\bin(?:ch|ches)?\b", text) else "mm"
    if part_type == "l_bracket":
        dims = {
            "arm1": nums[0] if len(nums) > 0 else 50.0,
            "arm2": nums[1] if len(nums) > 1 else 50.0,
        }
    elif part_type == "housing":
        dims = {
            "width": nums[0] if len(nums) > 0 else 80.0,
            "depth": nums[1] if len(nums) > 1 else 60.0,
            "height": nums[2] if len(nums) > 2 else 40.0,
        }
    elif part_type == "gear":
        dims = {"teeth": nums[0] if len(nums) > 0 else 24, "module": nums[1] if len(nums) > 1 else 1.5}
    else:
        dims = {"diameter": nums[0] if len(nums) > 0 else 20.0, "length": nums[1] if len(nums) > 1 else 100.0}

    thickness_match = re.search(rf"(?:thick(?:ness)?|wall)\s*(?:of|=|:)??\s*{_NUMBER}\s*mm?", text)
    thickness = float(thickness_match.group(1)) if thickness_match else (nums[2] if part_type == "l_bracket" and len(nums) > 2 else 5.0)
    hole_match = re.search(rf"{_NUMBER}\s*mm?\s*(?:mounting\s*)?holes?", text)
    hole_count = int(float(hole_match.group(1))) if hole_match else 0
    diameter_match = re.search(rf"(?:hole|holes).*?{_NUMBER}\s*mm", text)
    hole_diameter = float(diameter_match.group(1)) if diameter_match else 4.0
    holes = [HoleSpec(x_mm=0.0, y_mm=0.0, diameter_mm=hole_diameter) for _ in range(max(hole_count, 0))]

    raw: dict[str, Any] = {
        "part_type": part_type,
        "units": units,
        "dimensions_mm": dims,
        "material_thickness_mm": thickness,
        "holes": holes,
    }
    if overrides:
        raw.update({key: value for key, value in overrides.items() if key in CadDesignSpec.model_fields})
    spec = CadDesignSpec.model_validate(raw)

    questions: list[str] = []
    assumptions = ["Dimensions default to millimetres unless the prompt explicitly uses inches."]
    if not nums:
        questions.append("Please confirm the primary dimensions before manufacturing or exporting this part.")
    if not hole_count and part_type == "l_bracket":
        assumptions.append("No mounting-hole pattern was specified; the template default will be used.")
    if units == "in":
        questions.append("Please confirm whether the numeric dimensions are inch values; the Fusion template currently normalizes its internal units to millimetres.")
    return spec, questions, assumptions


def _component_from_text(value: str, index: int) -> ComponentSpec:
    token = value.strip()
    upper = token.upper()
    if "ESP32" in upper:
        return ComponentSpec(
            reference=f"U{index}", value=token, symbol="RF_Module:ESP32-WROOM-32", footprint="RF_Module:ESP32-WROOM-32"
        )
    if "USB" in upper or "CP210" in upper or "CH340" in upper:
        return ComponentSpec(reference=f"U{index}", value=token, symbol="Interface_USB:USB-UART", footprint="Package_QFN:QFN-20-1EP-4x4mm-P0.5mm")
    if "CAP" in upper or upper.startswith("C"):
        return ComponentSpec(reference=f"C{index}", value=token, symbol="Device:C", footprint="Capacitor_SMD:C_0603_1608Metric")
    if "RES" in upper or upper.startswith("R"):
        return ComponentSpec(reference=f"R{index}", value=token, symbol="Device:R", footprint="Resistor_SMD:R_0603_1608Metric")
    return ComponentSpec(reference=f"U{index}", value=token, symbol="Connector_Generic:Conn_01x04", footprint="Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical")


def parse_pcb(
    description: str,
    components: list[str] | None = None,
    requested_spec: PcbDesignSpec | None = None,
    outputs: list[str] | None = None,
) -> tuple[PcbDesignSpec, list[str], list[str]]:
    text = description.lower()
    nums = _numbers(text)
    if requested_spec is not None:
        spec = requested_spec.model_copy(deep=True)
        if outputs:
            spec.outputs = outputs  # type: ignore[assignment]
        return spec, [], ["PCB dimensions and electrical intent were supplied through the typed request."]

    board_name = "trinity_esp32" if "esp32" in text else "trinity_board"
    size_match = re.search(rf"{_NUMBER}\s*(?:x|by|×)\s*{_NUMBER}\s*mm?", text)
    width = float(size_match.group(1)) if size_match else (nums[0] if len(nums) > 0 else 60.0)
    height = float(size_match.group(2)) if size_match else (nums[1] if len(nums) > 1 else 40.0)
    layer_match = re.search(r"(2|4|6|8)[\s-]?layer", text)
    layers = int(layer_match.group(1)) if layer_match else 2
    source_components = components or []
    if not source_components and "esp32" in text:
        source_components = ["ESP32-WROOM-32", "USB-UART", "3.3V regulator", "100nF decoupling capacitor", "RESET/BOOT header"]
    resolved = [_component_from_text(value, index + 1) for index, value in enumerate(source_components)]
    if not resolved and "esp32" not in text:
        resolved = [_component_from_text("Custom connector", 1)]

    nets = [
        NetSpec(name="GND", connections=[component.reference for component in resolved]),
        NetSpec(name="+3V3", connections=[component.reference for component in resolved if component.reference.startswith("U") or component.reference.startswith("C")]),
    ]
    if any("USB" in component.value.upper() for component in resolved):
        nets.extend([NetSpec(name="USB_D+", connections=["USB", "ESP32"]), NetSpec(name="USB_D-", connections=["USB", "ESP32"])])

    spec = PcbDesignSpec(
        board_name=board_name,
        width_mm=width,
        height_mm=height,
        layers=layers,  # type: ignore[arg-type]
        components=resolved,
        nets=nets,
        outputs=outputs or ["project", "schematic", "pcb"],  # type: ignore[arg-type]
    )
    questions: list[str] = []
    assumptions = ["Unspecified board dimensions default to 60 mm × 40 mm.", f"A {layers}-layer stackup was selected because no explicit stackup was supplied."]
    if "esp32" in text:
        questions.extend([
            "Please confirm the exact ESP32 module and antenna keepout requirements.",
            "Please confirm the power input and USB-UART interface before ordering a board.",
        ])
    if not layer_match:
        assumptions.append("Layer count defaulted to 2; this is not a production recommendation.")
    return spec, questions, assumptions
