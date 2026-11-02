"""Cross-checks C01-C05 (M20.2): the checker the verified report builds on.

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


CHECK_ORDER: tuple[str, ...] = ("C01", "C02", "C03", "C04", "C05")
_CHECKS = {
    "C01": _check_c01,
    "C02": _check_c02,
    "C03": _check_c03,
    "C04": _check_c04,
    "C05": _check_c05,
}


def run_checks(record: DesignRecord) -> list[CheckResult]:
    """Run the implemented cross-checks in the decided order (the C00 gate
    and C06-C11 land in their own milestones; this list grows, never
    reorders)."""
    return [_CHECKS[id_](record) for id_ in CHECK_ORDER]
