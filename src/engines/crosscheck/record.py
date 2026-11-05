"""The design record (M20.1): one shared store of what the engines built.

One record per project: the PCB answer's JSON (parts, nets and pins, board
outline, placements — the ``PcbDesign`` fragment), the firmware answer's
JSON (MCU, pin map, requirements — the ``FirmwareProject`` fragment), and
the record-side sections no IR carries yet (board mounting holes, external
connectors with positions, the enclosure cavity/standoffs/cutouts, and the
BOM once Phase 2 ships the export).

Contract, settled in `decided.md` (Phase 0):

* The builder accepts the engines' existing JSON-shaped fragments plus
  record-side sections as explicit arguments. It stores what it gets and
  invents nothing: an argument that is absent stays absent, so check C01
  can later FAIL the record with the section named. Building
  honestly-incomplete records is deliberate; completeness is the
  checker's job, never the builder's.
* A record persists at ``TRINITY_STORAGE_ROOT/projects/<id>/record.json``
  (files, not DB rows — the M17 data-schema chain is untouched). Writes
  are atomic write-then-replace, the same pattern as the schema marker.
* A stored record this build cannot read is refused, not guessed, with
  the path named — the M17 marker convention, applied to records.
* Nothing here consults a model or advertises an engine op; the checker
  (M20.2+) consumes load() of this record.
"""

from __future__ import annotations

import copy
import json
import os
import re
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from src.core.config import settings

RECORD_SCHEMA_VERSION = 1
RECORDS_DIRNAME = "projects"
RECORD_FILENAME = "record.json"

# The decided rule table's sections (`decided.md`, Phase 0). ``pcb`` is
# the evidence source for parts / nets+pins / board size; ``firmware``
# carries the MCU, pin map and peripherals; the rest are record-side
# annotations until an IR grows the field. ``bom`` stays optional until
# Phase 2 makes it real. ``power`` is the electrical annotation the
# board and firmware IRs do not carry (one class per rail net for C06,
# per-rail draw/capacity for C07); ``engine_checks`` is the C00 gate's
# stored per-artifact engine-validate verdicts. Both stay optional: an
# absent one is the checker's WARN cannot_verify, never an invented
# default.
REQUIRED_SECTIONS: tuple[str, ...] = (
    "pcb",
    "firmware",
    "holes",
    "connectors",
    "enclosure",
)
OPTIONAL_SECTIONS: tuple[str, ...] = ("bom", "power", "engine_checks")

_DICT_SECTIONS: tuple[str, ...] = ("pcb", "firmware", "enclosure", "bom",
                                   "power")
_LIST_SECTIONS: tuple[str, ...] = ("holes", "connectors", "engine_checks")

_RECORD_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")


class DesignRecordError(RuntimeError):
    """The design record cannot be built, written, or read honestly."""


def _utc_now_iso() -> str:
    return datetime.now(UTC).isoformat()


def _validate_record_id(record_id: str) -> None:
    if not isinstance(record_id, str) or not _RECORD_ID_RE.match(record_id):
        raise DesignRecordError(
            f"invalid design record id {record_id!r}; ids are 1-64 chars of "
            "letters, digits, '._-' and may not start with '.' (no paths)"
        )


def records_root(records_root_param: Path | None = None) -> Path:
    """The projects directory: an override, else ``storage_root/projects``."""
    if records_root_param is not None:
        return Path(records_root_param)
    return Path(settings.storage_root) / RECORDS_DIRNAME


def record_path(
    record_id: str, records_root_param: Path | None = None
) -> Path:
    """Where one record lives (path math only — nothing is created)."""
    _validate_record_id(record_id)
    return records_root(records_root_param) / record_id / RECORD_FILENAME


