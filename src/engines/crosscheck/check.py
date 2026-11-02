"""Cross-checks (M20.2-M20.4): the checker the verified report builds on.

C00 is the engine gate: the record's ``engine_checks`` entries must all
be VALIDATED/VERIFIED (the status chain ``src/models/schemas.py`` owns).
A FAILED engine check fails the gate with the engine's reason verbatim;
an unfinished status (e.g. GENERATED) is a WARN cannot_verify — an
engine check the record never made is never passed by default.

C01-C05 cover the firmware/board wiring (record completeness, MCU
identity, pin existence, named nets, peripheral counterparts); C06-C07
cover the electrical record-side annotations: C06 wants exactly one
declared rail class per board power net (discovered from
``power_in``/``power_out`` pads) plus the MCU's rail on the board, and
C07 weighs each rail's declared draws against its ``capacity_ma`` (≤ 80%
PASS, ≤ 100% WARN, over capacity FAIL; a rider without declared draw
data WARNs). Both read the ``power`` section — absent means WARN
cannot_verify, never a guessed rail. C08-C10
cover the enclosure geometry the design record owns itself — board
inside the cavity, standoffs on board holes, connectors inside cutouts —
in a single mm frame where `enclosure.cavity_mm` must carry its own
`origin_mm` (an absent origin is `cannot_verify`; a position is stated,
never guessed). Bands per decided.md: C08/C10 clearances below 0.5 mm
WARN, negative means the inner escapes the window (FAIL); C09 pairs
standoffs with board holes at ≤ 0.2 mm drift PASS, ≤ 1.0 mm WARN, beyond
that the standoff simply has no hole (FAIL; `HOLE_CAPTURE_MM = 1.0`).
C11 (M21.1) matches the BOM against the board's populated components:
refs 1:1, value/package compared string-collapsed and case-sensitive,
quantity against the ref's population count (`decided.md`, convention
9).

``overall_status()`` folds a run into the report-level triad decided in
`decided.md`: VERIFIED = every check PASS (the C00 gate included);
WARNING = no FAIL but at least one WARN; FAILED = any check FAILed or
any engine check FAILED. WARN never grants VERIFIED, so a cannot_verify
WARN denies it too.

Each check reads the design record (`src.engines.crosscheck.record`) and
returns exactly one verdict with a human-readable reason and a location:

    {check_id, name, status: PASS|WARN|FAIL, reason, location, evidence}

Settled semantics (`decided.md`, Phase 0): WARN never grants VERIFIED and
never fails the report; a required input that is **absent** (a record
section, or a field the check needs inside a present section) is the WARN
`cannot_verify` with the input named — never a PASS by default. Nothing
in this module consults a model.

Field names are the native serializers' real wire names, verified against
`app/native/src/pcb/PcbDesign.cpp` and `.../firmware/FirmwareProject.cpp`
(nested struct fields are snake_case even though the C++ members are
camelCase: `mcu_pin`, `pos_mm`, `rotation_deg`, `clock_hz_max`;
directions are `in`/`out`/`inout`; component types are
`mcu`/`regulator`/`connector`/...).

One documented interplay from the decided table: a *driven* pin the board
leaves floating is reported twice, on purpose — C03 gives the pin-layer
WARN ("pad exists but is attached to no net") and C04 gives the net-layer
FAIL ("drives MCU pin X but the board leaves it unconnected"). C03
verifies pins exist and resolve; C04 verifies the net those pins ride.
Fixing the net clears both.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from src.engines.crosscheck.record import DesignRecord

PASS = "PASS"
WARN = "WARN"
FAIL = "FAIL"


@dataclass
class CheckResult:
    """One check's verdict. `location` points at the problem (a net, part
    ref, pin, hole, edge); `evidence` carries the structured facts that
    back the verdict."""

    check_id: str
    name: str
    status: str
    reason: str
    location: str = ""
    evidence: dict[str, Any] = field(default_factory=dict)


def _cannot_verify(
    check_id: str, name: str, why: str, location: str = ""
) -> CheckResult:
    return CheckResult(
        check_id=check_id,
        name=name,
        status=WARN,
        reason=f"cannot_verify: {why}",
        location=location,
        evidence={"missing": why},
    )


def _aggregate(
    check_id: str,
    name: str,
    pass_reason: str,
    rows: list[tuple[str, str, str, str]],
    evidence_key: str,
) -> CheckResult:
    """Fold per-item findings into the check's single verdict: FAIL
    dominates WARN dominates PASS, exactly the decided report semantics.

    ``rows`` items are ``(status, reason, location, note)``.
    """
    fails = [row for row in rows if row[0] == FAIL]
    warns = [row for row in rows if row[0] == WARN]
    items = []
    for status, reason, location, note in rows:
        item = {"status": status, "reason": reason, "location": location}
        if note:
            item["note"] = note
        items.append(item)
    evidence = {evidence_key: items}
    if fails:
        return CheckResult(
            check_id=check_id,
            name=name,
            status=FAIL,
            reason=fails[0][1]
            + (
                f"; {len(fails) - 1} more failure(s) in evidence"
                if len(fails) > 1
                else ""
            ),
            location=fails[0][2],
            evidence=evidence,
        )
    if warns:
        return CheckResult(
            check_id=check_id,
            name=name,
            status=WARN,
            reason=warns[0][1]
            + (
                f"; {len(warns) - 1} more warning(s) in evidence"
                if len(warns) > 1
                else ""
            ),
            location=warns[0][2],
            evidence=evidence,
        )
    return CheckResult(
        check_id=check_id,
        name=name,
        status=PASS,
        reason=pass_reason,
        evidence=evidence,
    )


# --- fragment accessors: read the verbatim IR shapes defensively ----------


def _mcu_component(pcb: dict[str, Any]) -> dict[str, Any] | None:
    components = pcb.get("components")
    if not isinstance(components, list):
        return None
    for component in components:
        if isinstance(component, dict) and component.get("type") == "mcu":
            return component
    return None


def _mcu_pin_map(mcu: dict[str, Any]) -> dict[str, dict[str, Any]]:
    pins = mcu.get("pins")
    mapping: dict[str, dict[str, Any]] = {}
    if isinstance(pins, list):
        for pin in pins:
            if isinstance(pin, dict) and isinstance(pin.get("name"), str):
                mapping[pin["name"]] = pin
    return mapping


def _net_index(pcb: dict[str, Any]) -> dict[str, dict[str, Any]]:
    nets = pcb.get("nets")
    index: dict[str, dict[str, Any]] = {}
    if isinstance(nets, list):
        for net in nets:
            if isinstance(net, dict) and isinstance(net.get("name"), str):
                index[net["name"]] = net
    return index


def _counterparts_by_net(
    pcb: dict[str, Any], mcu_ref: str | None
) -> dict[str, list[str]]:
    """Net name -> non-MCU refs, read from the netlist's own ref lists —
    the single source of truth for connectivity (C05's counterpart test).
    Pad-level net strings are not used here: when they disagree with the
    netlist, C04's driven-pad membership check is what reports it."""
    by_net: dict[str, list[str]] = {}
    nets = pcb.get("nets")
    if not isinstance(nets, list):
        return by_net
    for net in nets:
        if not isinstance(net, dict):
            continue
        net_name = net.get("name")
        if not isinstance(net_name, str) or not net_name:
            continue
        refs = net.get("pins")
        if not isinstance(refs, list):
            continue
        roots: list[str] = []
        for ref in refs:
            if not isinstance(ref, str) or not ref:
                continue
            root = ref.split(".", 1)[0]
            if mcu_ref is not None and root == mcu_ref:
                continue
            if root not in roots:
                roots.append(root)
        by_net[net_name] = roots
    return by_net


def _mapping_rows(firmware: dict[str, Any]) -> list[dict[str, Any]]:
    mappings = firmware.get("pin_mappings")
    if not isinstance(mappings, list):
        return []
    return [item for item in mappings if isinstance(item, dict)]


def _board_ready(
    record: DesignRecord, check_id: str, name: str
) -> tuple[dict[str, Any], dict[str, Any], dict[str, dict[str, Any]]
          ] | None:
    """Shared precondition for C03/C04/C05: pcb + firmware sections
    present and an MCU component to resolve against. Returns
    ``(pcb, firmware, {pin name -> pin})`` or ``None`` after reporting
    the honest WARN refusal; callers order the tuple unpack explicitly.
    """
    if record.pcb is None or record.firmware is None:
        return None
    mcu = _mcu_component(record.pcb)
    if mcu is None:
        return None
    return record.pcb, record.firmware, _mcu_pin_map(mcu)


def _board_refusal(
    record: DesignRecord, check_id: str, name: str
) -> CheckResult:
    """The cannot_verify reason for what `_board_ready` found missing."""
    if record.pcb is None or record.firmware is None:
        return _cannot_verify(
            check_id, name, "the record lacks a pcb or firmware section"
        )
    return _cannot_verify(
        check_id,
        name,
        "the board populates no MCU-typed component to resolve firmware "
        "pins against (C02 reports this defect)",
        location="pcb.components",
    )


# --- the checks -----------------------------------------------------------


def _check_c01(record: DesignRecord) -> CheckResult:
    missing = record.absent_required_sections()
    if missing:
        return CheckResult(
            check_id="C01",
            name="record_complete",
            status=FAIL,
            reason=(
                "required design-record section(s) missing: "
                + ", ".join(missing)
            ),
            location=missing[0],
            evidence={"missing": missing},
        )
    return CheckResult(
        check_id="C01",
        name="record_complete",
        status=PASS,
        reason=(
            "all required sections present (pcb, firmware, holes, "
            "connectors, enclosure)"
        ),
        evidence={"missing": []},
    )


def _check_c02(record: DesignRecord) -> CheckResult:
    id_, name = "C02", "mcu_matches_board"
    if record.pcb is None or record.firmware is None:
        return _cannot_verify(
            id_, name, "the record lacks a pcb or firmware section"
        )
    fw_mcu = record.firmware.get("mcu")
    fw_mcu = fw_mcu if isinstance(fw_mcu, dict) else {}
    model = str(fw_mcu.get("model", "") or "").strip()
    if not model:
        return _cannot_verify(
            id_,
            name,
            "firmware.mcu.model is empty; nothing to match against the "
            "board",
            location="firmware.mcu",
        )
    mcu = _mcu_component(record.pcb)
    if mcu is None:
        return CheckResult(
            check_id=id_,
            name=name,
            status=FAIL,
            reason=(
                f"firmware names MCU '{model}' but the board populates "
                "no MCU-typed component"
            ),
            location="pcb.components",
            evidence={"firmware_model": model},
        )
    ref = str(mcu.get("ref", "?"))
    value = str(mcu.get("value", "")).strip()
    family = str(fw_mcu.get("family", "") or "").strip()
    if model.lower() == value.lower():
        return CheckResult(
            check_id=id_,
            name=name,
            status=PASS,
            reason=(
                f"firmware MCU '{model}' matches board component "
                f"{ref} ('{value}')"
            ),
            location=ref,
            evidence={
                "firmware_model": model,
                "board_value": value,
                "board_ref": ref,
                "family": family,
            },
        )
    # Same part line at different package granularity ("ESP32" vs
    # "ESP32-WROOM-32") is a WARN; a different family variant
    # ("ESP32-C6" vs "ESP32-WROOM-32") is a different part: FAIL.
    if value.lower().startswith(model.lower()) or model.lower().startswith(
        value.lower()
    ):
        return CheckResult(
            check_id=id_,
            name=name,
            status=WARN,
            reason=(
                f"same MCU part line, but firmware model '{model}' and "
                f"board component '{value}' name different packages"
            ),
            location=ref,
            evidence={
                "firmware_model": model,
                "board_value": value,
                "family": family,
            },
        )
    return CheckResult(
        check_id=id_,
        name=name,
        status=FAIL,
        reason=(
            f"firmware MCU '{model}' does not match board component "
            f"{ref} ('{value}')"
        ),
        location=ref,
        evidence={
            "firmware_model": model,
            "board_value": value,
            "family": family,
        },
    )


def _check_c03(record: DesignRecord) -> CheckResult:
    id_, name = "C03", "pins_exist_on_board"
    ready = _board_ready(record, id_, name)
    if ready is None:
        return _board_refusal(record, id_, name)
    pcb, firmware, pin_map = ready
    mcu = _mcu_component(pcb)
    assert mcu is not None
    mcu_ref = str(mcu.get("ref", "?"))
    rows: list[tuple[str, str, str, str]] = []
    for item in _mapping_rows(firmware):
        mcu_pin = str(item.get("mcu_pin", ""))
        function = str(item.get("function", "")) or "unnamed function"
        location = f"{mcu_ref}.{mcu_pin}"
        pin = pin_map.get(mcu_pin)
        if pin is None:
            rows.append((
                FAIL,
                (
                    f"firmware maps '{function}' to MCU pin "
                    f"'{mcu_pin}', but the board's MCU component has no "
                    "pin by that name"
                ),
                location,
                "",
            ))
        elif not str(pin.get("net", "")):
            rows.append((
                WARN,
                (
                    f"pin '{mcu_pin}' exists on the board but is attached "
                    "to no net (floating)"
                ),
                location,
                f"function '{function}'",
            ))
        else:
            rows.append((
                PASS,
                f"pin '{mcu_pin}' resolves on the board",
                location,
                f"net '{pin.get('net')}'",
            ))
    if not rows:
        return _cannot_verify(id_, name, "the firmware maps no pins",
                              "firmware")
    return _aggregate(
        id_,
        name,
        "every firmware-mapped pin resolves on the board's MCU component",
        rows,
        "pins",
    )


def _check_c04(record: DesignRecord) -> CheckResult:
    id_, name = "C04", "pins_match_named_nets"
    ready = _board_ready(record, id_, name)
    if ready is None:
        return _board_refusal(record, id_, name)
    pcb, firmware, pin_map = ready
    mcu = _mcu_component(pcb)
    assert mcu is not None
    mcu_ref = str(mcu.get("ref", "?"))
    net_index = _net_index(pcb)
    rows: list[tuple[str, str, str, str]] = []
    for item in _mapping_rows(firmware):
        mcu_pin = str(item.get("mcu_pin", ""))
        function = str(item.get("function", "")) or "unnamed function"
        pin = pin_map.get(mcu_pin)
        if pin is None:
            # C03 already fails this pin; the net layer has nothing to
            # say without a resolved pad.
            continue
        location = f"{mcu_ref}.{mcu_pin}"
        net_name = str(pin.get("net", ""))
        if not net_name:
            rows.append((
                FAIL,
                (
                    f"'{function}' drives MCU pin '{mcu_pin}' but the "
                    "board leaves it unconnected (no named net)"
                ),
                location,
                (
                    "float reported as C03's WARN; the drive itself "
                    "cannot function"
                ),
            ))
        elif net_name not in net_index:
            rows.append((
                FAIL,
                (
                    f"pin '{mcu_pin}' carries net '{net_name}' but no net "
                    "by that name is declared in the board netlist"
                ),
                location,
                f"function '{function}'",
            ))
        else:
            net = net_index[net_name]
            refs = [
                str(r) for r in net.get("pins", []) if isinstance(r, str)
            ]
            pin_number = str(pin.get("number", ""))
            pin_ref = f"{mcu_ref}.{pin_number}" if pin_number else ""
            if pin_ref and pin_ref not in refs:
                rows.append((
                    FAIL,
                    (
                        f"net '{net_name}' declares {refs} but not the "
                        f"driven pad '{pin_ref}'"
                    ),
                    location,
                    f"function '{function}'",
                ))
                continue
            if len(refs) < 2:
                where = refs[0] if refs else "none"
                rows.append((
                    WARN,
                    (
                        f"net '{net_name}' has a single attachment "
                        f"({where}) and connects to nothing else"
                    ),
                    location,
                    f"function '{function}'",
                ))
            else:
                rows.append((
                    PASS,
                    (
                        f"pin '{mcu_pin}' sits on named net '{net_name}' "
                        f"({len(refs)} attachments)"
                    ),
                    location,
                    f"function '{function}'",
                ))
    if not rows:
        return _cannot_verify(id_, name, "the firmware maps no pins",
                              "firmware")
    return _aggregate(
        id_,
        name,
        "every driven pin sits on a named net with real connectivity",
        rows,
        "pins",
    )


def _check_c05(record: DesignRecord) -> CheckResult:
    id_, name = "C05", "peripherals_on_board"
    ready = _board_ready(record, id_, name)
    if ready is None:
        return _board_refusal(record, id_, name)
    pcb, firmware, pin_map = ready
    mcu = _mcu_component(pcb)
    assert mcu is not None
    mcu_ref = str(mcu.get("ref", "?"))
    counterparts = _counterparts_by_net(pcb, mcu_ref)
    # Group the mappings by the peripheral instance the firmware names
    # ("I2C0", "UART0", ...). Plain GPIO pins (no instance) carry no
    # counterpart contract, so they are not peripherals.
    mappings = _mapping_rows(firmware)
    groups: dict[str, list[int]] = {}
    for index, item in enumerate(mappings):
        instance = str(item.get("peripheral", ""))
        if instance:
            groups.setdefault(instance, []).append(index)
    rows: list[tuple[str, str, str, str]] = []
    for instance, indexes in groups.items():
        functions = sorted({
            str(mappings[i].get("function", "")) or "unnamed function"
            for i in indexes
        })
        unresolved = sorted({
            str(mappings[i].get("mcu_pin", ""))
            for i in indexes
            if str(mappings[i].get("mcu_pin", "")) not in pin_map
        })
        nets = sorted({
            str(pin_map[str(mappings[i].get("mcu_pin", ""))].get("net", ""))
            for i in indexes
            if str(mappings[i].get("mcu_pin", "")) in pin_map
        } - {""})
        net_pads = {
            net: sorted(set(counterparts.get(net, []))) for net in nets
        }
        missing = [net for net, pads in net_pads.items() if not pads]
        note = (
            f"nets {net_pads}"
            + (f"; unresolved pins {unresolved}" if unresolved else "")
        )
        if unresolved:
            rows.append((
                WARN,
                (
                    f"peripheral '{instance}' has pins the board cannot "
                    f"resolve ({', '.join(unresolved)}); C03 reports "
                    "existence separately"
                ),
                f"{mcu_ref}.{unresolved[0]}",
                note,
            ))
        elif missing and len(missing) == len(net_pads):
            rows.append((
                FAIL,
                (
                    f"peripheral '{instance}' on "
                    f"{', '.join(functions)} has no counterpart on the "
                    "board: none of its net(s) "
                    f"{', '.join(missing)} terminate on a non-MCU pad"
                ),
                mcu_ref,
                note,
            ))
        elif missing:
            rows.append((
                WARN,
                (
                    f"peripheral '{instance}' counterpart incomplete: "
                    f"net(s) {', '.join(missing)} terminate on the MCU "
                    "pad alone"
                ),
                mcu_ref,
                note,
            ))
        else:
            rows.append((
                PASS,
                (
                    f"peripheral '{instance}' ({', '.join(functions)}) "
                    "finds counterpart pads on all of its net(s)"
                ),
                mcu_ref,
                note,
            ))
    if not groups:
        return _cannot_verify(
            id_, name, "the firmware names no peripherals", "firmware"
        )
    return _aggregate(
        id_,
        name,
        "every firmware peripheral finds a counterpart on the board",
        rows,
        "peripherals",
    )


# --- enclosure geometry (record-side; one mm frame, no invented origins) ---

# C08/C10: an edge distance below this warns (decided tolerance, 0.5 mm).
ENCLOSURE_CLEARANCE_MM = 0.5
# C09: an aligned standoff/board-hole pair may drift this much.
HOLE_DRIFT_MM = 0.2
# C09: beyond this radius a standoff simply has no board hole (the FAIL
# row; the drift WARN band is DRIFT..CAPTURE).
HOLE_CAPTURE_MM = 1.0


def _as_float(value: Any) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return float(value)


def _xy(value: Any) -> tuple[float, float] | None:
    if not isinstance(value, dict):
        return None
    x = _as_float(value.get("x"))
    y = _as_float(value.get("y"))
    if x is None or y is None:
        return None
    return x, y


def _edges(
    inner: tuple[float, float, float, float],
    outer: tuple[float, float, float, float],
) -> dict[str, float]:
    """Clearances of an inner rect inside an outer rect: negative means
    the inner sticks out past that edge."""
    ix0, iy0, ix1, iy1 = inner
    ox0, oy0, ox1, oy1 = outer
    return {
        "left": ix0 - ox0,
        "right": ox1 - ix1,
        "bottom": iy0 - oy0,
        "top": oy1 - iy1,
    }


def _distance(
    a: tuple[float, float], b: tuple[float, float]
) -> float:
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5


def _check_c08(record: DesignRecord) -> CheckResult:
    id_, name = "C08", "board_fits_enclosure"
    if record.pcb is None:
        return _cannot_verify(id_, name, "the record lacks a pcb section")
    if not isinstance(record.enclosure, dict):
        return _cannot_verify(
            id_, name, "the record lacks an enclosure section"
        )
    board = record.pcb.get("board")
    cavity = record.enclosure.get("cavity_mm")
    if not isinstance(board, dict):
        return _cannot_verify(id_, name, "pcb.board is absent", "pcb.board")
    if not isinstance(cavity, dict):
        return _cannot_verify(
            id_, name, "enclosure.cavity_mm is absent",
            "enclosure.cavity_mm",
        )
    bw = _as_float(board.get("width_mm"))
    bh = _as_float(board.get("height_mm"))
    board_origin = _xy(board.get("origin_mm"))
    if bw is None or bh is None or bw <= 0 or bh <= 0:
        return _cannot_verify(
            id_, name,
            "pcb.board lacks a usable width_mm/height_mm", "pcb.board",
        )
    if board_origin is None:
        return _cannot_verify(
            id_, name, "pcb.board.origin_mm is absent", "pcb.board"
        )
    cw = _as_float(cavity.get("width_mm"))
    ch = _as_float(cavity.get("height_mm"))
    cavity_origin = _xy(cavity.get("origin_mm"))
    if cw is None or ch is None or cw <= 0 or ch <= 0:
        return _cannot_verify(
            id_, name,
            "enclosure.cavity_mm lacks a usable width_mm/height_mm",
            "enclosure.cavity_mm",
        )
    if cavity_origin is None:
        # Origin is required: a default position would be an invented
        # value, and C08's whole verdict depends on it.
        return _cannot_verify(
            id_, name,
            "enclosure.cavity_mm.origin_mm is absent; the record states "
            "where the cavity sits, it is not guessed",
            "enclosure.cavity_mm.origin_mm",
        )
    bx0, by0 = board_origin
    cx0, cy0 = cavity_origin
    board_rect = (bx0, by0, bx0 + bw, by0 + bh)
    cavity_rect = (cx0, cy0, cx0 + cw, cy0 + ch)
    edges = _edges(board_rect, cavity_rect)
    tightest = min(edges.values())
    if tightest < 0:
        edge = min(edges, key=edges.get)  # type: ignore[arg-type]
        return CheckResult(
            check_id=id_,
            name=name,
            status=FAIL,
            reason=(
                f"the board sticks out past the cavity's {edge} edge by "
                f"{-tightest:.2f} mm ({bw:.1f}x{bh:.1f} mm board in a "
                f"{cw:.1f}x{ch:.1f} mm cavity)"
            ),
            location=f"board.{edge}",
            evidence={
                "edge_clearances_mm": edges,
                "board_rect_mm": board_rect,
                "cavity_rect_mm": cavity_rect,
            },
        )
    verdict_edge = min(edges, key=edges.get)  # type: ignore[arg-type]
    if tightest >= ENCLOSURE_CLEARANCE_MM:
        return CheckResult(
            check_id=id_,
            name=name,
            status=PASS,
            reason=(
                f"the board sits inside the cavity; the tightest edge "
                f"({verdict_edge}) holds {tightest:.2f} mm clearance"
            ),
            location=f"board.{verdict_edge}",
            evidence={
                "edge_clearances_mm": edges,
                "board_rect_mm": board_rect,
                "cavity_rect_mm": cavity_rect,
            },
        )
    return CheckResult(
        check_id=id_,
        name=name,
        status=WARN,
        reason=(
            f"the board is inside the cavity, but edge '{verdict_edge}' "
            f"holds only {tightest:.2f} mm clearance "
            f"(< {ENCLOSURE_CLEARANCE_MM} mm)"
        ),
        location=f"board.{verdict_edge}",
        evidence={
            "edge_clearances_mm": edges,
            "board_rect_mm": board_rect,
            "cavity_rect_mm": cavity_rect,
        },
    )


def _check_c09(record: DesignRecord) -> CheckResult:
    id_, name = "C09", "mounting_holes_line_up"
    if record.holes is None:
        return _cannot_verify(
            id_, name, "the record lacks a holes section"
        )
    if not isinstance(record.enclosure, dict):
        return _cannot_verify(
            id_, name, "the record lacks an enclosure section"
        )
    standoffs = record.enclosure.get("standoffs")
    if not isinstance(standoffs, list):
        return _cannot_verify(
            id_, name, "enclosure.standoffs is absent or malformed",
            "enclosure.standoffs",
        )
    rows: list[tuple[str, str, str, str]] = []
    malformed_holes = 0
    usable_holes: list[tuple[str, tuple[float, float]]] = []
    for hole in record.holes:
        if not isinstance(hole, dict):
            malformed_holes += 1
            continue
        pos = _xy(hole.get("pos_mm"))
        if pos is None:
            malformed_holes += 1
            continue
        usable_holes.append((str(hole.get("ref", "?hole")), pos))
    posless_standoffs = malformed_standoffs = 0
    for standoff in standoffs:
        if not isinstance(standoff, dict):
            malformed_standoffs += 1
            continue
        sid = str(standoff.get("id", "?standoff"))
        pos = _xy(standoff.get("pos_mm"))
        if pos is None:
            posless_standoffs += 1
            rows.append((
                WARN,
                (
                    f"standoff '{sid}' has no usable pos_mm; it cannot "
                    "be paired with a board hole"
                ),
                sid,
                "",
            ))
            continue
        nearest: tuple[str, float] | None = None
        for href, hpos in usable_holes:
            d = _distance(pos, hpos)
            if nearest is None or d < nearest[1]:
                nearest = (href, d)
        if nearest is None:
            rows.append((
                FAIL,
                (
                    f"standoff '{sid}' has no board hole to pair with: "
                    f"the holes section holds no usable position"
                ),
                sid,
                "",
            ))
        elif nearest[1] <= HOLE_DRIFT_MM:
            rows.append((
                PASS,
                (
                    f"standoff '{sid}' pairs with hole '{nearest[0]}' "
                    f"at {nearest[1]:.3f} mm drift"
                ),
                sid,
                "",
            ))
        elif nearest[1] <= HOLE_CAPTURE_MM:
            rows.append((
                WARN,
                (
                    f"standoff '{sid}' pairs its nearest hole "
                    f"'{nearest[0]}' only at {nearest[1]:.2f} mm drift "
                    f"(> {HOLE_DRIFT_MM} mm)"
                ),
                sid,
                "",
            ))
        else:
            rows.append((
                FAIL,
                (
                    f"standoff '{sid}' has no board hole within "
                    f"{HOLE_CAPTURE_MM:.1f} mm (nearest "
                    f"'{nearest[0]}' at {nearest[1]:.2f} mm)"
                ),
                sid,
                "",
            ))
    if malformed_standoffs:
        rows.append((
            WARN,
            (
                f"{malformed_standoffs} standoff entrie(s) are not "
                "objects; the enclosure section is malformed"
            ),
            "enclosure.standoffs",
            "",
        ))
    if not rows:
        return CheckResult(
            check_id=id_,
            name=name,
            status=PASS,
            reason=(
                f"no standoff/hole pairs to check "
                f"({len(standoffs)} standoffs, {len(usable_holes)} holes"
                + (f", {malformed_holes} malformed hole(s)" if malformed_holes else "")
                + ")"
            ),
            evidence={
                "standoffs": [],
                "counts": {
                    "standoffs": len(standoffs),
                    "paired_holes": len(usable_holes),
                    "malformed_holes": malformed_holes,
                    "posless_standoffs": posless_standoffs,
                },
            },
        )
    result = _aggregate(
        id_,
        name,
        "every enclosure standoff finds a board hole",
        rows,
        "standoffs",
    )
    result.evidence["counts"] = {
        "standoffs": len(standoffs),
        "paired_holes": len(usable_holes),
        "malformed_holes": malformed_holes,
        "posless_standoffs": posless_standoffs,
    }
    return result


def _check_c10(record: DesignRecord) -> CheckResult:
    id_, name = "C10", "connectors_exposed"
    if record.connectors is None:
        return _cannot_verify(
            id_, name, "the record lacks a connectors section"
        )
    if not isinstance(record.enclosure, dict):
        return _cannot_verify(
            id_, name, "the record lacks an enclosure section"
        )
    cutouts = record.enclosure.get("cutouts")
    if not isinstance(cutouts, list):
        return _cannot_verify(
            id_, name, "enclosure.cutouts is absent or malformed",
            "enclosure.cutouts",
        )
    usable_cutouts: list[tuple[str, tuple[float, float, float, float]]] = []
    malformed_cutouts = 0
    for cutout in cutouts:
        if not isinstance(cutout, dict):
            malformed_cutouts += 1
            continue
        cid = str(cutout.get("id", "?cutout"))
        pos = _xy(cutout.get("pos_mm"))
        w = _as_float(cutout.get("width_mm"))
        h = _as_float(cutout.get("height_mm"))
        if pos is None or w is None or h is None or w <= 0 or h <= 0:
            malformed_cutouts += 1
            continue
        x, y = pos
        usable_cutouts.append((cid, (x, y, x + w, y + h)))
    rows: list[tuple[str, str, str, str]] = []
    for connector in record.connectors:
        if not isinstance(connector, dict):
            rows.append((
                WARN,
                "the connectors section holds a non-object entry; it "
                "cannot be checked",
                "connectors",
                "",
            ))
            continue
        ref = str(connector.get("ref", "?connector"))
        pos = _xy(connector.get("pos_mm"))
        w = _as_float(connector.get("width_mm"))
        h = _as_float(connector.get("height_mm"))
        if pos is None or w is None or h is None or w <= 0 or h <= 0:
            rows.append((
                WARN,
                (
                    f"connector '{ref}' has no usable pos_mm/width_mm/"
                    "height_mm; it cannot be checked against a cutout"
                ),
                ref,
                "",
            ))
            continue
        x, y = pos
        conn_rect = (x, y, x + w, y + h)
        containing = []
        for cid, rect in usable_cutouts:
            edges = _edges(conn_rect, rect)
            worst = min(edges.values())
            if worst >= 0:
                containing.append((cid, worst, edges))
        skip_note = (
            f"{malformed_cutouts} malformed cutout(s) skipped"
            if malformed_cutouts else ""
        )
        if containing:
            cid, margin, edges = max(
                containing, key=lambda item: (item[1], item[0])
            )
            if margin >= ENCLOSURE_CLEARANCE_MM:
                rows.append((
                    PASS,
                    (
                        f"connector '{ref}' sits within cutout '{cid}' "
                        f"({margin:.2f} mm edge distance at the tightest "
                        "edge)"
                    ),
                    ref,
                    f"cutout '{cid}'",
                ))
            else:
                rows.append((
                    WARN,
                    (
                        f"connector '{ref}' is inside cutout '{cid}' but "
                        f"its tightest edge holds only {margin:.2f} mm "
                        f"(< {ENCLOSURE_CLEARANCE_MM} mm)"
                    ),
                    ref,
                    f"cutout '{cid}'",
                ))
            continue
        best: tuple[str, float, dict[str, float]] | None = None
        for cid, rect in usable_cutouts:
            edges = _edges(conn_rect, rect)
            stickout = max(-v for v in edges.values())
            if best is None or stickout < best[1]:
                best = (cid, stickout, edges)
        if best is None:
            reason = (
                f"connector '{ref}' has no cutout window: the enclosure "
                "declares no usable cutout"
            )
            if skip_note:
                reason += f" ({skip_note})"
            rows.append((FAIL, reason, ref, ""))
        else:
            outside_edge = min(
                best[2], key=best[2].get  # type: ignore[arg-type]
            )
            reason = (
                f"connector '{ref}' is covered by no cutout; the closest "
                f"window '{best[0]}' leaves it {best[1]:.2f} mm past its "
                f"{outside_edge} edge"
            )
            if skip_note:
                reason += f" ({skip_note})"
            rows.append((
                FAIL,
                reason,
                ref,
                f"connector {w:.1f}x{h:.1f} mm at ({x:.1f}, {y:.1f})",
            ))
    if not rows:
        return CheckResult(
            check_id=id_,
            name=name,
            status=PASS,
            reason=(
                f"no external connectors to expose "
                f"({len(record.connectors)} connectors, "
                f"{len(usable_cutouts)} cutouts)"
            ),
            evidence={
                "connectors": [],
                "counts": {
                    "connectors": len(record.connectors),
                    "usable_cutouts": len(usable_cutouts),
                    "malformed_cutouts": malformed_cutouts,
                },
            },
        )
    result = _aggregate(
        id_,
        name,
        "every external connector sits inside a cutout window",
        rows,
        "connectors",
    )
    result.evidence["counts"] = {
        "connectors": len(record.connectors),
        "usable_cutouts": len(usable_cutouts),
        "malformed_cutouts": malformed_cutouts,
    }
    return result


# --- the C00 gate and the electrical record-side checks -------------------

# C00 speaks the exact status chain src/models/schemas.py owns
# (ValidationLevel). VALIDATED/VERIFIED close the gate; FAILED fails it;
# anything else (a GENERATED-only artifact, say) is a validation the
# record never completed: WARN cannot_verify, never a PASS by default.
GATE_GREEN_STATUSES: tuple[str, ...] = ("VALIDATED", "VERIFIED")
GATE_FAILED_STATUS: str = "FAILED"

# C07: a rail's summed draw above this fraction of capacity warns —
# above 100% of capacity fails (decided tolerance, 80%).
RAIL_WARN_FRACTION = 0.8


def _check_c00(record: DesignRecord) -> CheckResult:
    id_, name = "C00", "engine_checks_green"
    gate = record.engine_checks
    if gate is None:
        return _cannot_verify(
            id_,
            name,
            "the record carries no engine-check gate (engine_checks "
            "absent)",
            "engine_checks",
        )
    if not isinstance(gate, list):
        return _cannot_verify(
            id_, name, "engine_checks is malformed", "engine_checks"
        )
    rows: list[tuple[str, str, str, str]] = []
    for entry in gate:
        if not isinstance(entry, dict):
            rows.append((
                WARN,
                "an engine_checks entry is not an object; it cannot be "
                "a gate verdict",
                "engine_checks",
                "",
            ))
            continue
        artifact = str(entry.get("artifact", "")) or "?artifact"
        op = str(entry.get("op", "")) or "?op"
        status = str(entry.get("status", ""))
        detail = str(entry.get("detail", "") or entry.get("reason", ""))
        where = f"{artifact}.{op}"
        if status in GATE_GREEN_STATUSES:
            rows.append((
                PASS,
                f"engine check {where}: {status}",
                artifact,
                detail,
            ))
        elif status == GATE_FAILED_STATUS:
            rows.append((
                FAIL,
                f"engine check {where} FAILED"
                + (f": {detail}" if detail else ""),
                artifact,
                f"status '{status}'",
            ))
        elif not status:
            rows.append((
                WARN,
                f"engine check {where} carries no status; the gate "
                "cannot weigh it",
                artifact,
                "",
            ))
        else:
            rows.append((
                WARN,
                f"engine check {where} has not completed a validation "
                f"(status: {status})",
                artifact,
                "an engine check the record never made cannot be "
                "passed by default",
            ))
    if not rows:
        return _cannot_verify(
            id_,
            name,
            "engine_checks holds no entries; no engine has validated "
            "anything yet",
            "engine_checks",
        )
    return _aggregate(
        id_,
        name,
        "every recorded engine check is VALIDATED or VERIFIED",
        rows,
        "engine_checks",
    )


def _board_power_nets(pcb: dict[str, Any]) -> set[str]:
    """Nets (as declared in the netlist) that a power-typed pad rides —
    the board's power topology, straight from the pads' electrical
    types. Undeclared pad nets are C04's business and are ignored here.
    """
    known = set(_net_index(pcb))
    nets: set[str] = set()
    components = pcb.get("components")
    if not isinstance(components, list):
        return nets
    for component in components:
        if not isinstance(component, dict):
            continue
        pins = component.get("pins")
        if not isinstance(pins, list):
            continue
        for pin in pins:
            if not isinstance(pin, dict):
                continue
            if str(pin.get("electrical", "")) in ("power_in", "power_out"):
                net = str(pin.get("net", ""))
                if net and net in known:
                    nets.add(net)
    return nets


def _check_c06(record: DesignRecord) -> CheckResult:
    id_, name = "C06", "voltage_domains_compatible"
    if record.pcb is None:
        return _cannot_verify(id_, name, "the record lacks a pcb section")
    if record.power is None:
        return _cannot_verify(
            id_,
            name,
            "the record lacks a power section; rail classes live there",
            "power",
        )
    rails = record.power.get("rails")
    if not isinstance(rails, list):
        return _cannot_verify(
            id_, name, "power.rails is absent or malformed",
            "power.rails",
        )
    board_power = _board_power_nets(record.pcb)
    net_index = _net_index(record.pcb)
    rows: list[tuple[str, str, str, str]] = []
    rails_by_net: dict[str, set[str | None]] = {}
    for rail in rails:
        if not isinstance(rail, dict):
            rows.append((
                WARN,
                "a power.rails entry is not an object; it cannot be "
                "classified",
                "power.rails",
                "",
            ))
            continue
        net_name = str(rail.get("net", "")).strip()
        cls = str(rail.get("class", "")).strip()
        if not net_name:
            rows.append((
                WARN,
                "a rail entry names no net; it cannot be classified",
                "power.rails",
                "",
            ))
            continue
        if net_name not in net_index:
            rows.append((
                FAIL,
                (
                    f"rail '{net_name}' names a net the board netlist "
                    "does not declare"
                ),
                net_name,
                "",
            ))
            continue
        if not cls:
            rows.append((
                WARN,
                (
                    f"power net '{net_name}' is declared but carries no "
                    "rail class (unclassified)"
                ),
                net_name,
                "",
            ))
        elif net_name not in board_power:
            rows.append((
                WARN,
                (
                    f"rail '{net_name}' names a declared net that no "
                    "power-typed pad rides; the board does not show it "
                    "as power"
                ),
                net_name,
                f"declared class '{cls}'",
            ))
        rails_by_net.setdefault(net_name, set()).add(cls or None)
    for net_name, classes in rails_by_net.items():
        if len({c for c in classes if c}) > 1:
            rows.append((
                FAIL,
                (
                    f"power net '{net_name}' carries "
                    f"{len({c for c in classes if c})} classes "
                    f"({', '.join(sorted({c for c in classes if c}))})"
                ),
                net_name,
                "",
            ))
    for net_name in sorted(board_power):
        if net_name not in rails_by_net:
            rows.append((
                WARN,
                (
                    f"power net '{net_name}' is present on the board but "
                    "no rail classifies it"
                ),
                net_name,
                "",
            ))
    mcu_rail = record.power.get("mcu_rail")
    if not isinstance(mcu_rail, dict):
        rows.append((
            WARN,
            (
                "cannot_verify: power.mcu_rail is absent; the MCU's "
                "expected rail cannot be checked"
            ),
            "power.mcu_rail",
            "",
        ))
    else:
        expected = str(mcu_rail.get("class", "")).strip()
        if not expected:
            rows.append((
                WARN,
                (
                    "power.mcu_rail states no rail class; the MCU's "
                    "rail cannot be checked"
                ),
                "power.mcu_rail",
                "",
            ))
        else:
            carriers = sorted(
                net_name
                for net_name, classes in rails_by_net.items()
                if expected in {c for c in classes if c}
                and net_name in board_power
            )
            if carriers:
                rows.append((
                    PASS,
                    (
                        f"the MCU's rail '{expected}' is carried by "
                        f"board power net(s) {', '.join(carriers)}"
                    ),
                    carriers[0],
                    "",
                ))
            else:
                rows.append((
                    FAIL,
                    (
                        f"the MCU's rail '{expected}' is missing from "
                        "the board: no board power net carries it"
                    ),
                    "power.mcu_rail",
                    "",
                ))
    return _aggregate(
        id_,
        name,
        (
            "every board power net carries exactly one rail class and "
            "the MCU's rail exists on the board"
        ),
        rows,
        "nets",
    )


def _riders_by_net(pcb: dict[str, Any]) -> dict[str, set[str]]:
    """Refs (all components, the MCU included) with at least one pad on
    each net — who a rail's draw data must account for."""
    riders: dict[str, set[str]] = {}
    components = pcb.get("components")
    if not isinstance(components, list):
        return riders
    for component in components:
        if not isinstance(component, dict):
            continue
        ref = str(component.get("ref", ""))
        if not ref:
            continue
        pins = component.get("pins")
        if not isinstance(pins, list):
            continue
        for pin in pins:
            if isinstance(pin, dict):
                net = str(pin.get("net", ""))
                if net:
                    riders.setdefault(net, set()).add(ref)
    return riders


def _check_c07(record: DesignRecord) -> CheckResult:
    id_, name = "C07", "current_budget"
    if record.pcb is None:
        return _cannot_verify(id_, name, "the record lacks a pcb section")
    if record.power is None:
        return _cannot_verify(
            id_,
            name,
            "the record lacks a power section; rail draws and "
            "capacities live there",
            "power",
        )
    rails = record.power.get("rails")
    if not isinstance(rails, list):
        return _cannot_verify(
            id_, name, "power.rails is absent or malformed",
            "power.rails",
        )
    if not rails:
        return _cannot_verify(
            id_,
            name,
            "power.rails declares no rails; there is nothing to weigh",
            "power.rails",
        )
    riders = _riders_by_net(record.pcb)
    rows: list[tuple[str, str, str, str]] = []
    for rail in rails:
        if not isinstance(rail, dict):
            rows.append((
                WARN,
                "a power.rails entry is not an object; its draw cannot "
                "be weighed",
                "power.rails",
                "",
            ))
            continue
        net_name = str(rail.get("net", "")).strip() or "?rail"
        capacity = _as_float(rail.get("capacity_ma"))
        if capacity is None or capacity <= 0:
            rows.append((
                WARN,
                (
                    f"rail '{net_name}' declares no usable "
                    "capacity_ma; its draw is not weighed"
                ),
                net_name,
                "",
            ))
            continue
        declared: dict[str, float] = {}
        draws = rail.get("draws")
        if isinstance(draws, list):
            for draw in draws:
                if not isinstance(draw, dict):
                    rows.append((
                        WARN,
                        (
                            f"a draw entry on rail '{net_name}' is not "
                            "an object"
                        ),
                        net_name,
                        "",
                    ))
                    continue
                ref = str(draw.get("ref", "")).strip()
                ma = _as_float(draw.get("draw_ma"))
                if not ref or ma is None:
                    rows.append((
                        WARN,
                        (
                            f"a draw entry on rail '{net_name}' lacks "
                            "ref or draw_ma"
                        ),
                        net_name,
                        "",
                    ))
                    continue
                if ma < 0:
                    rows.append((
                        WARN,
                        (
                            f"draw for '{ref}' on rail '{net_name}' is "
                            "negative and cannot be summed"
                        ),
                        net_name,
                        f"declared {ma:g} mA",
                    ))
                    continue
                if ref not in riders.get(net_name, set()):
                    rows.append((
                        WARN,
                        (
                            f"draw data for '{ref}' on rail "
                            f"'{net_name}', but the board puts no pad "
                            "of it on this net"
                        ),
                        net_name,
                        f"declared {ma:g} mA",
                    ))
                declared[ref] = declared.get(ref, 0.0) + ma
        unmetered = sorted(
            riders.get(net_name, set()) - declared.keys()
        )
        for ref in unmetered:
            rows.append((
                WARN,
                (
                    f"'{ref}' rides rail '{net_name}' on the board but "
                    "declares no draw data"
                ),
                ref,
                "without it the budget is under-counted",
            ))
        total = sum(declared.values())
        ratio = total / capacity
        pct = ratio * 100.0
        if ratio > 1.0:
            rows.append((
                FAIL,
                (
                    f"rail '{net_name}' is over budget: {total:.1f} mA "
                    f"drawn of {capacity:.1f} mA capacity"
                ),
                net_name,
                f"{pct:.0f}% of capacity",
            ))
        elif ratio > RAIL_WARN_FRACTION:
            rows.append((
                WARN,
                (
                    f"rail '{net_name}' runs at {pct:.0f}% of capacity "
                    f"({total:.1f} of {capacity:.1f} mA)"
                ),
                net_name,
                "81-100% of capacity",
            ))
        else:
            rows.append((
                PASS,
                (
                    f"rail '{net_name}' draws {total:.1f} mA of "
                    f"{capacity:.1f} mA ({pct:.0f}%)"
                ),
                net_name,
                "",
            ))
    return _aggregate(
        id_,
        name,
        "every weighed rail holds at 80% of its capacity",
        rows,
        "rails",
    )


# --- the BOM match (C11; the grammar lives in crosscheck.bom) -------------

def _nonempty_text(value: Any) -> str:
    """The string-collapsed view of an attribute (M21.1 convention 9):
    stripped, case-sensitive; an absent field reads as the empty string
    because "absent" is a state the board genuinely has."""
    if value is None:
        return ""
    return str(value).strip()


def _check_c11(record: DesignRecord) -> CheckResult:
    id_, name = "C11", "bom_matches_parts"
    if record.pcb is None:
        return _cannot_verify(id_, name, "the record lacks a pcb section")
    if record.bom is None:
        return _cannot_verify(
            id_,
            name,
            "the record carries no bom section; build one with "
            "bom_from_pcb(record.pcb) or annotate the lines",
            "bom",
        )
    if not isinstance(record.bom, dict):
        return _cannot_verify(id_, name, "the bom section is malformed",
                              "bom")
    components = record.pcb.get("components")
    if not isinstance(components, list):
        return _cannot_verify(
            id_, name, "pcb.components is absent or malformed",
            "pcb.components",
        )
    lines = record.bom.get("lines")
    if not isinstance(lines, list):
        return _cannot_verify(
            id_, name, "bom.lines is absent or malformed", "bom.lines"
        )
    # The board population: ref -> (count, value, package). Duplicate
    # refs compare against their first occurrence; a refless component
    # cannot enter a BOM, so it is a WARN (C06's convention).
    board: dict[str, tuple[int, str, str]] = {}
    rows: list[tuple[str, str, str, str]] = []
    for component in components:
        if not isinstance(component, dict):
            rows.append((
                WARN,
                "a pcb component is not an object; it cannot populate "
                "the BOM",
                "pcb.components",
                "",
            ))
            continue
        ref = _nonempty_text(component.get("ref"))
        if not ref:
            rows.append((
                WARN,
                "a pcb component carries no ref; the BOM cannot name it",
                "pcb.components",
                "",
            ))
            continue
        if ref in board:
            count, value, package = board[ref]
            board[ref] = (count + 1, value, package)
        else:
            board[ref] = (
                1,
                _nonempty_text(component.get("value")),
                _nonempty_text(component.get("footprint")),
            )
    for line in lines:
        if not isinstance(line, dict):
            rows.append((
                WARN,
                "a bom.lines entry is not an object; it cannot be "
                "matched",
                "bom.lines",
                "",
            ))
            continue
        ref = _nonempty_text(line.get("ref"))
        if not ref:
            rows.append((
                WARN,
                "a BOM line names no ref; it cannot match a component",
                "bom.lines",
                "",
            ))
            continue
        populated = board.pop(ref, None)
        if populated is None:
            rows.append((
                FAIL,
                f"BOM line '{ref}' matches no populated component "
                "(extra ref)",
                ref,
                "",
            ))
            continue
        count, board_value, board_package = populated
        mismatches: list[str] = []
        line_value = _nonempty_text(line.get("value"))
        if line_value != board_value:
            mismatches.append(
                f"value '{line_value}' vs '{board_value}'"
            )
        line_package = _nonempty_text(line.get("package"))
        if line_package != board_package:
            mismatches.append(
                f"package '{line_package}' vs '{board_package}'"
            )
        line_quantity = line.get("quantity")
        quantity = _as_float(line_quantity)
        if quantity is None or quantity != count:
            mismatches.append(
                f"quantity {line_quantity!r} vs {count}"
            )
        if mismatches:
            rows.append((
                WARN,
                f"BOM line '{ref}' differs from the populated part: "
                f"{'; '.join(mismatches)}",
                ref,
                "",
            ))
        else:
            rows.append((
                PASS,
                (
                    f"BOM line '{ref}' matches the populated part "
                    f"('{board_value}', package '{board_package}', "
                    f"quantity {count})"
                ),
                ref,
                "",
            ))
    for ref in sorted(board):
        rows.append((
            FAIL,
            f"no BOM line for populated component '{ref}' (missing "
            "ref)",
            ref,
            "",
        ))
    if not rows:
        return CheckResult(
            check_id=id_,
            name=name,
            status=PASS,
            reason=(
                "the board populates no components and the BOM names "
                "none; the ref sets match (both empty)"
            ),
            location="bom",
            evidence={"bom": []},
        )
    return _aggregate(
        id_,
        name,
        "the BOM lines match the populated components 1:1 on ref, "
        "value, package and quantity",
        rows,
        "bom",
    )


CHECK_ORDER: tuple[str, ...] = ("C00", "C01", "C02", "C03", "C04",
                                "C05", "C06", "C07", "C08", "C09",
                                "C10", "C11")
_CHECKS = {
    "C00": _check_c00,
    "C01": _check_c01,
    "C02": _check_c02,
    "C03": _check_c03,
    "C04": _check_c04,
    "C05": _check_c05,
    "C06": _check_c06,
    "C07": _check_c07,
    "C08": _check_c08,
    "C09": _check_c09,
    "C10": _check_c10,
    "C11": _check_c11,
}


OVERALL_VERIFIED = "VERIFIED"
OVERALL_WARNING = "WARNING"
OVERALL_FAILED = "FAILED"


def overall_status(checks: list[CheckResult]) -> str:
    """The report-level triad (`decided.md`, status semantics): VERIFIED
    = every cross-check PASS **and** the engine-check gate green — i.e.
    every check in ``checks`` is a PASS; WARNING = no FAIL but at least
    one WARN; FAILED = any check FAILed (an engine check FAILed is a
    C00 FAIL). WARN never grants VERIFIED, so a cannot_verify WARN
    denies it too."""
    statuses = [result.status for result in checks]
    if FAIL in statuses:
        return OVERALL_FAILED
    if WARN in statuses:
        return OVERALL_WARNING
    return OVERALL_VERIFIED


def status_counts(checks: list[CheckResult]) -> dict[str, int]:
    """The per-verdict tally the report shows next to the status."""
    counts = {PASS: 0, WARN: 0, FAIL: 0}
    for result in checks:
        if result.status in counts:
            counts[result.status] += 1
    return counts


def run_checks(record: DesignRecord) -> list[CheckResult]:
    """Run the implemented cross-checks in the decided order (C00's
    gate through C11's BOM match; this list grows, never reorders)."""
    return [_CHECKS[id_](record) for id_ in CHECK_ORDER]
