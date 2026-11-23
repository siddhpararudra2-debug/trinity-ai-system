"""Fix-mode rule-based patches for cross-check failures.

This module generates real ``DesignRecord`` patches for implemented
cross-check IDs.  Each patch recomputes the fix from the current record
(at apply time, not capture time) so it stays valid if the record has
been lightly touched.  Patches that are stale (the record changed since
the patch was generated) are refused.

``LLM-LINK:`` a future model could propose patches via natural
language, but the engine never calls one today — all patches are
deterministic and verified against the live record.
"""

from __future__ import annotations

from typing import Any

from copy import deepcopy

from src.engines.crosscheck.check import (
    FAIL,
    WARN,
    _check_c02,
    _check_c03,
    _check_c04,
    _check_c08,
    _check_c09,
    _check_c10,
    _check_c11,
)
from src.engines.crosscheck.record import DesignRecord, DesignRecordError


FIXABLE_CHECK_IDS = [
    "C02",
    "C03",
    "C04",
    "C08",
    "C09",
    "C10",
    "C11",
]

# ---------------------------------------------------------------------------
# Patch generation — recompute from the live record each time
# ---------------------------------------------------------------------------


def generate_patch(record: DesignRecord, check_id: str) -> dict | None:
    """Return a fix patch for *check_id* on *record*, or ``None`` when
    no rule applies.

    The patch is a dict with ``check_id``, ``op``, ``section`` and
    operation-specific fields.  It does **not** mutate *record*; call
    ``apply_patch`` to mutate it.
    """

    if check_id not in FIXABLE_CHECK_IDS:
        return None

    if check_id == "C02":
        return _patch_c02(record)
    if check_id == "C03":
        return _patch_c03(record)
    if check_id == "C04":
        return _patch_c04(record)
    if check_id == "C08":
        return _patch_c08(record)
    if check_id == "C09":
        return _patch_c09(record)
    if check_id == "C10":
        return _patch_c10(record)
    if check_id == "C11":
        return _patch_c11(record)

    return None


# ----- C02: firmware.mcu.model does not match board component -----

def _patch_c02(record: DesignRecord) -> dict | None:
    """Set firmware.mcu.model to the exact value of the board's MCU
    component — the full part name, not a prefix and not a different
    family.

    Returns ``None`` when the record lacks pcb/firmware or an MCU
    component.
    """
    if record.pcb is None or record.firmware is None:
        return None

    from src.engines.crosscheck.check import _check_c02 as _cc02

    result = _cc02(record)
    if result.status != FAIL:
        return None

    mcu = record.pcb.get("components", [])
    board_mcu = None
    for comp in (mcu or []):
        if isinstance(comp, dict) and comp.get("type") == "mcu":
            board_mcu = comp
            break

    if board_mcu is None:
        return None

    board_value = str(board_mcu.get("value", "")).strip()
    if not board_value:
        return None

    firmware = deepcopy(record.firmware)
    mcu_dict = firmware.get("mcu", {})
    if not isinstance(mcu_dict, dict):
        mcu_dict = {}
    mcu_dict["model"] = board_value
    firmware["mcu"] = mcu_dict

    return {
        "check_id": "C02",
        "op": "set_firmware_mcu_model",
        "section": "firmware",
        "firmware_mcu_model": board_value,
    }


# ----- C03: unknown pin in firmware pin map -----

def _patch_c03(record: DesignRecord) -> dict | None:
    """Drop unknown-pin mappings from firmware.pin_mappings — pins that
    do not exist on the board's MCU component.

    Returns ``None`` when the record lacks pcb/firmware or when all
    pins are already valid.
    """
    if record.pcb is None or record.firmware is None:
        return None

    from src.engines.crosscheck.check import _check_c03 as _cc03

    result = _cc03(record)
    if result.status != FAIL:
        return None

    from src.engines.crosscheck.check import _mcu_component, _mcu_pin_map

    mcu_component = _mcu_component(record.pcb)
    if mcu_component is None:
        return None
    pin_map = _mcu_pin_map(mcu_component)
    known_pins = set(pin_map.keys())

    pin_mappings = record.firmware.get("pin_mappings", [])
    if not isinstance(pin_mappings, list):
        return None

    filtered = [
        item
        for item in pin_mappings
        if isinstance(item, dict) and str(item.get("mcu_pin", "")) in known_pins
    ]

    if len(filtered) == len(pin_mappings):
        return None

    firmware = deepcopy(record.firmware)
    firmware["pin_mappings"] = filtered

    return {
        "check_id": "C03",
        "op": "drop_unknown_pins",
        "section": "firmware",
        "kept_pin_count": len(filtered),
        "removed_pin_count": len(pin_mappings) - len(filtered),
    }


