# Trinity AI — Language Migration & Production-Readiness Plan

Status: **plan only — no Rust or Go code has been written yet.** Nothing in
this document is shipped, advertised, or counted as a capability. It is a
map of seams and decision rules, not a promise of dates.

This plan follows the house rules in [PROCESS.md](PROCESS.md): one concern
per commit, worklog ticked in the same commit, verify before advertise, and
a wrong premise gets corrected rather than implemented.

---

## 1. How to use this plan (the "free plan" contract)

Code will move while this plan is being executed. A plan that names exact
files and line numbers goes stale after the first refactor. So this document
is deliberately written at the level of **layers, seams, and decision
rules**. Rules for keeping it useful:

1. **The allocation table (§3) is the policy; the stage lists (§5) are
   examples.** Reorder, split, or merge stage work whenever reality changes.
   A stage is done when its *done-when* gate passes, not when its bullet list
   is exhausted.
2. **Never move code without its seam.** Every migrated unit must keep the
   same wire contract (§4) and pass the parity harness (§4.4). If a seam
   changed, update the contract first, in its own commit, then migrate.
3. **Every migration step is reversible in one env-var flip** (§4.2). If a
   step cannot be rolled back instantly, it is too big — split it.
4. **Update this file in the same commit as any decision that changes it.**
   Keep the settled/open decision log (§8) honest; record the evidence, not
   the optimism.
5. **Counts in §2 are a snapshot at `f00786c` (2026-11-17).** They are audit
   evidence, not targets. Re-measure before claiming progress.

---

## 2. Audit — what exists today

Measured at commit `f00786c` with vendored dependencies and generated files
excluded.

| Layer | Location | First-party size | Who owns it today |
|---|---|---|---|
| Python API + core | `src/` (104 `.py`) | 18,452 lines | FastAPI, 60 routes, 10 registered engines, jobs, artifacts, SQLite, accounts/billing/limits/trust |
| Python tests | `tests/` (54 `.py`) | 10,154 lines | 823 pass / 24 skip with extras; route table pinned by `tests/test_route_table.py` |
| Python tooling | `tools/`, `cloud/`, `packaging/` | 2,175 lines | eval runner (38 prompts), parts/reference import CLIs, PyInstaller specs |
| C++ native | `app/native/` (249 files) | 45,189 lines | 8 real engines, deterministic requirement parser, planner, Qt GUI, OpenCV vision, WinHTTP, line-JSON daemon, parallel SQLite layer |
| C++ tests | inside the 45,189 | 10,061 lines / 66 files | doctest, 404 cases / 6,331 assertions; Windows CI only |
| Frontend | `frontend/` (43 `.ts`/`.tsx`) | 7,466 lines | Next.js 16 static export, 10 pages, 14 browser E2E suites |
| Rust / Go | — | **0 lines** | none exists anywhere in the repo |

### 2.1 The two engine trees have drifted (the central problem)

`docs/ARCHITECTURE.md:6` says new engines land in C++ and "Python owns the
API, jobs, artifacts, and the bridge". In practice both trees implement the
same concepts and neither is a faithful mirror:

- `app/native/src/engines/MathEngine.cpp` (2,475 lines) and `CadEngine.cpp`
  are **not bridged at all** — the API always uses Python SymPy math (146
  lines, 2 ops) and the Python CAD tree (2,589 lines, 7 ops).
- Native simulation (10 ops), robotics (11), research (7) and vision (8)
  expose many more operations than `src/engines/native_bridge.py` maps (2,
  2, 1 and 7) — the extras are unreachable from the API.
- Two independent requirement parsers exist: `RequirementParser.cpp`
  (2,556 lines) and `src/intelligence/router.py` (576 lines). Two planners,
  two job managers, two workflow executors, two artifact managers, two
  SQLite layers (the native one is quarantined in a temp storage root by
  the bridge so the two never touch the same file).
- The bridge advertises 6 engines; the Python registry advertises 10.
  `verification` and `crosscheck` (3,934 lines, deterministic C00–C11
  checks) are Python-only and have no native counterpart.

This duplication is why a language migration is worth doing: the clean-up
is the migration. Every engine must end with **one owner**.

### 2.2 Production-readiness gaps already on record

- **Toolchain**: `cargo`, `rustc`, `go` and `cmake` are not installed on the
  development machine used for this audit (Python 3.13 and Node 24 are).
  CI must be the first builder for every new language, the same way
  `native-windows.yml` already builds C++ on a runner.
