# Trinity AI

## Overview

Trinity is an AI-assisted hardware design system for embedded systems development. It integrates PCB design, firmware, and CAD tools into a unified workflow.

## Stage 12 (M25-M29) - Make Usable, Then Add the Data Layer

### M25 Backend Gaps ✅ Complete
- `GET /api/projects/{id}/bom.csv` — export priced BOM as CSV; 404 if record missing, 409 if no BOM data
- `POST /api/projects/{id}/bom/rebuild` — rebuild BOM from PCB design fragment via `save_record`
- `POST /api/projects/from-design` — build a new design record from a request body

### M26 Projects UI ⬜ In Progress
- `/projects` list page with design record cards
- `/projects/view` tabs:
  - Report: badge, tally, check table (status, reason, location, hint)
  - Record: JSON textarea plus Save
  - History: snapshots, Restore (confirm), Compare
  - BOM: table, CSV download, Rebuild from PCB (confirm)
- E2E suite `projects`: seed `good_esp32_i2c` and `break_c03_unknown_pin`; assert VERIFIED vs FAILED, C03 at `U1.GPIO39`, fix-and-save flips status, restore, API-down error

### M27 Reference Library ⬜ In Progress
- Audit `library.py` against convention 9. Fixed tag vocabulary; rank by tag matches, then id; recompute verified on read
- Grow to ~10 designs (drift test, all-VERIFIED test)
- Routes: `/api/reference`, `/match?tags=`, POST `/api/projects/from-reference` (copy, never alias), `/api/reference/candidates`
- No promote route; CLI `tools/reference/review.py`
- UI: start from reference, submit as candidate

### M28 India Data ✅ Complete
- Fix convention 9: parts get `bom_value`, `bom_package`, `source` (required with a price), `price_note` (required when null)
- Match (value, package) first; value alone only if exactly one part has it; ambiguous means blank
- Build strict `src/engines/parts/catalog.py`, `parts.json` (100-200 generic parts, prices null), `fabs.json` (supplied entries only)
- `export_priced_bom_csv()` (adds price_inr, price_updated_on)
- `tools/parts/import_prices.py`
- routes `/api/parts`, `/api/fabs`, `/api/projects/{id}/bom-priced.csv`
- Parts/Fabs pages

### M29 Close-out ⬜ In Progress
- Eval set to ~38 prompts. Update README, MILESTONES.md, work.md
- Run full e2e

## Do NOT build
Accounts, billing, trust pages, sandboxing, hosting. Fix/Explain modes, router changes. XLSX/PDF. Auto-promotion. Scraping, live prices. Fab booking. Visual editors. Delete-project. Linux PCB/firmware.

## Getting Started

### API Routes
- `/api/projects` — list design records
- `/api/projects/{id}` — get a design record
- `/api/projects/from-design` — create a new design record
- `/api/projects/{id}/bom.csv` — export priced BOM as CSV
- `/api/projects/{id}/bom/rebuild` — rebuild BOM from PCB fragment
- `/api/reference` — list verified library designs
- `/api/match?tags=` — rank designs by tag containment
- `/api/parts` — list parts from India catalog
- `/api/fabs` — list fabrication directories
- `/api/projects/{id}/bom-priced.csv` — priced BOM CSV export

### Conventions
- **Convention 9**: Parts get `bom_value`, `bom_package`, `source` (required with a price), `price_note` (required when null)
- **Price stale after 90 days**: label it, never hide it. Totals say "N of M lines priced".

### Running Tests
```
pytest tests/          # 703 passed, 26 skipped
python tools/eval/run.py  # 24 PASS / 6 SKIP baseline
```

### Design Record Flow
1. Create a design via `/api/projects/from-design`
2. View details via `/api/projects/{id}` or the UI `/projects/view/{id}`
3. Export BOM as CSV via `/api/projects/{id}/bom.csv`
4. Rebuild BOM from PCB via `/api/projects/{id}/bom/rebuild`
5. Save and restore snapshots via the History tab

### Eval Set
- 38 prompts across domains: cad (10), pcb (9), firmware (6), math (7), mixed (6)
- 24 PASS / 6 SKIP baseline, with 8 additional prompts for M29 close-out