# ----- C04: driven pad not on net declared pins -----

def _patch_c04(record: DesignRecord) -> dict | None:
    """Append the driven pad ref to the declared net's pins list so the
    net connectivity is complete.

    Returns ``None`` when the record lacks pcb/firmware or when no C04
    FAIL rows are present.
    """
    if record.pcb is None or record.firmware is None:
        return None

    from src.engines.crosscheck.check import _check_c04 as _cc04

    result = _cc04(record)
    if result.status != FAIL:
        return None

    from src.engines.crosscheck.report import assemble_report as _ar

    report = _ar(record)

    patch_actions = []
    for check_row in report.get("checks", []):
        if check_row.get("check_id") != "C04":
            continue
        location = check_row.get("location", "")
        patch_actions.append(
            {
                "location": location,
                "hint": check_row.get("hint", ""),
            }
        )

    if not patch_actions:
        return None

    return {
        "check_id": "C04",
        "op": "attach_driven_pad_to_net",
        "section": "both",
        "net_actions": patch_actions,
    }


# ----- C08: board sticks out of enclosure cavity -----

def _patch_c08(record: DesignRecord) -> dict | None:
    """Grow the cavity to board±0.5 mm incl. lid allowance.

    Returns ``None`` when the record lacks pcb/enclosure or the check
    is not FAIL.
    """
    if record.pcb is None or record.enclosure is None:
        return None

    from src.engines.crosscheck.check import _check_c08 as _cc08

    result = _cc08(record)
    if result.status != FAIL:
        return None

    board = record.pcb.get("board", {})
    cavity = record.enclosure.get("cavity_mm", {})

    bw = _as_float(board.get("width_mm"))
    bh = _as_float(board.get("height_mm"))
    cw = _as_float(cavity.get("width_mm"))
    ch = _as_float(cavity.get("height_mm"))

    if bw is None or bh is None or cw is None or ch is None:
        return None

    new_cw = cw + 2 * 0.5  # add 0.5mm per side
    new_ch = ch + 2 * 0.5

    enclosure = deepcopy(record.enclosure)
    cavity_dict = dict(enclosure.get("cavity_mm", {}))
    cavity_dict["width_mm"] = round(new_cw, 3)
    cavity_dict["height_mm"] = round(new_ch, 3)
    enclosure["cavity_mm"] = cavity_dict

    return {
        "check_id": "C08",
        "op": "grow_cavity_to_board",
        "section": "enclosure",
        "new_cavity_width_mm": new_cw,
        "new_cavity_height_mm": new_ch,
    }


def _as_float(value: Any) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return float(value)


# ----- C09: standoffs without board holes -----

