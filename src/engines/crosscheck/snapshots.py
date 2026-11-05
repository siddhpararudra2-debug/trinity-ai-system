"""Snapshots (M21.3, decided.md convention 10): the history is every
state a save displaced.

``record.py``'s ``save_record`` snapshots the displaced state before an
overwrite (reason ``save``); a restore snapshots what it displaces
(reason ``restore``) and then writes the chosen snapshot's record
verbatim — stamps included, because a restored record is supposed to be
*that* version, not a re-stamped cousin of it. One snapshot per file:

    projects/<id>/snapshots/<nnnn>.json
    {snapshot_id: int, taken_at: iso, reason: str, record: {...}}

Snapshots load through the same refusal rules as records — a snapshot
this build cannot read back is never silently restored or silently
hidden from the history — and the compare of two snapshots is
section-level canonical JSON across the decided dimensions.
"""

from __future__ import annotations

import json
import os
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from src.engines.crosscheck.record import (
    DesignRecord,
    DesignRecordError,
    _validate_record_id,
    load_record,
    record_path,
    write_record_json,
)

__all__ = [
    "ABSENT_PIN_REF",
    "SNAPSHOTS_DIRNAME",
    "compare_snapshots",
    "list_snapshots",
    "load_snapshot",
    "restore_snapshot",
    "snapshot_path",
    "snapshots_dir",
    "take_snapshot",
]

SNAPSHOTS_DIRNAME = "snapshots"

# A quoted value in a compare row is serialized JSON, capped so one
# huge net list cannot flood the report — capped text, never dropped
# conclusions (what changed is stated, and where the truncation is).
_QUOTE_MAX = 240

# The pins diff quotes absence explicitly: a ref missing from one
# snapshot reads this marker on that side — chosen so it cannot be a
# legitimate pins fragment (a list), never silently collapsing
# "component absent" into "component with no pins".
ABSENT_PIN_REF = "(component absent from this snapshot)"


def snapshots_dir(
    record_id: str, records_root_param: Path | None = None
) -> Path:
    _validate_record_id(record_id)
    return (
        record_path(record_id, records_root_param).parent
        / SNAPSHOTS_DIRNAME
    )


def snapshot_path(
    record_id: str,
    snapshot_id: int,
    records_root_param: Path | None = None,
) -> Path:
    if not isinstance(snapshot_id, int) or isinstance(snapshot_id, bool):
        snapshot_id = -1
    if snapshot_id < 1:
        raise DesignRecordError(
            f"invalid snapshot id {snapshot_id!r}; ids are 1, 2, 3, ..."
        )
    return snapshots_dir(record_id, records_root_param) / (
        f"{snapshot_id:04d}.json"
    )


def take_snapshot(
    record: DesignRecord,
    *,
    reason: str,
    records_root_param: Path | None = None,
) -> int:
    """Append one snapshot of this record's state; returns its id.

    Ids are the next number after the highest existing snapshot slot,
    so a deleted middle state can never make a newer file reuse its
    id (an id names a state, not a position).
    """
    directory = snapshots_dir(record.record_id, records_root_param)
    numbers: list[int] = []
    if directory.is_dir():
        for entry in directory.glob("*.json"):
            try:
                numbers.append(int(entry.stem))
            except ValueError:
                continue  # not a slot; the numbering disagrees with nothing
    snapshot_id = max(numbers, default=0) + 1
    payload = {
        "snapshot_id": snapshot_id,
        "taken_at": datetime.now(UTC).isoformat(),
        "reason": reason,
        "record": record.to_dict(),
    }
    directory.mkdir(parents=True, exist_ok=True)
    target = snapshot_path(
        record.record_id, snapshot_id, records_root_param
    )
    tmp = target.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, target)
    return snapshot_id


