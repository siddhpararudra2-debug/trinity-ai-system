#!/usr/bin/env python3
"""Entry point for the PyInstaller-bundled Trinity API.

Imports src.main directly (no uvicorn import-string) so everything is
resolved through PyInstaller's static analysis, then serves it. The
launcher script sets TRINITY_UI_DIR / TRINITY_NATIVE_EXEC /
TRINITY_STORAGE_ROOT before starting this.
"""

from __future__ import annotations

import os

import src.main
import uvicorn


def main() -> None:
    host = os.getenv("TRINITY_API_HOST", "127.0.0.1")
    port = int(os.getenv("TRINITY_API_PORT", "8000"))
    uvicorn.run(
        src.main.app,
        host=host,
        port=port,
        log_level="info",
    )


if __name__ == "__main__":
    raise SystemExit(main())