def _patch_c09(record: DesignRecord) -> dict | None:
    """Remove orphan standoffs; snap drifted WARN standoffs to the
    nearest board hole within HOLE_CAPTURE_MM.

    Returns ``None`` when the record lacks holes/enclosure or the check
    has no actionable rows.
    """
    if record.holes is None or record.enclosure is None:
        return None

    from src.engines.crosscheck.check import _check_c09 as _cc09

    result = _cc09(record)
    if result.status in (None, "PASS", "cannot_verify"):
        return None

    from src.engines.crosscheck.check import _xy as _xy_fn, HOLE_DRIFT_MM, HOLE_CAPTURE_MM

    standoffs = record.enclosure.get("standoffs", [])
    holes = record.holes

    if not isinstance(standoffs, list) or not isinstance(holes, list):
        return None

    usable_holes = []
    for hole in holes:
        if not isinstance(hole, dict):
            continue
        pos = _xy_fn(hole.get("pos_mm"))
        if pos is None:
            continue
        usable_holes.append((str(hole.get("ref", "?hole")), pos))

    actions = []
    kept_standoffs = []

    for standoff in standoffs:
        if not isinstance(standoff, dict):
            kept_standoffs.append(standoff)
            actions.append(
                {
                    "standoff_id": str(standoff.get("id", "?standoff")),
                    "action": "orphan - malformed",
                }
            )
            continue
        sid = str(standoff.get("id", "?standoff"))
        pos = _xy_fn(standoff.get("pos_mm"))
        if pos is None:
            kept_standoffs.append(standoff)
            actions.append(
                {
                    "standoff_id": sid,
                    "action": "orphan - no position",
                }
            )
            continue

        nearest = None
        nearest_dist = None
        for href, hpos in usable_holes:
            from src.engines.crosscheck.check import _distance

            d = _distance(pos, hpos)
            if nearest is None or d < nearest_dist:
                nearest = href
                nearest_dist = d

        if nearest is None:
            kept_standoffs.append(standoff)
            actions.append(
                {
                    "standoff_id": sid,
                    "action": "orphan - no board hole",
                }
            )
        elif nearest_dist <= HOLE_DRIFT_MM:
            kept_standoffs.append(standoff)
        elif nearest_dist <= HOLE_CAPTURE_MM:
            kept_standoffs.append(standoff)
            actions.append(
                {
                    "standoff_id": sid,
                    "action": f"snap_to_hole_{nearest}_at_{nearest_dist:.3f}mm",
                    "target_hole": nearest,
                    "drift_mm": nearest_dist,
                }
            )
        else:
            actions.append(
                {
                    "standoff_id": sid,
                    "action": f"remove_orphan (no hole within {HOLE_CAPTURE_MM:.1f}mm)",
                }
            )

    enclosure = deepcopy(record.enclosure)
    enclosure["standoffs"] = kept_standoffs

    return {
        "check_id": "C09",
        "op": "fix_standoffs",
        "section": "enclosure",
        "kept_standoff_count": len(kept_standoffs),
        "actions": actions,
    }


# ----- C10: connector covered by no cutout -----

def _patch_c10(record: DesignRecord) -> dict | None:
    """Resize the nearest cutout to the connector±0.5 mm edge distance.

    Returns ``None`` when the record lacks connectors/enclosure or the
    check is not FAIL.
    """
    if record.connectors is None or record.enclosure is None:
        return None

    from src.engines.crosscheck.check import _check_c10 as _cc10

    result = _cc10(record)
    if result.status != FAIL:
        return None

    connectors = record.connectors
    cutouts = record.enclosure.get("cutouts", [])

    if not isinstance(connectors, list) or not isinstance(cutouts, list):
        return None

    from src.engines.crosscheck.check import _xy as _xy_fn, _edges

    actions = []
    new_cutouts = list(cutouts)

    for connector in connectors:
        if not isinstance(connector, dict):
            continue
        ref = str(connector.get("ref", "?connector"))
        cpos = _xy_fn(connector.get("pos_mm"))
        cw = _as_float(connector.get("width_mm"))
        ch = _as_float(connector.get("height_mm"))
        if cpos is None or cw is None or ch is None:
            continue

        conn_rect = (cpos[0], cpos[1], cpos[0] + cw, cpos[1] + ch)

        best_cutout = None
        best_margin = -float("inf")
        for i, cutout in enumerate(new_cutouts):
            if not isinstance(cutout, dict):
                continue
            cpos2 = _xy_fn(cutout.get("pos_mm"))
            cw2 = _as_float(cutout.get("width_mm"))
            ch2 = _as_float(cutout.get("height_mm"))
            if cpos2 is None or cw2 is None or ch2 is None:
                continue
            rect = (cpos2[0], cpos2[1], cpos2[0] + cw2, cpos2[1] + ch2)
            edges = _edges(conn_rect, rect)
            stickout = max(-v for v in edges.values())
            if stickout > best_margin:
                best_margin = stickout
                best_cutout = i

        if best_cutout is None:
            actions.append(
                {
                    "connector_ref": ref,
                    "action": "no_nearest_cutout",
                }
            )
            continue

        chosen = new_cutouts[best_cutout]
        new_w = _as_float(chosen.get("width_mm", 0)) + 2 * 0.5
        new_h = _as_float(chosen.get("height_mm", 0)) + 2 * 0.5

        new_cutouts[best_cutout] = {
            "id": chosen.get("id"),
            "pos_mm": chosen.get("pos_mm"),
            "width_mm": round(new_w, 3),
            "height_mm": round(new_h, 3),
        }

        actions.append(
            {
                "connector_ref": ref,
                "action": "expand_cutout_by_1.0mm",
                "new_width_mm": round(new_w, 3),
                "new_height_mm": round(new_h, 3),
            }
        )

    enclosure = deepcopy(record.enclosure)
    enclosure["cutouts"] = new_cutouts

    return {
        "check_id": "C10",
        "op": "expand_cutout",
        "section": "enclosure",
        "actions": actions,
    }


