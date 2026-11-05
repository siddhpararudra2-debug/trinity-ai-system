"""The BOM grammar (M21.1): board-derived lines + CSV export.

The record's ``bom`` section is ``{lines[]}`` with per-ref lines
``{ref, part, value, quantity, package}`` (decided.md convention 9 —
one line per populated component ref). ``part`` is the annotation
nobody can invent from the IR: the board carries no MPN, so the
board-derived builder leaves it ``""`` and C11 compares only what the
board genuinely knows (refs, value, package, quantity — never part).

The export is stdlib ``csv`` with exactly the decided columns, no
tricks attached: no quoting dialect changes, no comments, no computed
cells. Round-trip is the acceptance contract (parse-back reproduces the
lines); XLSX ships only behind a real reader/writer library — never
hand-rolled bytes called an xlsx.
"""

from __future__ import annotations

import csv
from pathlib import Path
from typing import Any

from src.engines.crosscheck.record import (
    DesignRecord,
    DesignRecordError,
)

__all__ = [
    "BOM_COLUMNS",
    "PRICED_BOM_COLUMNS",
    "bom_from_pcb",
    "export_bom_csv",
    "export_priced_bom_csv",
    "load_bom_csv",
]

# The decided export columns (M21.1); their order is the header order.
BOM_COLUMNS: tuple[str, ...] = (
    "ref",
    "part",
    "value",
    "quantity",
    "package",
)

# M24 (Phase 5): the quote-shaped extension. It carries every decided
# column plus the price provenance — but only from the parts catalog.
PRICED_BOM_COLUMNS: tuple[str, ...] = (
    "ref",
    "part",
    "value",
    "quantity",
    "package",
    "price_inr",
    "price_updated_on",
)

BOM_LINES_KEY = "lines"


def bom_from_pcb(pcb: dict[str, Any] | None) -> dict[str, Any]:
    """Derive the BOM section from the board's populated components.

    One line per component, in board order: ``ref`` from the ref,
    ``value``/``package`` from the component (an absent one stays an
    empty string — stated as absent, never invented), ``quantity`` 1
    (one populated instance per ref), and ``part`` always ``""`` — the
    MPN is record-side annotation, added by a human or a later
    pipeline, never guessed from the board.

    Deep-copies nothing from ``pcb``: every scalar is rebuilt.
    """
    if pcb is None:
        raise DesignRecordError(
            "bom_from_pcb needs the record's pcb section"
        )
    if not isinstance(pcb, dict):
        raise DesignRecordError(
            f"bom_from_pcb needs a dict pcb section, got "
            f"{type(pcb).__name__}"
        )
    components = pcb.get("components")
    if not isinstance(components, list):
        raise DesignRecordError(
            "pcb.components is absent or malformed; the board's "
            "population is unknown, so no BOM can be derived"
        )
    lines: list[dict[str, Any]] = []
    for index, component in enumerate(components):
        if not isinstance(component, dict):
            raise DesignRecordError(
                f"pcb.components[{index}] is not an object; the BOM "
                "cannot derive a line for it"
            )
        ref = str(component.get("ref", "")).strip()
        if not ref:
            raise DesignRecordError(
                f"pcb.components[{index}] carries no ref; a BOM line "
                "cannot be built for a nameless component"
            )
        lines.append(
            {
                "ref": ref,
                "part": "",
                "value": _text(component.get("value")),
                "quantity": 1,
                "package": _text(component.get("footprint")),
            }
        )
    return {BOM_LINES_KEY: lines}


def _load_bom_section(record: DesignRecord) -> list[dict[str, Any]]:
    """The record's bom lines, refusing everything un-exportable.

    Refusal, not leniency: the exporter exists to produce the one file
    a purchasing conversation trusts, so a malformed section is an
    error naming the problem, never a quietly truncated CSV.
    """
    bom = record.bom
    if bom is None:
        raise DesignRecordError(
            "the record carries no bom section; build one with "
            "bom_from_pcb(record.pcb) or annotate the desired lines "
            "before exporting"
        )
    if not isinstance(bom, dict):
        raise DesignRecordError(
            f"the record's bom section is not an object "
            f"({type(bom).__name__})"
        )
    lines = bom.get(BOM_LINES_KEY)
    if not isinstance(lines, list):
        raise DesignRecordError(
            f"bom.{BOM_LINES_KEY} is absent or malformed; nothing to "
            "export"
        )
    for index, line in enumerate(lines):
        if not isinstance(line, dict):
            raise DesignRecordError(
                f"bom.lines[{index}] is not an object"
            )
    return lines


