"""
Pure-Python mesh primitives and STL export.

This is the "local fallback" backend mentioned in PRD §12 — it exists
so Trinity produces a real, inspectable geometry artifact without any
external CAD kernel. It is intentionally simple (axis-aligned boxes
only, no boolean CSG), which means motor mounts are rendered as raised
bosses rather than bored holes. Swapping this module for a CadQuery or
Onshape adapter behind the same `CADEngine.generate/validate/export`
interface is exactly the extension point the architecture is built for.
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass

Vec3 = tuple[float, float, float]
Triangle = tuple[Vec3, Vec3, Vec3]


@dataclass
class Mesh:
    triangles: list[Triangle]

    def bounding_box(self) -> tuple[Vec3, Vec3]:
        xs = [v[0] for t in self.triangles for v in t]
        ys = [v[1] for t in self.triangles for v in t]
        zs = [v[2] for t in self.triangles for v in t]
        return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))

    def extend(self, other: "Mesh") -> None:
        self.triangles.extend(other.triangles)


def _face(v0: Vec3, v1: Vec3, v2: Vec3, v3: Vec3) -> list[Triangle]:
    """Two triangles for a planar quad v0-v1-v2-v3 (consistent winding)."""
    return [(v0, v1, v2), (v0, v2, v3)]


def box(center: Vec3, size: Vec3, rotation_z_deg: float = 0.0) -> Mesh:
    """Axis-aligned box (optionally rotated about Z) centered at `center`."""
    cx, cy, cz = center
    sx, sy, sz = size
    hx, hy, hz = sx / 2, sy / 2, sz / 2

    local_corners = [
        (-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
        (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz),
    ]

    theta = math.radians(rotation_z_deg)
    cos_t, sin_t = math.cos(theta), math.sin(theta)

    def place(p: Vec3) -> Vec3:
        x, y, z = p
        rx = x * cos_t - y * sin_t
        ry = x * sin_t + y * cos_t
        return (rx + cx, ry + cy, z + cz)

    c = [place(p) for p in local_corners]

    tris: list[Triangle] = []
    tris += _face(c[0], c[1], c[2], c[3])  # bottom
    tris += _face(c[7], c[6], c[5], c[4])  # top
    tris += _face(c[4], c[5], c[1], c[0])  # front
    tris += _face(c[5], c[6], c[2], c[1])  # right
    tris += _face(c[6], c[7], c[3], c[2])  # back
    tris += _face(c[7], c[4], c[0], c[3])  # left
    return Mesh(triangles=tris)


def write_binary_stl(mesh: Mesh, path: str, name: bytes = b"trinity") -> None:
    with open(path, "wb") as f:
        header = name.ljust(80, b"\0")[:80]
        f.write(header)
        f.write(struct.pack("<I", len(mesh.triangles)))
        for (v0, v1, v2) in mesh.triangles:
            ux, uy, uz = _normal(v0, v1, v2)
            f.write(struct.pack("<3f", ux, uy, uz))
            for v in (v0, v1, v2):
                f.write(struct.pack("<3f", *v))
            f.write(struct.pack("<H", 0))


def _normal(v0: Vec3, v1: Vec3, v2: Vec3) -> Vec3:
    ux = v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]
    vx = v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]
    nx = ux[1] * vx[2] - ux[2] * vx[1]
    ny = ux[2] * vx[0] - ux[0] * vx[2]
    nz = ux[0] * vx[1] - ux[1] * vx[0]
    length = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
    return (nx / length, ny / length, nz / length)
