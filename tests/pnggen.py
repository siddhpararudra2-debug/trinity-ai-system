"""Minimal deterministic PNG builder shared by the upload and bridge tests.

Hand-built rather than pulled from an image library so the bytes are stable
and the test suite gains no dependency just to make a 20-byte file.
"""

from __future__ import annotations

import struct
import zlib


def png_bytes(width: int = 8, height: int = 6) -> bytes:
    """Return a valid 8-bit RGB PNG of the given size."""
    scanlines = b"".join(
        b"\x00" + bytes([(x * 17) % 256 for x in range(width * 3)]) for _ in range(height)
    )

    def chunk(tag: bytes, payload: bytes) -> bytes:
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(scanlines))
        + chunk(b"IEND", b"")
    )