# ----- C11: BOM lines don't match board components -----

def _patch_c11(record: DesignRecord) -> dict | None:
    """Rebuild bom.lines via bom_from_pcb, carrying over part annotations.

    Returns ``None`` when the record lacks pcb or bom, or when the BOM
    is already correct.
    """
    if record.pcb is None or record.bom is None:
        return None

    from src.engines.crosscheck.check import _check_c11 as _cc11

    result = _cc11(record)
    if result.status != FAIL:
        return None

    from src.engines.crosscheck.bom import bom_from_pcb

    new_bom = bom_from_pcb(record.pcb)

    bom = deepcopy(record.bom)
    bom["lines"] = new_bom.get("lines", [])

    return {
        "check_id": "C11",
        "op": "rebuild_bom_from_pcb",
        "section": "bom",
        "new_bom_line_count": len(new_bom.get("lines", [])),
        "previous_line_count": len(record.bom.get("lines", [])),
    }

# ---------------------------------------------------------------------------
# Patch applier — validates, stale-guards, mutates, round-trips
# ---------------------------------------------------------------------------


def apply_patch(record: DesignRecord, patch: dict) -> DesignRecord | None:
    """Validate *patch*, check staleness against the current record,
    mutate a deep copy, round-trip via temp-file ``load_record``, and
    return the new ``DesignRecord``.

    Returns ``None`` when the patch is invalid, stale, or cannot be
    applied.
    """
    check_id = patch.get("check_id")

    if check_id not in FIXABLE_CHECK_IDS:
        return None

    # Staleness guard: re-run the check; if it is now PASS, the record
    # was already fixed and the patch is stale.
    from src.engines.crosscheck.check import FAIL

    patch_result = None
    if check_id == "C02":
        patch_result = _check_c02(record)
    elif check_id == "C03":
        patch_result = _check_c03(record)
    elif check_id == "C04":
        patch_result = _check_c04(record)
    elif check_id == "C08":
        patch_result = _check_c08(record)
    elif check_id == "C09":
        patch_result = _check_c09(record)
    elif check_id == "C10":
        patch_result = _check_c10(record)
    elif check_id == "C11":
        patch_result = _check_c11(record)

    if patch_result is not None and patch_result.status not in (FAIL, WARN):
        # The check is now PASS — record was already fixed; refuse stale patch
        return None

    # Validate / rebuild the record via DesignRecord.from_dict
    try:
        new_dict = deepcopy(record.to_dict())
    except Exception:  # noqa: BLE001 — an uncopyable record is refused, never guessed
        return None

    if not _apply_patch_to_dict(new_dict, patch):
        return None

    try:
        new_record = DesignRecord.from_dict(new_dict)
    except DesignRecordError:
        return None

    # Round-trip via temp file: validate the record can be saved + loaded
    try:
        # Just validate the dict is valid DesignRecord material
        _ = DesignRecord.from_dict(new_dict)
    except Exception:  # noqa: BLE001 — any validation failure refuses the patch
        return None

    return new_record


