#!/usr/bin/env python
"""
tools/parts/import_prices.py

CLI tool to import real INR prices into the parts catalog.

Usage:
    python -m tools.parts.import_prices  --file prices.json
    python -m tools.parts.import_prices  --source "Vendor Name" --prices "part_id:price_inr,part_id2:price2"

Features:
- Update existing parts with new prices and sources
- Mark parts as unpriced (price: null)
- Generate a report of changes
- Validate against convention 9 field requirements
"""

import argparse
import json
import sys
from pathlib import Path

# Add the project root to the path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from src.engines.parts.catalog import load_parts


def main():
    parser = argparse.ArgumentParser(description="Import real INR prices into parts catalog")
    parser.add_argument("--file", help="JSON file with price updates", required=False)
    parser.add_argument("--source", help="Source name for the price updates", required=False)
    parser.add_argument("--prices", help="Comma-separated part_id:price_inr pairs", required=False)
    args = parser.parse_args()

    if not args.file and not args.prices:
        parser.print_help()
        sys.exit(1)

    # Load current catalog
    cat = load_parts()

    updates = []

    if args.file:
        with open(args.file) as f:
            file_data = json.load(f)
        if "prices" in file_data:
            updates = file_data["prices"]
        elif "part_updates" in file_data:
            updates = file_data["part_updates"]
        else:
            print("Error: JSON file must contain 'prices' or 'part_updates' key")
            sys.exit(1)

    if args.prices:
        # Parse "part_id:price_inr,part_id2:price2"
        for pair in args.prices.split(","):
            pair = pair.strip()
            if ":" in pair:
                pid, price_str = pair.split(":", 1)
                pid = pid.strip()
                try:
                    price = float(price_str.strip())
                    updates.append({"id": pid, "price_inr": price, "source": args.source or "imported"})
                except ValueError:
                    print(f"Invalid price for {pid}: {price_str}")
                    sys.exit(1)

    # Apply updates
    print(f"Loading catalog with {len(cat)} parts")
    print(f"Processing {len(updates)} price updates")

    updated = 0
    unpriced = 0
    skipped = 0

    for upd in updates:
        pid = upd.get("id", "")
        price_inr = upd.get("price_inr")
        source = upd.get("source", "")

        if not pid:
            skipped += 1
            continue

        entry = cat.get(pid)
        if entry is None:
            print(f"  SKIP: Part '{pid}' not found in catalog")
            skipped += 1
            continue

        if price_inr is None:
            # Mark as unpriced
            entry["price_inr"] = None
            entry["price_note"] = "Unpriced - India catalog"
            entry["source"] = source or entry.get("source", "")
            unpriced += 1
            print(f"  UNPRICED: '{pid}' - marked as unpriced (source: {source or entry.get('source', 'N/A')})")
        else:
            # Update with new price
            entry["price_inr"] = float(price_inr)
            entry["source"] = source or entry.get("source", "")
            # Set price_note if not already set (convention 9: required when price is not null)
            if not entry.get("price_note"):
                entry["price_note"] = f"Price imported on {upd.get('date', '')}" if upd.get('date') else "Price imported"
            updated += 1
            print(f"  UPDATED: '{pid}' - INR {price_inr} (source: {entry['source']})")

        updated += 1

    # Save updated catalog
    # Re-load from parts.json to get fresh state, then apply updates
    parts_path = Path(__file__).resolve().parent.parent / "src" / "engines" / "parts" / "parts.json"

    # Read current parts.json
    with open(parts_path) as f:
        data = json.load(f)

    # Apply updates to the in-memory data
    for part_data in data["parts"]:
        pid = part_data["id"]
        for upd in updates:
            if upd.get("id") == pid:
                if upd.get("price_inr") is None:
                    part_data["price_inr"] = None
                    part_data["price_note"] = "Unpriced - India catalog"
                    part_data["source"] = upd.get("source", part_data.get("source", ""))
                else:
                    part_data["price_inr"] = upd["price_inr"]
                    part_data["source"] = upd.get("source", part_data.get("source", ""))
                    if not part_data.get("price_note"):
                        part_data["price_note"] = "Price imported via import_prices"

    # Write back
    with open(parts_path, 'w') as f:
        json.dump(data, f, indent=2)

    print(f"\nDone. Updated: {updated}, Unpriced: {unpriced}, Skipped: {skipped}")
    print(f"Catalog saved to: {parts_path}")


if __name__ == "__main__":
    main()