- **Windows-only native**: all four CMake presets are Windows/x64 with a
  hard-coded Qt prefix; `HttpTransport.cpp` is WinHTTP-only; firmware
  `Builder.cpp` refuses controlled process execution off Windows. CI runs
  the backend on `ubuntu-latest`, which is why 22 tests skip without
  `Trinity.exe`.
- **Config drift**: `deploy/.env` (untracked, gitignored) names settings
  such as `TRINITY_DATABASE_URL`, `TRINITY_AUTH_SECRET`, `TRINITY_API_KEY`,
  Redis and S3 backends that `src/core/config.py` never reads. The real
  contract is `src/core/config.py` + `.env.dummy`. Confirm which is the
  deployment target before migrating any config surface (§8, D-04).
- **Hosting**: `GO_LIVE.md` → Hosting records TLS/proxy work as open;
  packages are Windows portable zip + installer with self-signed signing.
- **No baseline for "fast"**: there is no recorded engine latency or
  throughput benchmark. Performance claims cannot be verified yet, so
  performance is *not* the entry criterion for any stage. Correctness and
  single ownership are.

---

## 3. Target allocation — which language owns what

### 3.1 Decision rule (apply to any new or moved unit)

| If the unit is… | Owner | Why |
|---|---|---|
| Deterministic computation on records: checks, solvers, geometry, IR, validators, exporters, topo sort, BOM folding | **Rust** | CPU-bound, pure, testable by golden fixtures; one implementation, no runtime surprises |
| A wire/model contract shared across processes | **Rust** (generated schema for other languages) | One source of truth kills the drift in §2.1 |
| Headless engine IPC / bridge daemon | **Rust** | Removes Windows DLL + Qt bootstrap from every engine call; keeps the existing JSONL protocol |
| HTTP API, auth verification, DB access on the request path | **Rust** (axum + SQLx/rusqlite) | Same envelope and status codes, no GIL, strong typing at the boundary |
| I/O-bound orchestration: webhook ingress, dedup, retry, external-tool supervision, queues, ops/release CLI | **Go** | Goroutines and a small static binary fit supervisors and sidecars; slower to get burned than hand-rolled async |
| LLM SDK adapters, SSE client plumbing, one-off data/import/eval tooling | **Python** (temporary for adapters, permanent for tooling) | Official SDKs and the existing mock/fake infrastructure live here |
| Qt desktop window, OpenGL viewport, OpenCV image algorithms | **C++** (retained, not expanded) | Already built, tested and packaged; rewriting GUI/OpenCV is a separate decision (§8, D-05) |

This puts the **majority of first-party deterministic logic — the engine
tree, cross-checks, parsers/validators, bridge and API — in Rust**, keeps
operational glue in Go, retains C++ only for GUI/platform bindings, and
leaves Python as a shrinking compatibility shim plus its tooling/SDK
strengths.

### 3.2 Target shape

```
Next.js static UI ──HTTP──> trinity-api (Rust / axum)
                                  │  auth, limits, jobs, artifacts
                                  ├──> trinity-core (Rust crates, in-process)
                                  │      engines · crosscheck · IR · DAG · exporters
                                  ├──> trinity-store (SQLite, WAL, one writer)
                                  ├──> trinity-bridge (Rust, JSONL) ──> Go worker / C++ tools
                                  └──> Python compat shim (until cutover) ──> LLM SDKs
Go services: webhook ingress · external tool supervisor · ops/release CLI
C++ retained: Qt desktop shell + OpenGL viewer · OpenCV algorithms
```

---

## 4. The seams we migrate through

These are the contracts that must hold regardless of how §5 is reordered.

### 4.1 Freeze the existing contracts before moving code

- **HTTP envelope** — `{success, engine, operation, result, artifacts,
  validation, errors, job_id}`; tracked failures are HTTP 200
  `success:false`, unknown ids are real 404s. `tests/test_route_table.py`
  pins the 60 routes. Freeze both in a machine-readable schema.
- **Engine wire** — `Trinity.exe --exec` / `--daemon` already speaks one
  JSON request line in, one envelope line out:
  `{"request_id","engine","operation","parameters"}` →
  `{"success","engine","operation","job_id":"","request_id","result",
   "errors","metadata","artifacts","pending_artifacts","validation"}`.
  Logs stay on stderr; blank lines are skipped; `{"ping":true}` is the
  boot handshake. **Do not redesign this protocol** — replacing it and the
  engine at the same time removes the ability to A/B them.