def export_bom_csv(record: DesignRecord, target: Path) -> Path:
    """Write the record's BOM as CSV and return the path.

    Header row = BOM_COLUMNS exactly; one row per ``bom.lines`` entry;
    a line field the record does not state becomes an empty cell (the
    source genuinely omitted it — an empty cell states absence, it does
    not invent a value). Cells must be scalars: an object or list in a
    cell would stringify into junk a spreadsheet would swallow as text.
    """
    lines = _load_bom_section(record)
    target = Path(target)
    if target.parent != Path("."):
        target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(BOM_COLUMNS)
        for index, line in enumerate(lines):
            row = []
            for column in BOM_COLUMNS:
                value = line.get(column)
                if value is None:
                    row.append("")
                    continue
                if isinstance(value, (dict, list, tuple, set)):
                    raise DesignRecordError(
                        f"bom.lines[{index}].{column} is "
                        f"{type(value).__name__}; a CSV cell must be a "
                        "scalar"
                    )
                row.append(value)
            writer.writerow(row)
    return target


def export_priced_bom_csv(record: DesignRecord, target: Path) -> Path:
    """Write the record's priced BOM as CSV and return the path.

    Header row = PRICED_BOM_COLUMNS exactly; one row per ``bom.lines``
    entry.  If a catalog price exists for the line (matched by ``part``,
    ``value``, or ``ref`` in that order, first match wins, case-insensitive),
    the row states the price (INR) and the date it was last stated;
    otherwise both cells are left blank (\"absent elsewhere\") — never
    presumed or invented.

    Uses the same ``PriceBook`` lookup as the catalog module, so priced
    and unpriced lines are handled consistently and the exact-columns
    BOM export contract is untouched.
    """
    from src.engines.parts.catalog import PriceBook, load_parts

    lines = _load_bom_section(record)
    target = Path(target)
    if target.parent != Path("."):
        target.parent.mkdir(parents=True, exist_ok=True)
    parts_catalog = load_parts()
    price_book = PriceBook(parts_catalog)
    with target.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(PRICED_BOM_COLUMNS)
        for index, line in enumerate(lines):
            row = []
            for column in BOM_COLUMNS:
                value = line.get(column)
                if value is None:
                    row.append("")
                    continue
                if isinstance(value, (dict, list, tuple, set)):
                    raise DesignRecordError(
                        f"bom.lines[{index}].{column} is "
                        f"{type(value).__name__}; a CSV cell must be a "
                        "scalar"
                    )
                row.append(value)
            # Add price columns: matched price or blank
            price_info = price_book.lookup(line)
            if price_info is not None:
                row.append(price_info[0])  # price_inr as float
                row.append(price_info[1])  # price_updated_on as string
            else:
                row.append("")  # price_inr absent
                row.append("")  # price_updated_on absent
            writer.writerow(row)
    return target


def load_bom_csv(path: Path) -> dict[str, Any]:
    """Parse a BOM CSV back to the record's bom section shape.

    The parse-back is the round-trip half of the contract: the columns
    must be BOM_COLUMNS exactly, and a quantity cell parses to a number
    (int when integral) so a re-built section equals the exported one.
    Every other cell stays a string — including empty ones, because the
    section's honesty lives in stating absence, not dropping keys.
    """
    path = Path(path)
    with path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        try:
            header = next(reader)
        except StopIteration:
            raise DesignRecordError(f"{path} is empty; no header row")
        header = [column.strip() for column in header]
        if tuple(header) != BOM_COLUMNS:
            raise DesignRecordError(
                f"{path}: columns {header} != the decided "
                f"{list(BOM_COLUMNS)}"
            )
        lines: list[dict[str, Any]] = []
        for number, cells in enumerate(reader, start=2):
            if not cells:
                continue
            line: dict[str, Any] = {}
            for column, cell in zip(BOM_COLUMNS, cells):
                stripped = cell.strip()
                if column == "quantity" and stripped:
                    number_value = _quantity(stripped)
                    if number_value is None:
                        raise DesignRecordError(
                            f"{path} line {number}: quantity cell "
                            f"'{cell}' is not a number"
                        )
                    line[column] = number_value
                    continue
                line[column] = stripped if column != "quantity" else ""
            lines.append(line)
    return {BOM_LINES_KEY: lines}


def _quantity(text: str) -> float | int | None:
    try:
        value = int(text)
    except ValueError:
        try:
            return float(text)
        except ValueError:
            return None
    return value


def _text(value: Any) -> str:
    if value is None:
        return ""
    return str(value).strip()
