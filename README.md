# Trinity AI

## Overview

Trinity is an AI-assisted hardware design system for embedded systems
development. It integrates CAD, PCB, and firmware tools into a unified
workflow with an LLM-independent core: deterministic engines grade
their own outputs, a strict 38-prompt eval set guards against
regressions, and every unverifiable claim is refused with a stated
reason instead of a fake pass.

## Current status (counts as of commit `f00786c`, 2026-11-17)

- **Backend**: 10 engines registered — math, cad, pcb, firmware,
  verification, vision, research, simulation, robotics, crosscheck.
  Without the native binary, pcb, firmware, simulation, robotics,
  vision and research run real Python fallbacks (no status label): pcb
  places, routes and design-checks a board; firmware validates
  pins/peripherals against an MCU database and labels builds
  `COMPILED` only when a real toolchain ran, else `"static check only,
  not compiled"`. The native bridge is still preferred whenever
  `Trinity.exe` exists.
- **Built** (M20–M33): design record + cross-checks + verified report,
  data layer + schema migration, projects/snapshots and BOM flows
  (CSV, priced CSV, XLSX), reference library, India parts catalog, JWT
  accounts, billing (plans, GST, signed deduped webhooks), trust pages,
  per-day plans & limits, single/multi-domain router, eval runner with
  `--require-native` enforcement, and Stage 14's A1–A7 / B1–B3 work
  (XLSX BOM, enclosure lid, eval fixtures, CAD audit, RAG retrieval,
  deterministic router, explain/fix, native audit, workers, sandbox).
- **Open**: M34 (business basics — the feedback route and analytics
  flag exist; error reports, events and the feedback CLI are still
  pending), M35 (launch check), Stage 15's remaining Fix-mode and
  embeddings work, and production hosting follow-ups — TLS certificate
  provisioning and the reverse proxy itself (see `GO_LIVE.md` →
  Hosting).
- **Migration plan**: [docs/MIGRATION_PLAN.md](docs/MIGRATION_PLAN.md) — repo audit plus a reversible, stage-by-stage plan to move the deterministic core to Rust (with Go services, retained C++ GUI/OpenCV, and Python adapters). Plan only; no Rust or Go code has shipped.
- **UI**: `/` workspace (command bar + live engine consoles), `/projects`
  artifacts & jobs (live catalog), `/chat`, `/login`, `/trust` (plus
  about/contact/services/systems/team). Design records are API-only;
  there is no dedicated design-record page in the current frontend.

## Getting started

```
.\start.ps1      # starts API + frontend, waits for real readiness
```

API only:

```
python -m uvicorn src.main:app --host 127.0.0.1 --port 8000
```

Production serving (details in `GO_LIVE.md` → Hosting):

- `TRINITY_ENV=production` with explicit `TRINITY_CORS_ORIGINS` — the
  app refuses wildcard or empty origins in production at startup.
- Worker count via `TRINITY_WEB_CONCURRENCY` (uvicorn `--workers`;
  SQLite runs in WAL mode and records/artifacts are DB-backed, so
  workers share state).
- TLS terminates at the reverse proxy on the same host
  (`--proxy-headers --forwarded-allow-ips 127.0.0.1` is pre-wired).

## Running tests

```
pytest tests/             # 823 passed, 24 skipped (with [cad] + [vision] extras)
python tools/eval/run.py  # 25 PASS / 0 FAIL / 13 SKIP of 38 (with the STEP kernel)
```

- The 24 pytest skips are environmental, not failures: 2 need live LLM
  API keys, 22 need `Trinity.exe` via `TRINITY_NATIVE_EXEC`. Without
  the `[cad]`/`[vision]` extras the same suite is 805 passed /
  42 skipped (16 STEP-kernel tests and 2 pypdf tests join the skips,
  with reasons). The six LLM transport contract tests need the
  declared `openai`/`anthropic` SDKs installed; without them they fail
  with `ModuleNotFoundError` rather than skip.
- Eval: native-dependent prompts SKIP with their reason;
  `--require-native` upgrades every SKIP to a FAIL for Windows-run
  enforcement. Without the STEP kernel (cadquery/OCP) the baseline is
  24 PASS / 0 FAIL / 14 SKIP. The saved baseline
  (`data/eval/results.json`) is deterministic — volatile temp paths
  and artifact hashes are scrubbed at save time.

### Eval set

38 prompts: cad (10), pcb (9), firmware (6), math (7), mixed (6).

## Conventions

- **Convention 9**: parts carry `bom_value`, `bom_package`, `source`
  (required with a price), `price_note` (required when null).
- **Price stale after 90 days**: label it, never hide it. Totals say
  "N of M lines priced".

## API

Key routes are pinned by `tests/test_route_table.py` (reference routes,
no double `/api` prefixes, and no dangling `from-design` stub). Key
entry points:

- `POST /api/requirements/execute` — single-domain routing
- `POST /api/workflows/execute` — multi-domain plan → DAG execution
- `POST /api/execute` — direct engine operations
- `/api/projects` (+ `/from-reference/{design_id}`, `/bom.csv`,
  `/bom-priced.csv`, `/bom.xlsx`, `/bom/rebuild`, snapshots/restore,
  `/inspect`, `/report`, `/explain`, `/fix`)
- `/api/reference`, `/api/reference/candidates/{design_id}`,
  `/api/match?tags=`, `/api/parts`, `/api/fabs`
- `/api/accounts/*`, `/api/billing/*`, `/api/trust/{about,privacy,terms,ownership}`
- `/api/uploads`, `/api/health`, `/api/feedback`

## Design record flow

1. Create a record by copying a reference design
   (`POST /api/projects/from-reference/{design_id}`) or by saving a
   full record (`PUT /api/projects/{id}`)
2. List/get records via `/api/projects` (design records are API-only;
   the `/projects` UI page is the artifacts & jobs view)
3. Export the BOM via `/api/projects/{id}/bom.csv` (priced variant:
   `/bom-priced.csv`, spreadsheet: `/bom.xlsx`)
4. Rebuild the BOM from a PCB fragment via `/api/projects/{id}/bom/rebuild`
5. Save/restore/compare snapshots via `/api/projects/{id}/snapshots*`
6. Inspect, explain or propose a fix via `/inspect`, `/explain`, `/fix`
