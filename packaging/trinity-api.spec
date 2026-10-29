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
# desktop-window dependency (pywebview/webview) never ship.
#
# M11: the OpenCascade kernel IS carried in (the `cad` extra must be
# installed in the build env). The OCP submodules step.py imports lazily
# are declared explicitly so the frozen importer finds them regardless
# of hook coverage, and the OCP wheels' TK*.dll dependencies reach
# _internal/ through PyInstaller's binary dependency scan.
a = Analysis(
    ["trinity_api.py"],
    pathex=["."],
    binaries=[],
    datas=[("..\\config", "config")],
    hiddenimports=[
        "uvicorn.logging",
        "uvicorn.loops.auto",
        "OCP",
        "OCP.BRep",
        "OCP.BRepBuilderAPI",
        "OCP.BRepGProp",
        "OCP.GProp",
        "OCP.gp",
        "OCP.Interface",
        "OCP.STEPControl",
        "OCP.TopAbs",
        "OCP.TopExp",
        "OCP.TopoDS",
    ],
    excludes=[
        "pytest",
        "ruff",
        "tkinter",
        "pywebview",
        "webview",
        "cv2",
        "matplotlib",
        "IPython",
        "cadquery",  # the app imports only OCP; keep cadquery's extras out
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
