"""
Geometry generation step of the CAD workflow (PRD §14):

    Engineering Parameters -> CAD IR -> Constraint Generation ->
    Geometry Generation -> Geometry Validation -> ...

This module is the "Geometry Generation" step for the built-in
quadcopter_frame primitive.
"""

from __future__ import annotations

import math

from src.engines.cad.ir import QuadcopterFrameIR
from src.engines.cad.primitives import Mesh, box

ARM_ANGLES_DEG = (45.0, 135.0, 225.0, 315.0)  # X configuration


def build_quadcopter_frame(ir: QuadcopterFrameIR) -> Mesh:
    p = ir.parameters
    plate_size = p["center_plate_size"]
    thickness = p["plate_thickness"]
    arm_width = p["arm_width"]
    overall = p["overall_size"]
    mount_d = p["motor_mount_diameter"]

    mesh = box(center=(0, 0, 0), size=(plate_size, plate_size, thickness))

    start_r = (plate_size / 2) * math.sqrt(2)
    end_r = overall / 2
    arm_length = end_r - start_r

    for angle in ARM_ANGLES_DEG:
        theta = math.radians(angle)
        center_r = (start_r + end_r) / 2
        arm_center = (center_r * math.cos(theta), center_r * math.sin(theta), 0.0)
        mesh.extend(
            box(
                center=arm_center,
                size=(arm_length, arm_width, thickness),
                rotation_z_deg=angle,
            )
        )

        motor_center = (end_r * math.cos(theta), end_r * math.sin(theta), 0.0)
        boss_side = mount_d * 1.4
        mesh.extend(
            box(
                center=motor_center,
                size=(boss_side, boss_side, thickness * 1.5),
                rotation_z_deg=angle,
            )
        )

    return mesh