@dataclass
class DesignRecord:
    """One project's cross-checkable record. Sections stay the caller's
    JSON fragments verbatim — the shape is owned by the engines."""

    record_id: str
    pcb: dict[str, Any] | None = None
    firmware: dict[str, Any] | None = None
    holes: list[dict[str, Any]] | None = None
    connectors: list[dict[str, Any]] | None = None
    enclosure: dict[str, Any] | None = None
    bom: dict[str, Any] | None = None
    power: dict[str, Any] | None = None
    engine_checks: list[dict[str, Any]] | None = None
    created_at: str | None = None
    updated_at: str | None = None
    app_version: str | None = None
    record_schema_version: int = RECORD_SCHEMA_VERSION

    def absent_required_sections(self) -> list[str]:
        """Required sections that are absent — data for check C01, not a
        verdict. The checker decides FAIL; this only states the truth."""
        return [
            name
            for name in REQUIRED_SECTIONS
            if getattr(self, name) is None
        ]

    def to_dict(self) -> dict[str, Any]:
        return {
            "record_id": self.record_id,
            "record_schema_version": self.record_schema_version,
            "created_at": self.created_at,
            "updated_at": self.updated_at,
            "app_version": self.app_version,
            "pcb": self.pcb,
            "firmware": self.firmware,
            "holes": self.holes,
            "connectors": self.connectors,
            "enclosure": self.enclosure,
            "bom": self.bom,
            "power": self.power,
            "engine_checks": self.engine_checks,
        }

    @classmethod
    def from_dict(cls, data: Any) -> DesignRecord:
        if not isinstance(data, dict):
            raise DesignRecordError(
                "a design record is a JSON object, got "
                f"{type(data).__name__}; refusing to guess"
            )
        unknown = sorted(set(data) - set(cls.from_dict_allowed_keys()))
        if unknown:
            raise DesignRecordError(
                "unknown design-record key(s) " + ", ".join(unknown)
                + "; refusing to guess what this record contains"
            )
        record_id = data.get("record_id")
        _validate_record_id(record_id if isinstance(record_id, str) else "")
        version = data.get("record_schema_version")
        if not isinstance(version, int) or isinstance(version, bool):
            raise DesignRecordError(
                "record has no integer record_schema_version; refusing to "
                "guess which record shape it uses"
            )
        if version != RECORD_SCHEMA_VERSION:
            raise DesignRecordError(
                f"record was written with record_schema_version {version}; "
                f"this build reads version {RECORD_SCHEMA_VERSION}. "
                "Refusing to guess or downgrade."
            )
        kwargs: dict[str, Any] = {}
        for name in _DICT_SECTIONS:
            kwargs[name] = _check_section(data.get(name), name)
        for name in _LIST_SECTIONS:
            kwargs[name] = _check_section(data.get(name), name)
        for stamp in ("created_at", "updated_at", "app_version"):
            value = data.get(stamp)
            if value is not None and not isinstance(value, str):
                raise DesignRecordError(
                    f"{stamp} must be a string or absent, got "
                    f"{type(value).__name__}"
                )
            kwargs[stamp] = value
        return cls(
            record_id=record_id,
            pcb=kwargs["pcb"],
            firmware=kwargs["firmware"],
            holes=kwargs["holes"],
            connectors=kwargs["connectors"],
            enclosure=kwargs["enclosure"],
            bom=kwargs["bom"],
            power=kwargs["power"],
            engine_checks=kwargs["engine_checks"],
            created_at=kwargs["created_at"],
            updated_at=kwargs["updated_at"],
            app_version=kwargs["app_version"],
            record_schema_version=version,
        )

    @classmethod
    def from_dict_allowed_keys(cls) -> tuple[str, ...]:
        return (
            "record_id",
            "record_schema_version",
            "created_at",
            "updated_at",
            "app_version",
            *_DICT_SECTIONS,
            *_LIST_SECTIONS,
        )


def _check_section(value: Any, name: str) -> Any:
    """Absent is honest (None is stored); present-but-wrong-shaped is an
    error naming the section — never a silently re-typed field."""
    container, shape_name = _SECTION_SHAPES[name]
    if value is None or isinstance(value, container):
        return copy.deepcopy(value)
    raise DesignRecordError(
        f"section '{name}' must be {shape_name}, got {type(value).__name__}"
    )


_SECTION_SHAPES: dict[str, tuple[type, str]] = {
    name: (dict, "an object") for name in _DICT_SECTIONS
}
for _name in _LIST_SECTIONS:
    _SECTION_SHAPES[_name] = (list, "an array")