def load_snapshot(
    record_id: str,
    snapshot_id: int,
    records_root_param: Path | None = None,
) -> dict[str, Any]:
    """Read one snapshot back under the records' refusal rules.

    The shape is exact (``snapshot_id`` must agree with the slot it
    was loaded from), and the carried record must be one this build
    can still read — refusals name the file, never guess.
    """
    path = snapshot_path(record_id, snapshot_id, records_root_param)
    if not path.exists():
        raise DesignRecordError(
            f"no snapshot {snapshot_id} for record '{record_id}' at {path}"
        )
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise DesignRecordError(
            f"{path} is unreadable or not valid JSON ({exc}); refusing "
            "to guess what this snapshot contains"
        ) from exc
    if not isinstance(raw, dict):
        raise DesignRecordError(
            f"{path}: a snapshot is an object, got {type(raw).__name__}"
        )
    snapshot_id_stored = raw.get("snapshot_id")
    if (
        not isinstance(snapshot_id_stored, int)
        or isinstance(snapshot_id_stored, bool)
    ):
        raise DesignRecordError(
            f"{path}: snapshot_id must be an integer, got "
            f"{type(snapshot_id_stored).__name__}"
        )
    if snapshot_id_stored != snapshot_id:
        raise DesignRecordError(
            f"{path}: says snapshot_id {snapshot_id_stored}, stored as "
            f"{snapshot_id}; refusing to guess which state this is"
        )
    for key in ("taken_at", "reason"):
        value = raw.get(key)
        if value is not None and not isinstance(value, str):
            raise DesignRecordError(
                f"{path}.{key} must be a string or absent, got "
                f"{type(value).__name__}"
            )
    record_data = raw.get("record")
    if not isinstance(record_data, dict):
        raise DesignRecordError(
            f"{path}: snapshot record is absent or not an object "
            f"({type(record_data).__name__})"
        )
    DesignRecord.from_dict(record_data)  # the read-back it promises
    return raw


def list_snapshots(
    record_id: str, records_root_param: Path | None = None
) -> list[dict[str, Any]]:
    """History rows for one record, oldest first.

    A snapshot slot that cannot be read back is listed with its
    refusal (the menu convention — hiding a history entry would fake
    an honest history into a shorter one). An empty list means no
    save has displaced anything yet.
    """
    directory = snapshots_dir(record_id, records_root_param)
    if not directory.is_dir():
        return []
    rows: list[dict[str, Any]] = []
    for entry in sorted(directory.glob("*.json")):
        try:
            number = int(entry.stem)
        except ValueError:
            rows.append(
                {
                    "snapshot_id": None,
                    "taken_at": None,
                    "reason": None,
                    "created_at": None,
                    "updated_at": None,
                    "app_version": None,
                    "error": (
                        f"{entry.name}: not a snapshot slot (ids are "
                        "numeric)"
                    ),
                }
            )
            continue
        try:
            payload = load_snapshot(record_id, number, records_root_param)
        except DesignRecordError as exc:
            rows.append(
                {
                    "snapshot_id": number,
                    "taken_at": None,
                    "reason": None,
                    "created_at": None,
                    "updated_at": None,
                    "app_version": None,
                    "error": str(exc),
                }
            )
            continue
        record_data = payload.get("record") or {}
        rows.append(
            {
                "snapshot_id": payload["snapshot_id"],
                "taken_at": payload["taken_at"],
                "reason": payload["reason"],
                "created_at": record_data.get("created_at"),
                "updated_at": record_data.get("updated_at"),
                "app_version": record_data.get("app_version"),
                "error": None,
            }
        )
    return rows


def restore_snapshot(
    record_id: str,
    snapshot_id: int,
    records_root_param: Path | None = None,
) -> DesignRecord:
    """Restore one snapshot as the record's live state.

    Nothing is destroyed on the way: the currently stored state —
    if one exists and can be read — is snapshotted first (reason
    ``restore``), then the chosen snapshot's record is written
    verbatim, stamps included. Restoring onto a record whose file
    cannot be read back is refused (convention 10's overwrite rule:
    never displace the only readable copy); restoring onto a record
    with no file at all is the repair path and needs no displacement.
    """
    payload = load_snapshot(record_id, snapshot_id, records_root_param)
    record = DesignRecord.from_dict(payload["record"])
    if record.record_id != record_id:
        raise DesignRecordError(
            f"snapshot {snapshot_id} of record '{record_id}' carries "
            f"record '{record.record_id}'; refusing to guess which "
            "one it belongs to"
        )
    path = record_path(record_id, records_root_param)
    if path.exists():
        displaced = load_record(record_id, records_root_param)
        take_snapshot(
            displaced,
            reason="restore",
            records_root_param=records_root_param,
        )
    write_record_json(record, path)
    return record


# --- compare: section-level canonical JSON, decided dimensions ------------


def _canonical(value: Any) -> str:
    """The one canonical serialization: sort_keys does the work."""
    return json.dumps(value, indent=2, sort_keys=True)


def _quote(value: Any) -> str:
    """A quoted diff value, capped so one huge net list cannot flood
    the report — the cap is stated, never hidden by silence."""
    text = _canonical(value)
    if len(text) > _QUOTE_MAX:
        text = text[:_QUOTE_MAX] + "...(truncated)"
    return text


