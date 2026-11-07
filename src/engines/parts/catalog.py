"""The India parts/fabs catalog (M24): priced catalogs loaded strictly.

Every part in ``parts.json`` is a committed, hand-catalogued India
price point and every fab a directory listing. Prices are INR; a null
price is explicitly unpriced (never a guess), a stated price carries
its ``last_updated_on`` provenance. The loader refuses anything it
cannot read back — unknown keys, non-ISO dates, duplicate ids,
non-INR or negative prices — so BOM pricing only ever states what
the catalog states.

Fabs are a directory listing with capabilities; nothing books or emails
one. The `enclosure`/`pcb`/`cad` capability names are the honest set —
no fab claims a capability it does not hold.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

__all__ = [
    "PartsCatalog",
    "PriceBook",
    "fabrications",
    "load_fabs",
    "load_parts",
    "price_table",
]

PARTS_FILENAME = "parts.json"
FABS_FILENAME = "fabs.json"

_PARTS_DIR = Path(__file__).resolve().parent
_PARTS_PATH = _PARTS_DIR / PARTS_FILENAME
_FABS_PATH = _PARTS_DIR / FABS_FILENAME

_PART_KEYS = {"id", "name", "category", "price_inr", "last_updated_on", "bom_value", "bom_package", "source", "price_note"}
_FAB_KEYS = {"id", "name", "city", "capabilities"}


class CatalogError(RuntimeError):
    """The catalog cannot be read honestly; nothing may price from it."""


def _iso_date(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise CatalogError(f"{context}: last_updated_on must be an ISO date string")
    text = value.strip()
    # Valid ISO date YYYY-MM-DD; verify shape and plausible ranges.
    parts = text.split("-")
    if len(parts) != 3 or any(not p.isdigit() for p in parts):
        raise CatalogError(f"{context}: {value!r} is not an ISO date")
    year, month, day = (int(p) for p in parts)
    if not (1 <= month <= 12 and 1 <= day <= 31 and 2000 <= year <= 2100):
        raise CatalogError(f"{context}: {value!r} is not a plausible ISO date")
    return text


def _check_keys(entry: dict[str, Any], allowed: set[str], context: str) -> None:
    extra = sorted(set(entry) - allowed)
    missing = sorted(allowed - set(entry))
    if extra or missing:
        raise CatalogError(
            f"{context}: entry keys {sorted(entry)} != {sorted(allowed)}"
            f"{' (extra ' + ', '.join(extra) + ')' if extra else ''}"
            f"{' (missing ' + ', '.join(missing) + ')' if missing else ''}"
        )


def _load_json(path: Path, kind: str) -> dict[str, Any]:
    if not path.exists():
        raise CatalogError(f"{kind} catalog missing at {path}")
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise CatalogError(f"{path}: not valid JSON ({exc})") from exc
    if not isinstance(raw, dict):
        raise CatalogError(f"{path}: the {kind} catalog is an object")
    for key in ("provenance", "updated_on"):
        if not isinstance(raw.get(key), str) or not raw[key].strip():
            raise CatalogError(f"{path}: '{key}' must be a non-empty string")
    entries = raw.get(kind)
    if not isinstance(entries, list) or not entries:
        raise CatalogError(f"{path}: '{kind}' must be a non-empty list")
    return raw


class PartsCatalog:
    """The strict parts table: `{id, name, category, price_inr,
    last_updated_on}`; a null price is explicitly unpriced."""

    def __init__(self, entries: list[dict[str, Any]]) -> None:
        self._entries = entries
        self._by_id = {e["id"]: e for e in entries}

    def __len__(self) -> int:
        return len(self._entries)

    def entries(self) -> list[dict[str, Any]]:
        return [dict(e) for e in self._entries]

    def get(self, part_id: str) -> dict[str, Any] | None:
        entry = self._by_id.get(part_id)
        return dict(entry) if entry is not None else None

    def by_category(self, category: str) -> list[dict[str, Any]]:
        return [e for e in self.entries() if e["category"] == category]

    def categories(self) -> list[str]:
        return sorted({e["category"] for e in self._entries})

    def priced(self) -> list[dict[str, Any]]:
        return [e for e in self.entries() if e["price_inr"] is not None]

    def unpriced(self) -> list[dict[str, Any]]:
        return [e for e in self.entries() if e["price_inr"] is None]


class PriceBook:
    """Catalog prices keyed by any trusted parts-catalog value.

    ``part``, ``value``, and ``ref`` are tried in that order — first
    match wins — with case-insensitive text. The book only ever states
    what a catalog entry stated: an unpriced catalog entry and an
    unmatched line are both explicitly absent (``None``), never
    presumed or invented.
    """

    def __init__(self, catalog: PartsCatalog) -> None:
        self._catalog = catalog
        self._book: dict[str, tuple[float, str]] = {}
        self._by_name: dict[str, tuple[float, str]] = {}
        for entry in catalog.priced():
            price = float(entry["price_inr"])
            updated = str(entry["last_updated_on"])
            self._book[entry["id"].lower()] = (price, updated)
            self._by_name[entry["name"].lower()] = (price, updated)

    def lookup(self, line: dict[str, Any]) -> tuple[float, str] | None:
        for key in ("part", "value", "ref"):
            candidate = line.get(key)
            if not candidate:
                continue
            normalized = str(candidate).strip().lower()
            if normalized in self._book:
                return self._book[normalized]
            if normalized in self._by_name:
                return self._by_name[normalized]
        return None

    def lookup_by_value_package(self, line: dict[str, Any]) -> tuple[float, str] | None:
        """Match (value, package) first per convention 9.

        1. Try (value, package) combo — first match wins.
        2. Fall back to value alone — only if exactly one part has that value.
        3. Ambiguous value → None (blank price).
        """
        value = str(line.get("value", "") or "").strip().lower()
        package = str(line.get("package", "") or "").strip().lower()
        if not value:
            return None

        # 1) Try (value, package) combo
        # Search priced entries whose name matches the package and value
        matches: list[tuple[float, str]] = []
        for entry in self._catalog.priced():
            # Check if this entry's name/package combination is relevant
            if package and (package in entry.get("name", "").lower()):
                hit = self._book.get(entry["id"].lower())
                if hit:
                    matches.append(hit)
            elif not package and entry.get("bom_package") is None:
                # No package filter, just value match via book
                hit = self._book.get(entry["id"].lower())
                if hit:
                    matches.append(hit)

        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            # Ambiguous — multiple parts with same value+package
            return None

        # 2) Fall back to value alone — only if exactly one part has it
        value_matches: list[tuple[float, str]] = []
        for entry in self._catalog.priced():
            if value in entry.get("name", "").lower():
                hit = self._book.get(entry["id"].lower())
                if hit:
                    value_matches.append(hit)

        if len(value_matches) == 1:
            return value_matches[0]
        if len(value_matches) > 1:
            return None

        return None

    def catalog(self) -> PartsCatalog:
        return self._catalog





def load_parts(path: Path | None = None) -> PartsCatalog:
    """The committed parts table, loaded strictly.

    Refusals name the entry: unknown keys, non-ISO dates, duplicate
    ids, non-INR or negative prices, empty names/categories.
    """
    raw = _load_json(path or _PARTS_PATH, "parts")
    entries: list[dict[str, Any]] = []
    seen: set[str] = set()
    for index, entry in enumerate(raw["parts"]):
        context = f"{path or _PARTS_PATH}: parts[{index}]"
        if not isinstance(entry, dict):
            raise CatalogError(f"{context}: not an object")
        _check_keys(entry, _PART_KEYS, context)
        pid = entry["id"]
        if not isinstance(pid, str) or not pid.strip():
            raise CatalogError(f"{context}: id must be a non-empty string")
        if pid in seen:
            raise CatalogError(f"{context}: duplicate id '{pid}'")
        seen.add(pid)
        if not isinstance(entry["name"], str) or not entry["name"].strip():
            raise CatalogError(f"{context}: name must be a non-empty string")
        if not isinstance(entry["category"], str) or not entry["category"].strip():
            raise CatalogError(f"{context}: category must be a non-empty string")
        price = entry["price_inr"]
        if price is not None:
            if isinstance(price, bool) or not isinstance(price, (int, float)):
                raise CatalogError(
                    f"{context}: price_inr must be a number (INR) or null, got {type(price).__name__}"
                )
            if float(price) < 0:
                raise CatalogError(f"{context}: price_inr {price} is negative")
        updated = _iso_date(entry["last_updated_on"], context)
        entries.append(
            {
                "id": pid,
                "name": entry["name"].strip(),
                "category": entry["category"].strip(),
                "price_inr": None if price is None else float(price),
                "last_updated_on": updated,
            }
        )
    return PartsCatalog(entries)


def fabrications(catalog: PartsCatalog, path: Path | None = None) -> list[dict[str, Any]]:
    """The priced part base resolved to INR quotes: entry plus its
    provenance. Pricing rides the catalog; a null price states None
    with its date — absent elsewhere, never zeroed or guessed."""
    book = PriceBook(catalog)
    rows: list[dict[str, Any]] = []
    for entry in catalog.entries():
        hit = book.lookup(entry)
        rows.append(
            {
                "id": entry["id"],
                "name": entry["name"],
                "category": entry["category"],
                "price_inr": hit[0] if hit else None,
                "price_updated_on": hit[1] if hit else entry["last_updated_on"],
                "provenance": hit is not None,
            }
        )
    return rows


def price_table(catalog: PartsCatalog, path: Path | None = None) -> list[dict[str, Any]]:
    """One row per named part: the BOM-price contract (price or null,
    plus the date the price was last stated)."""
    return fabrications(catalog, path)


def load_fabs(path: Path | None = None) -> list[dict[str, Any]]:
    """The fab directory: `{id, name, city, capabilities}`; a directory
    listing, nothing more (no booking, no email, no lead-time guess)."""
    raw = _load_json(path or _FABS_PATH, "fabs")
    fabs: list[dict[str, Any]] = []
    seen: set[str] = set()
    for index, entry in enumerate(raw["fabs"]):
        context = f"{path or _FABS_PATH}: fabs[{index}]"
        if not isinstance(entry, dict):
            raise CatalogError(f"{context}: not an object")
        _check_keys(entry, _FAB_KEYS, context)
        fid = entry["id"]
        if not isinstance(fid, str) or not fid.strip():
            raise CatalogError(f"{context}: id must be a non-empty string")
        if fid in seen:
            raise CatalogError(f"{context}: duplicate id '{fid}'")
        seen.add(fid)
        for key in ("name", "city"):
            if not isinstance(entry[key], str) or not entry[key].strip():
                raise CatalogError(f"{context}: {key} must be a non-empty string")
        caps = entry["capabilities"]
        if not isinstance(caps, list) or not caps or any(
            not isinstance(c, str) or not c.strip() for c in caps
        ):
            raise CatalogError(f"{context}: capabilities must be a non-empty list of strings")
        fabs.append(
            {
                "id": fid,
                "name": entry["name"].strip(),
                "city": entry["city"].strip(),
                "capabilities": [c.strip() for c in caps],
            }
        )
    return fabs