- **Design record** — sections are verbatim IR-shaped JSON fragments whose
  field names were verified against the native serializers. Keep the shape;
  never "clean it up" during a port.
- **Statuses** — `GENERATED | VALIDATED | VERIFIED | FAILED` (Python maps
  native `INVALID` → `FAILED`); `WARN` never grants `VERIFIED`.
- **Data schema marker** — `DATA_SCHEMA.json` at `TRINITY_STORAGE_ROOT`
  with an explicit migration chain. Whatever process opens the DB must go
  through it first.
- **Config** — the env names in `src/core/config.py` are the contract;
  `deploy/.env` drift is a bug to resolve, not a second contract (§8 D-04).

### 4.2 Dispatcher + kill switch (mandatory for every cutover)

Add one routing layer with explicit modes, defaulting to today's behaviour:

```
TRINITY_ENGINE_BACKEND = python | native | rust | shadow   (default: python)
TRINITY_API_BACKEND    = fastapi | axum                    (default: fastapi)
```

- `shadow`: run both implementations, compare envelopes after scrubbing
  volatile fields (temp paths, timestamps, artifact hashes), log the diff,
  and answer with the incumbent. This is how confidence is earned.
- Any cutover is one env change; rollback is the reverse change. No data
  migration may be coupled to the flip.
- The C++ bridge already proves this pattern: `TRINITY_NATIVE_EXEC` selects
  native vs Python fallback, `TRINITY_NATIVE_DAEMON=0` selects single-shot
  `--exec`, and unmapped ops are never advertised (501 instead of a lie).
  Reuse that discipline rather than inventing a new one.

### 4.3 One writer per database

Never let two runtimes write the same SQLite file. The current bridge
already isolates the native child with a private temp storage root and
copies artifacts out. Keep that rule for the Rust bridge and Go services:
the API owns the production DB; other processes receive inputs and return
envelopes, or use their own storage root.

### 4.4 Parity harness — the gate that makes the plan executable

Build `tools/parity/` (or a Rust `trinity-parity` crate) that:

1. Replays recorded `(request → envelope)` pairs from `data/eval/results.json`,
   `src/engines/crosscheck/fixtures.py`, and the native doctest vectors.
2. Compares the incumbent and challenger after deterministic scrubbing.
3. Fails CI on **any** diff, and fails if **zero cases executed**. This is
   not optional: the repo already learned the skip-goes-green lesson in the
   `cad-kernel` job, which now greps for executed tests.
4. Preserves the 38-prompt eval verdicts byte-for-byte and keeps
   `--require-native` semantics: a port that skips is not a port that passed.

No engine is cut over until its parity gate is green. No exception, no
"close enough" on a numeric tolerance — if a tolerance is needed, make it
an explicit, recorded field of the check.

---

## 5. Stages (reorderable; each has its own done-when)

Each stage is independent work that can land in any order after Stage 0,
except that a unit's parity gate must precede its cutover. Each stage is
one or more commits following the one-concern-per-commit rule.

### Stage 0 — Foundations (no product behaviour change)

- Bootstrap toolchains in CI first: Rust (stable + clippy + fmt), Go, and a
  CMake leg where native builds are needed. Add local setup instructions.
- Create the Rust workspace skeleton: `trinity-contracts`, `trinity-core`,
  `trinity-bridge`, `trinity-api`, `trinity-parity`; generate the JSON
  schema for the frozen contracts (§4.1).
- Land the parity harness and record the baseline: 823/24 pytest, 404
  native doctests, 38 eval prompts, and measured engine latencies.
- Resolve the config drift (D-04) in its own commit.

**Done when:** CI builds and tests every new workspace crate and Go module;
the parity harness executes a nonzero recorded set and fails on an
injected diff; route-table and eval baselines are unchanged.
**Rollback:** delete the skeleton; nothing is on a request path yet.

### Stage 1 — Rust deterministic core (the bulk of the migration)

Port in dependency order, smallest pure unit first; each unit gets parity
fixtures before it gets a dispatcher entry:

1. `src/engines/crosscheck/` (C00–C11 + report status fold) — pure, the
   strongest correctness signal and no native counterpart to conflict with.
2. Design record, snapshots, BOM (`src/engines/crosscheck/{record,
   snapshots,bom,library}.py`) and `src/exports/`.
3. `src/engines/verification/`, parts catalog, deterministic router/DAG
   (`src/router.py`, `src/workflows/`, `src/intelligence/router.py`).
4. Simulation → robotics → research → pcb → firmware → math → cad, using
   the native algorithms in `app/native/src/` as the behavioural reference
   where they exist and the Python fallbacks where they don't.
