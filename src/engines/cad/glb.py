"""Dependency-free GLB 2.0 exporter for Trinity's deterministic triangle mesh."""

from __future__ import annotations

import json
import struct
from pathlib import Path

from src.engines.cad.primitives import Mesh, _normal


def write_glb(mesh: Mesh, path: Path) -> None:
    """Write a standards-compliant glTF binary with unindexed triangle positions/normals."""
    vertices = [v for triangle in mesh.triangles for v in triangle]
    normals = [_normal(*triangle) for triangle in mesh.triangles for _ in range(3)]
    positions = b"".join(struct.pack("<3f", *v) for v in vertices)
    normal_bytes = b"".join(struct.pack("<3f", *v) for v in normals)
    binary = positions + normal_bytes
    min_v = [min(v[i] for v in vertices) for i in range(3)]
    max_v = [max(v[i] for v in vertices) for i in range(3)]
    document = {
        "asset": {"version": "2.0", "generator": "Trinity AI native CAD"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [
            {"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "mode": 4}]}
        ],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {
                "buffer": 0,
                "byteOffset": 0,
                "byteLength": len(positions),
                "target": 34962,
            },
            {
                "buffer": 0,
                "byteOffset": len(positions),
                "byteLength": len(normal_bytes),
                "target": 34962,
            },
        ],
        "accessors": [
            {
                "bufferView": 0,
                "componentType": 5126,
                "count": len(vertices),
                "type": "VEC3",
                "min": min_v,
                "max": max_v,
            },
            {
                "bufferView": 1,
                "componentType": 5126,
                "count": len(vertices),
                "type": "VEC3",
            },
        ],
    }
    encoded = json.dumps(document, separators=(",", ":")).encode()
    encoded += b" " * ((4 - len(encoded) % 4) % 4)
    binary += b"\0" * ((4 - len(binary) % 4) % 4)
    total = 12 + 8 + len(encoded) + 8 + len(binary)
    with path.open("wb") as handle:
        handle.write(struct.pack("<4sII", b"glTF", 2, total))
        handle.write(struct.pack("<I4s", len(encoded), b"JSON"))
        handle.write(encoded)
        handle.write(struct.pack("<I4s", len(binary), b"BIN\0"))
        handle.write(binary)
