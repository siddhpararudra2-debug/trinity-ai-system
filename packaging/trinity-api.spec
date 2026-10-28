# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec for the Trinity API backend (onedir).
#
# Layout contract the launcher depends on:
#   - config/*.yaml land under _internal/config/ so
#     src.core.model_config (REPO_ROOT = _MEIPASS) keeps resolving them
#     without a repack.
#   - src is bundled wholesale so the native bridge package, engines and
#     API routes all reach the frozen importer.
#
# Exclusions honest-bundle rule: test/dev tooling (pytest, ruff) and the
# desktop-window dependency (pywebview/webview) never ship; the CAD
# OpenCascade kernel is an optional extra and stays out — the catalog
# reports step as unavailable with a reason, as designed.

a = Analysis(
    ["trinity_api.py"],
    pathex=["."],
    binaries=[],
    datas=[("..\\config", "config")],
    hiddenimports=["uvicorn.logging", "uvicorn.loops.auto"],
    excludes=[
        "pytest",
        "ruff",
        "tkinter",
        "pywebview",
        "webview",
        "cv2",
        "matplotlib",
        "IPython",
    ],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="trinity-api",
    console=True,
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    name="trinity-api",
)
