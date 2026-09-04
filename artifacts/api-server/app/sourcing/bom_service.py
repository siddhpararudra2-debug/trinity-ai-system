"""Automated BOM normalization and distributor sourcing lookups."""
from __future__ import annotations

import csv
import io
import re
from datetime import datetime, UTC
from typing import Any


_CATALOG = {
    "ESP32-WROOM-32": {"manufacturer": "Espressif", "distributors": {"digikey": "ESP32-WROOM-32", "lcsc": "C82899", "jlcpcb": "C82899"}},
    "STM32F103C8T6": {"manufacturer": "ST", "distributors": {"digikey": "497-10857-ND", "lcsc": "C8734", "jlcpcb": "C8734"}},
    "0805-10K": {"manufacturer": "Generic", "distributors": {"digikey": "RC0805FR-0710KL", "lcsc": "C17414", "jlcpcb": "C17414"}},
    "0805-100N": {"manufacturer": "Generic", "distributors": {"digikey": "445-1262-1-ND", "lcsc": "C1525", "jlcpcb": "C1525"}},
}


class BomSourcingService:
    async def from_csv(self, csv_text: str, preferences: dict[str, Any] | None = None) -> dict[str, Any]:
        preferences = preferences or {}
        preferred = preferences.get("distributors") or ["digikey", "lcsc", "jlcpcb"]
        reader = csv.DictReader(io.StringIO(csv_text))
        lines: list[dict[str, Any]] = []
        for row in reader:
            part = (row.get("Value") or row.get("Part") or row.get("Reference") or "").strip()
            qty = int(re.sub(r"[^\d]", "", row.get("Qty", "1") or "1") or "1")
            match = self._match_part(part)
            lines.append(
                {
                    "reference": row.get("Reference", ""),
                    "value": part,
                    "quantity": qty,
                    "engineering_identity": part,
                    "match": match,
                    "sourcing": self._quote(match, qty, preferred) if match else None,
                    "status": "matched" if match else "unmatched",
                }
            )
        return self._wrap(lines, preferences)

    async def from_components(self, components: list[dict[str, Any]], preferences: dict[str, Any] | None = None) -> dict[str, Any]:
        preferences = preferences or {}
        preferred = preferences.get("distributors") or ["digikey", "lcsc", "jlcpcb"]
        lines = []
        for component in components:
            value = str(component.get("value") or component.get("part") or "")
            qty = int(component.get("quantity") or 1)
            match = self._match_part(value)
            lines.append(
                {
                    "reference": component.get("reference", ""),
                    "value": value,
                    "quantity": qty,
                    "engineering_identity": value,
                    "match": match,
                    "sourcing": self._quote(match, qty, preferred) if match else None,
                    "status": "matched" if match else "unmatched",
                }
            )
        return self._wrap(lines, preferences)

    def _match_part(self, value: str) -> dict[str, Any] | None:
        upper = value.upper()
        for key, meta in _CATALOG.items():
            if key.upper() in upper or upper in key.upper():
                return {"catalog_key": key, **meta}
        return None

    def _quote(self, match: dict[str, Any], qty: int, preferred: list[str]) -> dict[str, Any]:
        quotes = []
        for distributor in preferred:
            sku = match.get("distributors", {}).get(distributor)
            if not sku:
                continue
            unit_price = round(0.05 + (hash(sku) % 500) / 1000, 3)
            quotes.append(
                {
                    "distributor": distributor,
                    "sku": sku,
                    "unit_price_usd": unit_price,
                    "extended_price_usd": round(unit_price * qty, 3),
                    "stock": (hash(sku) % 9000) + 100,
                    "timestamp": datetime.now(UTC).isoformat(),
                }
            )
        return {"quotes": quotes, "preferred": preferred[0] if preferred else None}

    @staticmethod
    def _wrap(lines: list[dict[str, Any]], preferences: dict[str, Any]) -> dict[str, Any]:
        matched = sum(1 for line in lines if line["status"] == "matched")
        return {
            "engine": "bom_sourcing",
            "lines": lines,
            "summary": {
                "total_lines": len(lines),
                "matched": matched,
                "unmatched": len(lines) - matched,
            },
            "preferences": preferences,
            "disclaimer": "Pricing and stock are illustrative snapshots; verify before procurement.",
        }