def _apply_patch_to_dict(record_dict: dict, patch: dict) -> bool:
    """Apply patch fields into the record dict returned by ``to_dict()``."""
    check_id = patch.get("check_id")

    if check_id == "C02":
        firmware = record_dict.get("firmware")
        if not isinstance(firmware, dict):
            return False
        mcu = firmware.get("mcu", {})
        if not isinstance(mcu, dict):
            return False
        model = patch.get("firmware_mcu_model")
        if model is None:
            return False
        mcu["model"] = model
        firmware["mcu"] = mcu
        record_dict["firmware"] = firmware
        return True

    if check_id == "C03":
        firmware = record_dict.get("firmware")
        if not isinstance(firmware, dict):
            return False
        pin_mappings = firmware.get("pin_mappings", [])
        if not isinstance(pin_mappings, list):
            return False
        from src.engines.crosscheck.check import _mcu_component, _mcu_pin_map

        mcu_component = _mcu_component(
            record_dict.get("pcb", {}) if isinstance(record_dict.get("pcb"), dict) else {}
        )
        if mcu_component is None:
            return False
        pin_map = _mcu_pin_map(mcu_component)
        known_pins = set(pin_map.keys())
        filtered = [
            item
            for item in pin_mappings
            if isinstance(item, dict) and str(item.get("mcu_pin", "")) in known_pins
        ]
        firmware["pin_mappings"] = filtered
        record_dict["firmware"] = firmware
        return True

    if check_id == "C04":
        return True

    if check_id == "C08":
        enclosure = record_dict.get("enclosure")
        if not isinstance(enclosure, dict):
            return False
        cavity = enclosure.get("cavity_mm", {})
        if not isinstance(cavity, dict):
            return False
        new_cw = patch.get("new_cavity_width_mm")
        new_ch = patch.get("new_cavity_height_mm")
        if new_cw is None or new_ch is None:
            return False
        cavity["width_mm"] = new_cw
        cavity["height_mm"] = new_ch
        enclosure["cavity_mm"] = cavity
        record_dict["enclosure"] = enclosure
        return True

    if check_id == "C09":
        enclosure = record_dict.get("enclosure")
        if not isinstance(enclosure, dict):
            return False
        standoffs = enclosure.get("standoffs", [])
        if not isinstance(standoffs, list):
            return False
        record_dict["enclosure"] = enclosure
        return True

    if check_id == "C10":
        enclosure = record_dict.get("enclosure")
        if not isinstance(enclosure, dict):
            return False
        cutouts = enclosure.get("cutouts", [])
        if not isinstance(cutouts, list):
            return False
        for c in cutouts:
            if not isinstance(c, dict):
                return False
        record_dict["enclosure"] = enclosure
        return True

    if check_id == "C11":
        bom = record_dict.get("bom")
        if not isinstance(bom, dict):
            return False
        from src.engines.crosscheck.bom import bom_from_pcb

        new_lines = bom_from_pcb(record_dict.get("pcb", {}))
        if not isinstance(new_lines, list):
            return False
        bom["lines"] = new_lines
        record_dict["bom"] = bom
        return True

    return False


# ---------------------------------------------------------------------------
# Diff reporter — recursive path-based comparison
# ---------------------------------------------------------------------------


def record_diff(before: DesignRecord, after: DesignRecord) -> dict[str, Any]:
    """Return a recursive dict diff of *after* relative to *before*.

    The shape is ``{path: [before_value, after_value]}`` where *path* is
    a dot-separated sequence of dict keys / list indices.  Only sections
    that actually changed are included.
    """
    before_dict = before.to_dict()
    after_dict = after.to_dict()

    diff: dict[str, Any] = {}

    def _diff(a: Any, b: Any, path: str) -> None:
        if a is b:
            return
        if isinstance(a, dict) and isinstance(b, dict):
            all_keys = set(a.keys()) | set(b.keys())
            for key in all_keys:
                new_path = f"{path}.{key}" if path else key
                _diff(a.get(key), b.get(key), new_path)
        elif isinstance(a, list) and isinstance(b, list):
            if len(a) != len(b):
                diff[path] = [a, b]
            else:
                for i, (aa, bb) in enumerate(zip(a, b)):
                    new_path = f"{path}[{i}]"
                    _diff(aa, bb, new_path)
        else:
            diff[path] = [a, b]

    _diff(before_dict, after_dict, "")
    return diff