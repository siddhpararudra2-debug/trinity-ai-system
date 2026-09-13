"""
CAD Intermediate Representation (PRD §13).

This is Trinity's own parametric representation, independent of any
CAD backend. A backend adapter (CadQuery, FreeCAD, Onshape, ...)
consumes this IR and produces actual geometry. Keeping this layer
thin is what prevents Trinity from becoming tightly coupled to one
CAD platform (PRD §12).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any

from app.core.errors import RequestValidationError

DEFAULT_QUADCOPTER_PARAMS: dict[str, float] = {
    "overall_size": 50.0,  # mm, motor-to-motor diagonal span
    "motor_count": 4,
    "arm_width": 5.0,  # mm
    "plate_thickness": 1.5,  # mm
    "motor_mount_diameter": 6.0,  # mm
    "fc_mount_spacing": 25.0,  # mm
    "center_plate_size": 26.0,  # mm, square center plate side length
}


@dataclass
class QuadcopterFrameIR:
    units: str
    parameters: dict[str, float] = field(default_factory=dict)

    @classmethod
    def from_request(cls, raw_parameters: dict[str, Any]) -> QuadcopterFrameIR:
        params = dict(DEFAULT_QUADCOPTER_PARAMS)
        for key, value in (raw_parameters or {}).items():
            if key not in params:
                raise RequestValidationError(
                    f"Unknown quadcopter_frame parameter '{key}'",
                    details={"allowed": sorted(params)},
                )
            params[key] = float(value)

        if params["motor_count"] not in (4,):
            raise RequestValidationError(
                "V1 only supports 4-motor (X) quadcopter frames",
                details={"motor_count": params["motor_count"]},
            )
        if params["overall_size"] <= params["center_plate_size"]:
            raise RequestValidationError(
                "overall_size must be larger than center_plate_size",
                details={
                    "overall_size": params["overall_size"],
                    "center_plate_size": params["center_plate_size"],
                },
            )
        for key in (
            "overall_size",
            "center_plate_size",
            "arm_width",
            "plate_thickness",
            "motor_mount_diameter",
            "fc_mount_spacing",
        ):
            if not math.isfinite(params[key]) or params[key] <= 0:
                raise RequestValidationError(f"{key} must be a finite positive value")
        if params["overall_size"] > 1000 or params["plate_thickness"] > 50:
            raise RequestValidationError(
                "Frame dimensions are outside V1's supported engineering range"
            )
        if params["fc_mount_spacing"] > params["center_plate_size"]:
            raise RequestValidationError(
                "fc_mount_spacing must fit within center_plate_size"
            )

        return cls(units="mm", parameters=params)

    def to_dict(self) -> dict[str, Any]:
        return {
            "type": "quadcopter_frame",
            "units": self.units,
            "parameters": self.parameters,
        }