def build_record(
    record_id: str,
    *,
    pcb: dict[str, Any] | None = None,
    firmware: dict[str, Any] | None = None,
    holes: list[dict[str, Any]] | None = None,
    connectors: list[dict[str, Any]] | None = None,
    enclosure: dict[str, Any] | None = None,
    bom: dict[str, Any] | None = None,
    power: dict[str, Any] | None = None,
    engine_checks: list[dict[str, Any]] | None = None,
) -> DesignRecord:
    """Assemble a record from the answers the engines actually gave.

    Invents nothing: a section not passed stays None in the stored JSON,
    and ``absent_required_sections()`` lists exactly what is missing.
    Fragments are deep-copied so the caller cannot mutate a saved record
    through its own references.
    """
    _validate_record_id(record_id)
    for name, value in (
        ("pcb", pcb),
        ("firmware", firmware),
        ("enclosure", enclosure),
        ("bom", bom),
        ("power", power),
        ("holes", holes),
        ("connectors", connectors),
        ("engine_checks", engine_checks),
    ):
        if value is None or isinstance(value, _SECTION_SHAPES[name][0]):
            continue
        raise DesignRecordError(
            f"section '{name}' must be {_SECTION_SHAPES[name][1]}, got "
            f"{type(value).__name__}"
        )
    return DesignRecord(
        record_id=record_id,
        pcb=copy.deepcopy(pcb),
        firmware=copy.deepcopy(firmware),
        holes=copy.deepcopy(holes),
        connectors=copy.deepcopy(connectors),
        enclosure=copy.deepcopy(enclosure),
        bom=copy.deepcopy(bom),
        power=copy.deepcopy(power),
        engine_checks=copy.deepcopy(engine_checks),
        record_schema_version=RECORD_SCHEMA_VERSION,
    )


def save_record(
    record: DesignRecord, records_root_param: Path | None = None
) -> Path:
    """Stamp and write one record atomically (write-then-replace).

    ``created_at`` is set on the first save and never moved;
    ``updated_at`` is refreshed on every save. The record object passed
    in carries the final stamps so the caller observes the stored state.
    """
    now = _utc_now_iso()
    if record.created_at is None:
        record.created_at = now
    record.updated_at = now
    record.app_version = settings.api_version
    _validate_record_id(record.record_id)
    path = record_path(record.record_id, records_root_param)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(
        json.dumps(record.to_dict(), indent=2) + "\n", encoding="utf-8"
    )
    os.replace(tmp, path)
    return path


def load_record(
    record_id: str, records_root_param: Path | None = None
) -> DesignRecord:
    """Read one record back, refusing everything this build cannot use."""
    _validate_record_id(record_id)
    path = record_path(record_id, records_root_param)
    if not path.exists():
        raise DesignRecordError(
            f"no design record named '{record_id}' at {path}"
        )
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise DesignRecordError(
            f"{path} is unreadable or not valid JSON ({exc}); refusing to "
            "guess what this record contains"
        ) from exc
    try:
        return DesignRecord.from_dict(raw)
    except DesignRecordError as exc:
        raise DesignRecordError(f"{path}: {exc}") from exc


def list_records(
    records_root_param: Path | None = None,
) -> list[dict[str, Any]]:
    """Summaries of every locally stored design record, sorted by id.

    The open menu must state what it cannot read, never hide it: a
    record that cannot be loaded back (corrupt file, unknown keys,
    future schema, invalid id) yields a row with ``error`` carrying the
    load refusal verbatim — silently skipping it would fake an honest
    menu into an empty one. Readable rows carry the save stamps
    verbatim; nothing is computed here (statuses are generated per
    request by the report), nothing guessed.
    """
    root = records_root(records_root_param)
    if not root.is_dir():
        return []
    summaries: list[dict[str, Any]] = []
    for entry in sorted(root.iterdir()):
        if not entry.is_dir():
            continue  # the layout stores one project per directory
        try:
            record = load_record(entry.name, records_root_param)
        except DesignRecordError as exc:
            summaries.append(
                {
                    "record_id": entry.name,
                    "created_at": None,
                    "updated_at": None,
                    "app_version": None,
                    "error": str(exc),
                }
            )
            continue
        summaries.append(
            {
                "record_id": record.record_id,
                "created_at": record.created_at,
                "updated_at": record.updated_at,
                "app_version": record.app_version,
            }
        )
    return summaries
