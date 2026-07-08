"""
Trinity Maker Engine (PCB) — Generates KiCad 7 S-expression files:
  • <name>.kicad_sch — schematic
  • <name>.kicad_pcb — PCB layout
"""
import asyncio
import uuid as _uuid
from datetime import datetime
from typing import Any


class MakerPcbEngine:

    async def process(self, description: str, components: list | None = None) -> dict[str, Any]:
        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(None, self._run, description, components or [])

    def _run(self, description: str, components: list) -> dict[str, Any]:
        board_type = self._detect_board(description)
        slug = board_type.lower().replace(" ", "_").replace("-", "_")
        filename = f"trinity_{slug}"

        sch = self._schematic(description, board_type)
        pcb = self._pcb(description, board_type)

        instructions = (
            "**How to use in KiCad 7:**\n"
            f"1. Save `{filename}.kicad_sch` and `{filename}.kicad_pcb` to a folder\n"
            "2. Open KiCad → **File → Open Project** → select that folder\n"
            "3. Double-click the `.kicad_sch` to view/edit the schematic\n"
            "4. Double-click the `.kicad_pcb` for the board layout\n"
            "5. Run **Inspect → Design Rules Checker (DRC)** before ordering\n"
            "6. **File → Fabrication Outputs → Gerbers** to export manufacturing files"
        )

        return {
            "description": f"KiCad {board_type} — {description[:80]}",
            "sch_content": sch,
            "pcb_content": pcb,
            "filename": filename,
            "board_type": board_type,
            "instructions": instructions,
            "engine": "maker_pcb",
        }

    # ------------------------------------------------------------------

    def _detect_board(self, text: str) -> str:
        t = text.lower()
        if "esp32" in t:
            return "ESP32-DevBoard"
        if "arduino" in t:
            return "Arduino-Shield"
        if "raspberry" in t or "rpi" in t:
            return "RPi-HAT"
        if "stm32" in t:
            return "STM32-Board"
        if "sensor" in t:
            return "Sensor-Board"
        if "power" in t:
            return "Power-Supply"
        return "Custom-PCB"

    def _uid(self) -> str:
        return str(_uuid.uuid4())

    # ------------------------------------------------------------------
    # Schematic
    # ------------------------------------------------------------------

    def _schematic(self, description: str, board_type: str) -> str:
        now = datetime.now()
        return f'''\
(kicad_sch
  (version 20231120)
  (generator "trinity_pcb_engine")
  (generator_version "1.0")
  (uuid "{self._uid()}")
  (title_block
    (title "{board_type}")
    (rev "1.0")
    (company "Trinity AI Engineering OS")
    (comment 1 "{description[:60]}")
    (comment 2 "Generated {now.strftime('%Y-%m-%d %H:%M')}")
  )
  (lib_symbols
    (symbol "MCU_Module:ESP32-WROOM-32"
      (pin_names (offset 1))
      (in_bom yes) (on_board yes)
      (property "Reference" "U" (at 0 33 0)
        (effects (font (size 1.27 1.27))))
      (property "Value" "ESP32-WROOM-32" (at 0 -33 0)
        (effects (font (size 1.27 1.27))))
      (property "Footprint" "RF_Module:ESP32-WROOM-32" (at 0 0 0)
        (effects (font (size 1.27 1.27)) hide))
      (symbol "MCU_Module:ESP32-WROOM-32_0_1"
        (rectangle (start -15.24 -31.75) (end 15.24 31.75)
          (stroke (width 0.254) (type default))
          (fill (type background)))
        (pin bidirectional "GPIO0" (at -17.78 25.4 0) (length 2.54)
          (name "GPIO0" (effects (font (size 1.27 1.27))))
          (number "1" (effects (font (size 1.27 1.27)))))
        (pin bidirectional "GPIO2" (at -17.78 22.86 0) (length 2.54)
          (name "GPIO2" (effects (font (size 1.27 1.27))))
          (number "2" (effects (font (size 1.27 1.27)))))
        (pin power_in "+3.3V" (at -17.78 5.08 0) (length 2.54)
          (name "+3V3" (effects (font (size 1.27 1.27))))
          (number "3" (effects (font (size 1.27 1.27)))))
        (pin power_in "GND" (at -17.78 2.54 0) (length 2.54)
          (name "GND" (effects (font (size 1.27 1.27))))
          (number "4" (effects (font (size 1.27 1.27)))))
        (pin input "EN" (at -17.78 0 0) (length 2.54)
          (name "EN" (effects (font (size 1.27 1.27))))
          (number "5" (effects (font (size 1.27 1.27)))))
      )
    )
    (symbol "Device:C"
      (pin_names (offset 0.254))
      (in_bom yes) (on_board yes)
      (property "Reference" "C" (at 1.016 0.508 0)
        (effects (font (size 1.27 1.27))))
      (property "Value" "C" (at 1.016 -0.508 0)
        (effects (font (size 1.27 1.27))))
      (symbol "Device:C_0_1"
        (polyline (pts (xy -1.524 -0.508) (xy 1.524 -0.508))
          (stroke (width 0.3048) (type default)))
        (polyline (pts (xy -1.524 0.508) (xy 1.524 0.508))
          (stroke (width 0.3048) (type default)))
        (pin passive "~" (at 0 1.778 270) (length 1.27)
          (name "~" (effects (font (size 1.27 1.27))))
          (number "1" (effects (font (size 1.27 1.27)))))
        (pin passive "~" (at 0 -1.778 90) (length 1.27)
          (name "~" (effects (font (size 1.27 1.27))))
          (number "2" (effects (font (size 1.27 1.27)))))
      )
    )
    (symbol "power:+3.3V"
      (in_bom no) (on_board yes)
      (property "Reference" "#PWR" (at 0 -3.81 0)
        (effects (font (size 1.27 1.27)) hide))
      (property "Value" "+3.3V" (at 0 3.556 0)
        (effects (font (size 1.27 1.27))))
      (symbol "power:+3.3V_0_1"
        (polyline (pts (xy 0 0) (xy 0 1.27))
          (stroke (width 0) (type default)))
        (pin power_in "~" (at 0 0 270) (length 0)
          (name "~" (effects (font (size 1.27 1.27))))
          (number "1" (effects (font (size 1.27 1.27)))))
      )
    )
    (symbol "power:GND"
      (in_bom no) (on_board yes)
      (property "Reference" "#PWR" (at 0 -3.81 0)
        (effects (font (size 1.27 1.27)) hide))
      (property "Value" "GND" (at 0 -3.81 0)
        (effects (font (size 1.27 1.27))))
      (symbol "power:GND_0_1"
        (polyline (pts (xy 0 0) (xy 0 -1.27) (xy 1.27 -1.27)
                       (xy 0 -2.54) (xy -1.27 -1.27) (xy 0 -1.27))
          (stroke (width 0) (type default))
          (fill (type none)))
        (pin power_in "~" (at 0 0 270) (length 0)
          (name "~" (effects (font (size 1.27 1.27))))
          (number "1" (effects (font (size 1.27 1.27)))))
      )
    )
  )
  (junction (at 60 60) (diameter 0) (color 0 0 0 0))
  (wire (pts (xy 55 50) (xy 55 60)) (stroke (width 0) (type default)))
  (wire (pts (xy 55 60) (xy 55 70)) (stroke (width 0) (type default)))
  (wire (pts (xy 55 70) (xy 82.26 70)) (stroke (width 0) (type default)))
  (wire (pts (xy 55 50) (xy 82.26 50)) (stroke (width 0) (type default)))
  (symbol (lib_id "MCU_Module:ESP32-WROOM-32") (at 100 80 0) (unit 1)
    (in_bom yes) (on_board yes)
    (property "Reference" "U1" (at 100 45 0))
    (property "Value" "ESP32-WROOM-32" (at 100 116 0))
    (pin "1" (uuid "{self._uid()}"))
    (pin "3" (uuid "{self._uid()}"))
    (pin "4" (uuid "{self._uid()}"))
    (pin "5" (uuid "{self._uid()}"))
  )
  (symbol (lib_id "Device:C") (at 55 60 0) (unit 1)
    (in_bom yes) (on_board yes)
    (property "Reference" "C1" (at 57.5 60 0))
    (property "Value" "100nF" (at 57.5 62.5 0))
    (pin "1" (uuid "{self._uid()}"))
    (pin "2" (uuid "{self._uid()}"))
  )
  (symbol (lib_id "power:+3.3V") (at 55 48 0) (unit 1)
    (in_bom yes) (on_board yes)
    (property "Reference" "#PWR01" (at 55 44 0) hide)
    (property "Value" "+3.3V" (at 55 44 0))
  )
  (symbol (lib_id "power:GND") (at 55 72 0) (unit 1)
    (in_bom yes) (on_board yes)
    (property "Reference" "#PWR02" (at 55 76 0) hide)
    (property "Value" "GND" (at 55 76 0))
  )
  (text "Trinity AI — {board_type}" (at 10 10 0)
    (effects (font (size 2.5 2.5) bold)))
  (text "Generated by Trinity AI Engineering OS" (at 10 14 0)
    (effects (font (size 1.5 1.5) italic)))
)
'''

    # ------------------------------------------------------------------
    # PCB layout
    # ------------------------------------------------------------------

    def _pcb(self, description: str, board_type: str) -> str:
        now = datetime.now()
        return f'''\
(kicad_pcb
  (version 20231120)
  (generator "trinity_pcb_engine")
  (generator_version "1.0")
  (general
    (thickness 1.6)
    (legacy_teardrops no)
  )
  (paper "A4")
  (title_block
    (title "{board_type}")
    (rev "1.0")
    (company "Trinity AI Engineering OS")
    (comment 1 "{description[:60]}")
    (comment 2 "Generated {now.strftime('%Y-%m-%d %H:%M')}")
  )
  (layers
    (0 "F.Cu" signal)
    (31 "B.Cu" signal)
    (36 "B.SilkS" user "B.Silkscreen")
    (37 "F.SilkS" user "F.Silkscreen")
    (38 "B.Mask" user)
    (39 "F.Mask" user)
    (44 "Edge.Cuts" user)
    (46 "B.CrtYd" user "B.Courtyard")
    (47 "F.CrtYd" user "F.Courtyard")
    (49 "F.Fab" user "F.Fabrication")
  )
  (setup
    (pad_to_mask_clearance 0.1)
    (pcbplotparams
      (layerselection 0x00010fc_ffffffff)
      (outputformat 1)
      (outputdirectory "gerbers/")
    )
  )
  (net 0 "")
  (net 1 "GND")
  (net 2 "+3.3V")
  (net 3 "GPIO0")
  (net 4 "GPIO2")
  (footprint "RF_Module:ESP32-WROOM-32" (layer "F.Cu")
    (descr "ESP32-WROOM-32 WiFi+BT SoC module, 38-pin")
    (at 100 100)
    (property "Reference" "U1" (at 0 -20 0) (layer "F.SilkS"))
    (property "Value" "ESP32-WROOM-32" (at 0 20 0) (layer "F.Fab"))
    (attr through_hole)
    (fp_rect (start -9.2 -14.4) (end 9.2 14.4) (layer "F.CrtYd")
      (stroke (width 0.05) (type default)))
    (fp_rect (start -9 -14.2) (end 9 14.2) (layer "F.Fab")
      (stroke (width 0.1) (type default)))
    (fp_text user "Trinity AI" (at 0 0 0) (layer "F.Fab")
      (effects (font (size 1.5 1.5))))
    (pad "1" thru_hole circle (at -8.89 12.7) (size 1.7 1.7) (drill 1.0)
      (layers "*.Cu" "*.Mask") (net 3 "GPIO0"))
    (pad "2" thru_hole circle (at -8.89 10.16) (size 1.7 1.7) (drill 1.0)
      (layers "*.Cu" "*.Mask") (net 4 "GPIO2"))
    (pad "GND" thru_hole rect (at -8.89 0) (size 1.8 1.8) (drill 1.0)
      (layers "*.Cu" "*.Mask") (net 1 "GND"))
    (pad "3V3" thru_hole circle (at -8.89 2.54) (size 1.7 1.7) (drill 1.0)
      (layers "*.Cu" "*.Mask") (net 2 "+3.3V"))
  )
  (footprint "Capacitor_SMD:C_0805_2012Metric" (layer "F.Cu")
    (descr "Decoupling capacitor 100nF 0805")
    (at 85 100)
    (property "Reference" "C1" (at 0 -2 0) (layer "F.SilkS"))
    (property "Value" "100nF" (at 0 2 0) (layer "F.Fab"))
    (attr smd)
    (pad "1" smd roundrect (at -1 0) (size 1.6 1.45) (roundrect_rratio 0.25)
      (layers "F.Cu" "F.Paste" "F.Mask") (net 2 "+3.3V"))
    (pad "2" smd roundrect (at 1 0) (size 1.6 1.45) (roundrect_rratio 0.25)
      (layers "F.Cu" "F.Paste" "F.Mask") (net 1 "GND"))
  )
  (gr_rect (start 70 80) (end 135 120) (layer "Edge.Cuts")
    (stroke (width 0.05) (type default)))
  (gr_text "Trinity AI — {board_type}" (at 80 82) (layer "F.SilkS")
    (effects (font (size 1.5 1.5) bold)))
  (gr_text "{now.strftime('%Y-%m-%d')}" (at 80 85) (layer "F.SilkS")
    (effects (font (size 1 1))))
  (segment (start 91.11 100) (end 87 100) (width 0.25) (layer "F.Cu") (net 2))
  (segment (start 91.11 97.46) (end 87 97.46) (width 0.25) (layer "F.Cu") (net 1))
  (via (at 91.11 100) (size 0.8) (drill 0.4) (layers "F.Cu" "B.Cu") (net 2))
)
'''