5. Expose the Rust core to Python via pyo3 **only during transition**, so
   existing tests and tools can call it without changing the API.

**Done when:** for every ported unit, parity is green, eval verdicts are
unchanged, and the dispatcher can serve that unit from Rust with an
instant flip back. **Rollback:** one env var per unit.

### Stage 2 — Rust bridge replaces the headless native path

- Implement `trinity-bridge` against the exact JSONL protocol in §4.1
  (`--exec`, `--daemon`, ping handshake, stderr logging, artifact copy-out).
- Run it beside `Trinity.exe --daemon` in `shadow` mode, then cut over the
  headless path. The Qt window and OpenCV algorithms stay behind the
  existing C++ boundary for now.
- Turn the 22 native-dependent skips into real Linux CI coverage where the
  operation does not need a GUI or a Windows toolchain.

**Done when:** parity green for every advertised op; `--require-native`
equivalent passes on Linux CI for the bridged ops; no capability in
`NATIVE_ENGINES`/`OP_MAP` is lost. **Rollback:**
`TRINITY_ENGINE_BACKEND=native|python`.

### Stage 3 — Go operational services

- Webhook ingress: signature verification, event-id dedup, out-of-order
  handling, durable retry queue — currently in `src/api/billing.py`.
- External tool supervisor: PlatformIO/KiCad process execution with
  timeouts, cancellation and honest `COMPILED` vs "static check only"
  labelling (currently `src/engines/firmware/__init__.py` and native
  `Builder.cpp`).
- Ops CLI: checksum/sign/compress/hash/publish orchestration now spread
  across `packaging/*.ps1` and `scripts/`. Keep PowerShell wrappers until
  the Go CLI is verified on a real release.

**Done when:** the existing duplicate/out-of-order/bad-signature webhook
tests and firmware two-tier build tests pass unchanged against the Go
services; a release bundle is produced and smoke-tested. **Rollback:** keep
the Python route registered and flip the dispatcher.

### Stage 4 — Rust API

- `trinity-api` (axum) serves the 60 frozen routes with identical paths,
  status codes and envelopes, backed by `trinity-core` and the shared DB.
- Move accounts/scrypt/JWT verification, billing state machine, limits and
  trust copy last — they are stateful and have the richest tests.
- Shadow the FastAPI app, compare responses, then cut over per route group.

**Done when:** the full suite (plus translated contract tests) is green
against axum; the frozen-bundle smoke passes; no route changes shape.
**Rollback:** `TRINITY_API_BACKEND=fastapi`.

### Stage 5 — Retire duplicates (only after parity)

- Delete the losing implementation of each engine — never leave two live
  copies of the same op. Remove the pyo3 shim once Rust is self-hosted.
- C++ shrinks to the Qt shell, OpenGL viewer and OpenCV algorithms; the
  native engine tree, native SQLite layer and WinHTTP transport can go once
  nothing depends on them.
- Rewrite `docs/ARCHITECTURE.md` to state the new single owner per layer.

**Done when:** every advertised capability has exactly one implementation;
docs and catalog match reality. **Rollback:** restore from the tagged
pre-retirement commit — this is the one stage without an env-var undo, so
it is deliberately last.

### Stage 6 — Production hardening (runs in parallel from Stage 0)

Cross-cutting, not a final waterfall:

- Observability: structured logs with job/engine/request ids, metrics for
  queue depth, latency, failure rates, and a dashboard/alert per SLO.
- Secrets and config: one env contract (§4.1), no secrets in the repo,
  production CORS refusal preserved.
- Data: backup + restore drill and point-in-time recovery for the SQLite
  file; exercise the `DATA_SCHEMA.json` migration chain on a copy of real
  data before every schema change.
- Delivery: TLS/proxy items in `GO_LIVE.md` → Hosting closed, bundle smoke
  and installed-build smoke green, signed release verified as published.
- Load and SLO gates: record throughput/latency under realistic engine
  mixes; add the numbers to this plan before making any performance claim.

**Done when:** a restore drill succeeds in CI, SLO dashboards exist, and
every GO_LIVE hosting item is either closed or explicitly re-recorded as
open with a reason.

---

## 6. Overall definition of done

The migration is complete when **all** of these are true:

1. Every advertised engine operation has exactly one implementation, in
   Rust, with a parity record proving behaviour was preserved.
2. The 60-route HTTP contract and the engine JSONL contract are unchanged
   unless a contract commit says otherwise.
