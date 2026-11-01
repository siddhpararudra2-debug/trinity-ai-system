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
#
# M16: the same spec builds BOTH platform bundles. The config path is
# joined at build time (no hard separator), the binary dependency scan
# collects TK*.dll on Windows and libTK*.so* on Linux alike, and the
# pywin32-only importers PyInstaller adds on Windows simply do not exist
# on a POSIX build — one spec, no per-platform forks to drift.
import os

# M16: resolve the repo root from the spec's own location, never from the
# caller's cwd. The Windows packager runs PyInstaller from the repo root,
# where pathex=["."] happened to find `src`; the Linux builder cd's into
# packaging/ first, so "." was packaging/ and `src` was silently left out
# of the bundle -- the frozen API died at startup with
# ModuleNotFoundError: No module named 'src'. SPECPATH is the directory
# holding this spec, so its parent is the repo root on both platforms.
REPO_ROOT = os.path.dirname(SPECPATH)

a = Analysis(
    ["trinity_api.py"],
    pathex=[REPO_ROOT],
    binaries=[],
    datas=[
        (os.path.join("..", "config"), "config"),
        # M18.4: the research fallback's repo-docs corpus ships with the
        # bundle (a few hundred KB); without these the Linux search op
        # would have an empty default corpus.
        (os.path.join("..", "README.md"), "."),
        (os.path.join("..", "work.md"), "."),
        (os.path.join("..", "opencodeworks.md"), "."),
        (os.path.join("..", "docs"), "docs"),
    ],
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
