"""
Trinity Maker Engine (CAD) — Generates Fusion 360 Python API scripts
for parametric mechanical parts.
"""
import asyncio
import re
from datetime import datetime
from typing import Any


class MakerCadEngine:

    async def process(self, description: str, parameters: dict | None = None) -> dict[str, Any]:
        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(None, self._run, description, parameters or {})

    def _run(self, description: str, parameters: dict) -> dict[str, Any]:
        part_type = self._detect_part(description)
        params = self._extract_params(description, parameters)

        dispatchers = {
            "bracket": (self._bracket, "l_bracket.py",
                        f"L-Bracket: {params['arm1']}mm × {params['arm2']}mm, thickness {params['thick']}mm"),
            "housing": (self._housing, "housing.py",
                        f"Rectangular Housing: {params['wbox']}×{params['dep']}×{params['ht']}mm"),
            "gear": (self._gear, "spur_gear.py",
                     f"Spur Gear: {params['teeth']} teeth, module {params['mod']}"),
            "shaft": (self._shaft, "shaft.py",
                      f"Shaft: Ø{params['dia']}mm × {params['length']}mm"),
        }

        fn, filename, desc = dispatchers.get(part_type, dispatchers["bracket"])
        script = fn(params)

        instructions = (
            "**How to run in Fusion 360:**\n"
            "1. Open Autodesk Fusion 360\n"
            "2. Tools → Add-ins → Scripts and Add-ins (Shift+S)\n"
            "3. Click **+** → paste this script → click **Run**\n"
            "4. The part appears on the design canvas\n"
            "5. Edit parameter values at the top of the script to resize"
        )

        return {
            "description": desc,
            "script": script,
            "filename": filename,
            "part_type": part_type,
            "parameters": params,
            "instructions": instructions,
            "engine": "maker_cad",
        }

    # ------------------------------------------------------------------
    # Part detection + parameter extraction
    # ------------------------------------------------------------------

    def _detect_part(self, text: str) -> str:
        t = text.lower()
        if any(w in t for w in ["bracket", "l-bracket", "angle", "l bracket"]):
            return "bracket"
        if any(w in t for w in ["housing", "enclosure", "box", "case", "shell"]):
            return "housing"
        if any(w in t for w in ["gear", "sprocket", "pinion"]):
            return "gear"
        if any(w in t for w in ["shaft", "rod", "axle"]):
            return "shaft"
        return "bracket"

    def _extract_params(self, text: str, overrides: dict) -> dict:
        nums = [float(m) for m in re.findall(r"\d+(?:\.\d+)?", text)]
        p = {
            "arm1": nums[0] if len(nums) > 0 else 50.0,
            "arm2": nums[1] if len(nums) > 1 else 50.0,
            "thick": nums[2] if len(nums) > 2 else 5.0,
            "wbox": nums[0] if len(nums) > 0 else 80.0,
            "dep": nums[1] if len(nums) > 1 else 60.0,
            "ht": nums[2] if len(nums) > 2 else 40.0,
            "wall": 3.0,
            "teeth": int(nums[0]) if len(nums) > 0 else 24,
            "mod": nums[1] if len(nums) > 1 else 1.5,
            "face_w": nums[2] if len(nums) > 2 else 10.0,
            "dia": nums[0] if len(nums) > 0 else 20.0,
            "length": nums[1] if len(nums) > 1 else 100.0,
            "hole_dia": 4.0,
        }
        p.update(overrides)
        return p

    # ------------------------------------------------------------------
    # Script generators
    # ------------------------------------------------------------------

    def _header(self, part_name: str) -> str:
        return f'''"""
Trinity Maker Engine — Fusion 360 Script
Part   : {part_name}
Date   : {datetime.now().strftime("%Y-%m-%d %H:%M")}
Source : Trinity AI Engineering OS

HOW TO RUN
  1. Open Autodesk Fusion 360
  2. Tools → Add-ins → Scripts and Add-ins (Shift+S)
  3. Create new script, paste this code, click Run
"""
import adsk.core
import adsk.fusion
import traceback

CM = 0.1  # mm → cm (Fusion 360 internal unit is cm)
'''

    def _bracket(self, p: dict) -> str:
        return self._header("Parametric L-Bracket") + f'''
# ── PARAMETERS (edit these) ─────────────────────────────────────
ARM1   = {p["arm1"]}   # mm — horizontal arm length
ARM2   = {p["arm2"]}   # mm — vertical arm length
THICK  = {p["thick"]}   # mm — material thickness
WIDTH  = 30.0          # mm — bracket width (extrusion depth)
HOLE_D = {p["hole_dia"]}  # mm — mounting hole diameter


def run(context):
    ui = None
    try:
        app    = adsk.core.Application.get()
        ui     = app.userInterface
        design = app.activeProduct
        root   = design.rootComponent

        # ── Sketch L-profile ────────────────────────────────────
        sketch = root.sketches.add(root.xYConstructionPlane)
        lines  = sketch.sketchCurves.sketchLines
        P = adsk.core.Point3D.create
        pts = [
            P(0,         0,          0),
            P(ARM1*CM,   0,          0),
            P(ARM1*CM,   THICK*CM,   0),
            P(THICK*CM,  THICK*CM,   0),
            P(THICK*CM,  ARM2*CM,    0),
            P(0,         ARM2*CM,    0),
        ]
        for i in range(len(pts)):
            lines.addByTwoPoints(pts[i], pts[(i+1) % len(pts)])

        # ── Extrude ──────────────────────────────────────────────
        prof    = sketch.profiles.item(0)
        exts    = root.features.extrudeFeatures
        ext_in  = exts.createInput(
            prof, adsk.fusion.FeatureOperations.NewBodyFeatureOperation
        )
        ext_in.setDistanceExtent(
            False, adsk.core.ValueInput.createByReal(WIDTH * CM)
        )
        exts.add(ext_in)

        # ── Mounting holes ──────────────────────────────────────
        hole_sk = root.sketches.add(root.xYConstructionPlane)
        cc      = hole_sk.sketchCurves.sketchCircles
        r       = (HOLE_D / 2) * CM
        # Two holes on horizontal arm
        cc.addByCenterRadius(P(15*CM,      THICK/2*CM, 0), r)
        cc.addByCenterRadius(P((ARM1-10)*CM, THICK/2*CM, 0), r)

        ui.messageBox(
            f"✅ L-Bracket created\\n"
            f"Arm1={{ARM1}}mm  Arm2={{ARM2}}mm  Thick={{THICK}}mm  Width={{WIDTH}}mm"
        )

    except Exception:
        if ui:
            ui.messageBox("Trinity CAD Error:\\n" + traceback.format_exc())
'''

    def _housing(self, p: dict) -> str:
        return self._header("Rectangular Housing") + f'''
# ── PARAMETERS ──────────────────────────────────────────────────
W    = {p["wbox"]}   # mm — outer width
D    = {p["dep"]}   # mm — outer depth
H    = {p["ht"]}   # mm — outer height
WALL = {p["wall"]}   # mm — wall thickness


def run(context):
    ui = None
    try:
        app    = adsk.core.Application.get()
        ui     = app.userInterface
        design = app.activeProduct
        root   = design.rootComponent

        # ── Outer block ─────────────────────────────────────────
        sketch = root.sketches.add(root.xYConstructionPlane)
        sketch.sketchCurves.sketchLines.addTwoPointRectangle(
            adsk.core.Point3D.create(0, 0, 0),
            adsk.core.Point3D.create(W*CM, D*CM, 0),
        )
        exts = root.features.extrudeFeatures
        ei   = exts.createInput(
            sketch.profiles.item(0),
            adsk.fusion.FeatureOperations.NewBodyFeatureOperation,
        )
        ei.setDistanceExtent(False, adsk.core.ValueInput.createByReal(H * CM))
        exts.add(ei)

        # ── Shell (hollow out) ──────────────────────────────────
        body     = root.bRepBodies.item(0)
        top_face = None
        for face in body.faces:
            if abs(face.boundingBox.maxPoint.z - H*CM) < 0.001:
                top_face = face
                break
        if top_face:
            col = adsk.core.ObjectCollection.create()
            col.add(top_face)
            shells = root.features.shellFeatures
            si = shells.createInput(col, False,
                                    adsk.core.ValueInput.createByReal(WALL * CM))
            shells.add(si)

        ui.messageBox(f"✅ Housing created: {{W}}×{{D}}×{{H}}mm, wall={{WALL}}mm")

    except Exception:
        if ui:
            ui.messageBox("Trinity CAD Error:\\n" + traceback.format_exc())
'''

    def _gear(self, p: dict) -> str:
        pitch_r = p["teeth"] * p["mod"] / 2
        outer_r = pitch_r + p["mod"]
        root_r  = pitch_r - 1.25 * p["mod"]
        return self._header("Parametric Spur Gear") + f'''
import math

# ── PARAMETERS ──────────────────────────────────────────────────
N       = {p["teeth"]}    # number of teeth
MODULE  = {p["mod"]}   # gear module (mm) — tooth size
FACE_W  = {p["face_w"]}   # face width (mm)
PRESSURE_ANGLE = 20.0  # degrees (standard)
SHAFT_D = 8.0          # shaft hole diameter (mm)

# Derived values
PITCH_R  = N * MODULE / 2          # = {pitch_r:.2f} mm
OUTER_R  = PITCH_R + MODULE        # addendum circle = {outer_r:.2f} mm
ROOT_R   = PITCH_R - 1.25*MODULE   # dedendum circle = {root_r:.2f} mm
BASE_R   = PITCH_R * math.cos(math.radians(PRESSURE_ANGLE))


def run(context):
    ui = None
    try:
        app    = adsk.core.Application.get()
        ui     = app.userInterface
        design = app.activeProduct
        root   = design.rootComponent

        # ── Addendum circle (simplified — no involute teeth) ────
        sketch = root.sketches.add(root.xYConstructionPlane)
        cc = sketch.sketchCurves.sketchCircles
        O  = adsk.core.Point3D.create
        cc.addByCenterRadius(O(0, 0, 0), OUTER_R * CM)

        exts = root.features.extrudeFeatures
        ei   = exts.createInput(
            sketch.profiles.item(0),
            adsk.fusion.FeatureOperations.NewBodyFeatureOperation,
        )
        ei.setDistanceExtent(False, adsk.core.ValueInput.createByReal(FACE_W * CM))
        exts.add(ei)

        # ── Shaft hole ──────────────────────────────────────────
        shaft_sk = root.sketches.add(root.xYConstructionPlane)
        shaft_sk.sketchCurves.sketchCircles.addByCenterRadius(
            O(0, 0, 0), (SHAFT_D / 2) * CM
        )
        ei2 = exts.createInput(
            shaft_sk.profiles.item(0),
            adsk.fusion.FeatureOperations.CutFeatureOperation,
        )
        ei2.setAllExtent(False)
        exts.add(ei2)

        ui.messageBox(
            f"✅ Spur Gear created\\n"
            f"Teeth={{N}}, Module={{MODULE}}, Pitch radius={{PITCH_R:.1f}}mm"
        )

    except Exception:
        if ui:
            ui.messageBox("Trinity CAD Error:\\n" + traceback.format_exc())
'''

    def _shaft(self, p: dict) -> str:
        return self._header("Parametric Shaft") + f'''
# ── PARAMETERS ──────────────────────────────────────────────────
DIA    = {p["dia"]}   # mm — shaft diameter
LENGTH = {p["length"]}  # mm — shaft length
KEY_W  = 4.0          # mm — keyway width  (set 0 to skip)
KEY_D  = 2.0          # mm — keyway depth


def run(context):
    ui = None
    try:
        app    = adsk.core.Application.get()
        ui     = app.userInterface
        design = app.activeProduct
        root   = design.rootComponent

        # ── Cylinder ────────────────────────────────────────────
        sketch = root.sketches.add(root.xYConstructionPlane)
        sketch.sketchCurves.sketchCircles.addByCenterRadius(
            adsk.core.Point3D.create(0, 0, 0), (DIA / 2) * CM
        )
        exts = root.features.extrudeFeatures
        ei   = exts.createInput(
            sketch.profiles.item(0),
            adsk.fusion.FeatureOperations.NewBodyFeatureOperation,
        )
        ei.setDistanceExtent(False, adsk.core.ValueInput.createByReal(LENGTH * CM))
        exts.add(ei)

        ui.messageBox(f"✅ Shaft: Ø{{DIA}}mm × {{LENGTH}}mm")

    except Exception:
        if ui:
            ui.messageBox("Trinity CAD Error:\\n" + traceback.format_exc())
'''