3. Python is reduced to LLM SDK adapters and tooling; Go owns the
   I/O-bound services; C++ owns only the retained GUI/OpenCV surface.
4. CI builds, tests and verifies all four languages on real runners, with
   nonzero executed-test gates — skips never count as passes.
5. Production checks in §5 Stage 6 pass and `GO_LIVE.md` is updated.

---

## 7. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Two implementations drift again | Dispatcher + parity harness are mandatory before any second implementation exists (§4.2, §4.4) |
| Wire/contract changes break clients mid-migration | Freeze schemas in Stage 0; contract changes land alone, in their own commit |
| Capability regression (the repo's worst historical bug class) | Tests pin `NATIVE_ENGINES` ↔ `OP_MAP` both directions; mirror that for Rust; never advertise an unmapped op |
| Dual runtime writes corrupt SQLite | One writer per DB; isolated storage roots for helpers (§4.3) |
| pyo3/GIL boundary quietly eats the speedup | Long Rust calls must release the GIL; benchmark through the real Python call path, not a bare Rust binary |
| Async runtime lock-in (reqwest/axum imply tokio) | Decide once in Stage 0; keep CPU-bound crates runtime-agnostic and use blocking clients where possible |
| Rust crate ≠ Python package (the 1-to-1 mapping trap) | A Python package often maps to a stack of crates; validation rules currently in pydantic validators need explicit equivalents, not just serde |
| Qt/OpenCV drag makes the headless bridge heavy | Rust bridge replaces the headless path before GUI decisions; GUI stays C++ behind the same protocol |
| Windows-only toolchain / no local cargo, go, cmake | CI is the first builder; local setup doc in Stage 0; do not claim a build that was not executed |
| A parity gate silently skips everything | Fail CI on zero executed cases and on any diff; assert executed tests like the `cad-kernel` job |
| `deploy/.env` config drift misdirects the cutover | Resolve D-04 before touching config surfaces; single source of truth is `src/core/config.py` |

---

## 8. Decision log

### Settled by this plan

- **Incremental strangler migration, not a big-bang rewrite.** No "flag
  day"; every step reversible behind a dispatcher and kill switch.
- **Contracts first, code second.** Envelopes, statuses, record shape and
  env names are frozen before any port.
- **Correctness before performance.** There is no benchmark baseline yet;
  parity is the entry criterion. Performance claims wait for measured data.
- **No capability invention.** A migrated unit that cannot do an op does
  not advertise it; `capability_unavailable` stays truthful.

### Open — need evidence before deciding

- **D-01 — LLM SDK strategy in Rust.** `[NEEDS: decision on keeping a thin
  Python adapter service vs a Rust HTTP client for OpenAI/Anthropic]`.
  Keep the Python adapters until parity of the SSE wire format is proven.
- **D-02 — Auth primitive.** `[NEEDS: keep scrypt vs move to argon2; which
  crate; how existing hashes are re-verified or upgraded]`.
- **D-03 — DB ownership at cutover.** `[NEEDS: single Rust writer vs
  keeping a Python write path during shadowing; how to test the swap on a
  copy of production data]`.
- **D-04 — Deployment config target.** `[NEEDS: owner to confirm whether
  deploy/.env's Redis/S3/aiosqlite topology is a planned future platform or
  stale config; reconcile with src/core/config.py and .env.dummy]`.
- **D-05 — Fate of the Qt desktop shell.** `[NEEDS: keep as a thin native
  client vs retire in favour of the packaged web UI; OpenCV algorithm
  rewrite is a separate cost estimate]`.
- **D-06 — Hosting target and SLOs.** `[NEEDS: production endpoint,
  certificate owner, expected concurrency and latency targets]`.
- **D-07 — Go service boundary.** `[NEEDS: confirm which operational
  services justify a separate binary vs a Rust crate; avoid two runtimes
  for work that is neither I/O-heavy nor supervisor-shaped]`.

---

## 9. Tracking and verification convention

Landing any part of this plan follows the repo's existing chain:

1. One concern per commit; tick `work.md` and log `opencodeworks.md` in
   the **same** commit.
2. Native doctests → Python suite → eval runner → bundle smoke → installed
   smoke, each asserting that tests **executed**.
3. Update `docs/ARCHITECTURE.md`, `docs/ROADMAP.md` and this file when the
   truth changes. A completed stage gets its result and evidence recorded
   here, not just a ticked box.
4. A stage that cannot demonstrate its done-when gate stays open, with the
   reason written down. That is an honest refusal, not a failure.