def _pcb_of(record: DesignRecord) -> dict[str, Any] | None:
    return record.pcb if isinstance(record.pcb, dict) else None


def _board_of(record: DesignRecord) -> Any:
    pcb = _pcb_of(record)
    return None if pcb is None else pcb.get("board")


def _nets_of(record: DesignRecord) -> Any:
    pcb = _pcb_of(record)
    return None if pcb is None else pcb.get("nets")


def _pin_parts(
    first: DesignRecord, second: DesignRecord
) -> list[tuple[str, Any, Any]]:
    """Each populated component's pins, keyed by ref in board order.

    A repeated ref gets ``ref#2`` for its second instance; a refless
    component is keyed ``(no ref)`` with the same numbering — and a
    component that is not even an object compares under its index,
    because refusing to diff a malformed fragment would be hiding a
    real difference.
    """
    def keyed(record: DesignRecord) -> dict[str, Any]:
        pcb = _pcb_of(record)
        if pcb is None:
            return {}
        components = pcb.get("components")
        if not isinstance(components, list):
            # A present-but-malformed components list is a stated
            # value too — compare it as one.
            return {"pcb.components": components}
        keys: dict[str, Any] = {}
        seen: dict[str, int] = {}
        for index, component in enumerate(components):
            if not isinstance(component, dict):
                keys[f"(component {index})"] = component
                continue
            ref = component.get("ref")
            base = str(ref) if ref is not None else "(no ref)"
            count = seen.get(base, 0) + 1
            seen[base] = count
            key = base if count == 1 else f"{base}#{count}"
            keys[key] = component.get("pins")
        return keys

    first_pins = keyed(first)
    second_pins = keyed(second)
    parts: list[tuple[str, Any, Any]] = []
    for key in sorted(set(first_pins) | set(second_pins)):
        # `.get` cannot be trusted here: a ref present with no pins
        # key (None) must differ from a ref that is absent entirely.
        first_value = (
            first_pins[key] if key in first_pins else ABSENT_PIN_REF
        )
        second_value = (
            second_pins[key] if key in second_pins else ABSENT_PIN_REF
        )
        parts.append((key, first_value, second_value))
    return parts


def _dimension_diffs(
    first: DesignRecord, second: DesignRecord
) -> dict[str, dict[str, Any]]:
    """The decided dimensions, each compared part by part."""
    groups: dict[str, list[tuple[str, Any, Any]]] = {
        "geometry": [
            ("pcb.board", _board_of(first), _board_of(second)),
            ("holes", first.holes, second.holes),
            ("connectors", first.connectors, second.connectors),
            ("enclosure", first.enclosure, second.enclosure),
        ],
        "nets": [("pcb.nets", _nets_of(first), _nets_of(second))],
        "pins": _pin_parts(first, second),
        "firmware": [("firmware", first.firmware, second.firmware)],
    }
    dimensions: dict[str, dict[str, Any]] = {}
    for name, parts in groups.items():
        changed = []
        for label, first_value, second_value in parts:
            if _canonical(first_value) != _canonical(second_value):
                changed.append(
                    {
                        "part": label,
                        "first": _quote(first_value),
                        "second": _quote(second_value),
                    }
                )
        dimensions[name] = (
            {"status": "identical"}
            if not changed
            else {"status": "changed", "parts": changed}
        )
    return dimensions


def compare_snapshots(
    record_id: str,
    first: int,
    second: int,
    records_root_param: Path | None = None,
) -> dict[str, Any]:
    """Diff two snapshots across the decided dimensions (convention 10).

    Every dimension is stated — ``identical`` or ``changed`` with the
    changed parts quoted on both sides; absent means absent (a section
    one snapshot has and the other lacks is a change, not a skip).
    """
    first_payload = load_snapshot(record_id, first, records_root_param)
    second_payload = load_snapshot(record_id, second, records_root_param)
    first_record = DesignRecord.from_dict(first_payload["record"])
    second_record = DesignRecord.from_dict(second_payload["record"])
    return {
        "record_id": record_id,
        "first": {
            "snapshot_id": first,
            "taken_at": first_payload["taken_at"],
            "reason": first_payload["reason"],
            "record_updated_at": first_record.updated_at,
        },
        "second": {
            "snapshot_id": second,
            "taken_at": second_payload["taken_at"],
            "reason": second_payload["reason"],
            "record_updated_at": second_record.updated_at,
        },
        "dimensions": _dimension_diffs(first_record, second_record),
    }
