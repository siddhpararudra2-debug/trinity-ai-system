# Trinity AI

## Overview

Trinity is an AI-assisted hardware design system for embedded systems
development. It integrates CAD, PCB, and firmware tools into a unified
workflow with an LLM-independent core: deterministic engines grade
their own outputs, a strict 38-prompt eval set guards against
regressions, and every unverifiable claim is refused with a stated
reason instead of a fake pass.

## Current status (counts as of commit `cf7b7da`, 2026-11-16)

- **Backend**: 10 engines registered — math, cad, pcb, firmware,
  verification, vision, research, simulation, robotics, crosscheck.
  Without the native binary, pcb/firmware run as pure-Python fallbacks
  that honestly label themselves `status: "scaffolded"`.
- **Built** (M20–M33): data layer + schema migration, projects and BOM
  flows, reference library, India parts catalog, JWT accounts,
  billing (plans, GST, signed deduped webhooks), trust pages, per-day
  plans & limits, single/multi-domain router, eval runner with
  `--require-native` enforcement.
- **Open**: M34/M35 (business basics + launch check); production
  hosting follow-ups — TLS certificate provisioning and the reverse
  proxy itself (see `GO_LIVE.md` → Hosting).
- **UI**: `/` landing, `/projects` design records, `/chat`, `/login`,
  `/trust` (plus about/contact/services/systems/team).

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
pytest tests/             # 780 passed, 24 skipped
python tools/eval/run.py  # 25 PASS / 0 FAIL / 13 SKIP of 38
```

- The 24 pytest skips are environmental, not failures: 2 need live LLM
  API keys, 22 need `Trinity.exe` via `TRINITY_NATIVE_EXEC`. Machines
  without the `[vision]`/`[cad]` extras skip more (with reasons).
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

The full route table is pinned by `tests/test_route_table.py`. Key
entry points:

- `POST /api/requirements/execute` — single-domain routing
- `POST /api/workflows/execute` — multi-domain plan → DAG execution
- `POST /api/execute` — direct engine operations
- `/api/projects` (+ `/from-design`, `/bom.csv`, `/bom-priced.csv`,
  `/bom/rebuild`, snapshots/restore, `/inspect`)
- `/api/reference`, `/api/match?tags=`, `/api/parts`, `/api/fabs`
- `/api/uploads`, `/api/health`

## Design record flow

1. Create a design via `POST /api/projects/from-design`
2. List/get records via `/api/projects` (or the `/projects` UI page)
3. Export the BOM via `/api/projects/{id}/bom.csv` (priced variant:
   `/bom-priced.csv`)
4. Rebuild the BOM from a PCB fragment via `/api/projects/{id}/bom/rebuild`
5. Save/restore snapshots via the record history API
