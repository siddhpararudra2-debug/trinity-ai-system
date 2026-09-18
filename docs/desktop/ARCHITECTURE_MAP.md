# Trinity — Existing Repository Architecture Map

Author: Buffy (Codebuff) · 2026-09-18 · Branch `feat/desktop-native-shell`
Purpose: ground truth of what exists today, produced during the pre-migration
inspection required by the desktop migration brief. Nothing here is aspirational.

## 1. Repositories and branches

| Location | Remote | Role |
|---|---|---|
| `trinity-ai-system/` | `siddhpararudra2-debug/trinity-ai-system` | **Canonical repo.** Lean V1: Next.js marketing/site shell + FastAPI V1 core. `main` @ `8e790a7` (green CI). |
| `TRINITY-systems/` | separate `TRINITY-systems` repo (mirrors + `trinity` remote pointing at trinity-ai-system) | Older, heavier prototype copy of the same tree (has `.next`, `node_modules`, local `lib/`). Reference only. |
| `hive/`, `roster.json`, `roster-backups/` | untracked | Local tooling. Not part of the product. |

The heavy prototype (`TRINITY-systems`) and the canonical repo were confirmed to
contain the **same backend module set**; the canonical repo is the source of truth.

## 2. Current process model

`Browser (Next.js SSR) → HTTP/JSON → FastAPI (uvicorn, :8000) → asyncio.to_thread → EngineRegistry → Engine → ValidationResult → JobManager → ArtifactManager → SQLite + files`

This is a **website + API server** architecture. The desktop migration replaces the
outer shell (Next.js + FastAPI-as-UI-backend) while preserving the inner core
(engines, validation model, artifact/SQLite lineage) as the reference semantics.

## 3. Backend inventory (`backend/app/`, Python 3.11+, FastAPI, no ORM)

| Module | What it actually does (verified by reading the code) | Reusable for desktop? |
|---|---|---|
| `core/config.py` | `Settings` dataclass; paths derived from repo root (`backend/../storage`). `ensure_storage_layout()` mkdirs the tree. | Semantics yes (data dir layout); paths must become `%LOCALAPPDATA%/Trinity` + configurable workspace |
| `core/errors.py` | Classified error hierarchy: `request_validation_error`, `engine_not_found`, `engine_execution_error`, `geometry_validation_error`, `job_not_found`, `artifact_not_found`, `capability_unavailable` — each with `code/message/details` | **Yes** — mirror these exact error codes in C++ `Error` |
| `core/logging_config.py` | Structured logging | Replaced by C++ structured logger; Python side keeps its own |
| `core/cache.py` | SQLite cache keyed `sha256(engine:operation:canonical-json)`; only `math.*` and `cad.validate` marked cacheable (CAD generate excluded to preserve provenance) | **Yes** — port policy to C++ cache |
| `db/database.py` | SQLite (WAL, FK on), schema: `jobs`, `artifacts`, `validations`, `cache_entries`, `projects` + 2 indexes. `CREATE TABLE IF NOT EXISTS` only — **no migration versioning yet** | **Yes** — semantics ported; C++ adds real `schema_migrations` |
| `engines/base.py` | `Engine` protocol: `name/version/capabilities/execute/describe`; `EngineResult{success, result, artifacts, pending_artifacts, validation, errors}`; `ArtifactRef`; `ValidationResult{status, checks}` with statuses `GENERATED/VALIDATED/VERIFIED/FAILED` | **Yes** — this is the contract mirrored as `trinity::engines::IEngine` |
| `engines/registry.py` | In-memory dict registry; `bootstrap_engines()` registers math, cad, and **scaffold engines** for `pcb, firmware, vision, research, simulation, robotics` | **Yes** — ported as `EngineRegistry` |
| `engines/scaffold.py` | `ScaffoldEngine` — raises `CapabilityUnavailableError` on execute; truthfully reports `status: scaffolded` | **Yes** — this honesty pattern is required by the brief ("never fabricate functionality") |
| `engines/math/engine.py` | SymPy `solve`/`evaluate`; re-substitutes each root and checks residual < 1e-9 → `VALIDATED`; identity check → `VERIFIED` | Logic reference; desktop V1 uses deterministic C++ evaluator, SymPy stays on the Python side via IPC |
| `engines/cad/ir.py` | `QuadcopterFrameIR` with strict parameter validation (unknown-key rejection, motor_count==4, positive finite, overall_size>center_plate, fc_mount_spacing fits, V1 range limits ≤1000 mm) | **Yes** — the parameter set, units and validation rules port verbatim |
| `engines/cad/builder.py` | Deterministic X-quadcopter mesh: center plate + 4 arms @45/135/225/315° + motor bosses, box primitives, rotation about Z | **Yes** — ported to C++ mesh kernel (same numbers) |
| `engines/cad/primitives.py` | `Mesh{triangles}`, `box()` (consistent winding), `write_binary_stl` (80-byte header + `<I>` count + per-tri normal/verts + `<H>` attr) | **Yes** — ported; STL format compatibility is asserted in tests |
| `engines/cad/validators.py` | Dimensions vs expected diagonal span (15% tol), finite vertices, degenerate-triangle count, arm length > 0, FDM min feature ≥ 1.0 mm | **Yes** — ported check-for-check |
| `engines/cad/glb.py` | Minimal glTF 2.0 GLB writer (magic/version/length header asserted in tests) | Reference; GLB export initially stays on the Python host, C++ port planned |
| `engines/cad/engine.py` | `CADEngine` orchestrating IR→builder→validate→export; `pending_artifacts` handed to JobManager (single-writer rule for `storage/artifacts/`); unavailable formats (STEP) reported honestly as `CAD_KERNEL_UNAVAILABLE` | **Yes** — orchestration ported; adapters below |
| `engines/cad/adapters/base.py` | `CADAdapter` protocol: `name/generate/export_step` | Evolves into `ICADAdapter` (brief) with `generate/import/export/validate/preview` |
| `engines/cad/adapters/{cadquery,freecad,onshape,openscad}.py` | All raise `CapabilityUnavailableError` honestly when kernel missing | **Yes** — same boundary kept |
| `intelligence/provider.py` | `ModelProvider` protocol: `generate/stream/tool_call` | **Yes** — becomes `IModelProvider` (C++) + Python equivalent; mock impl |
| `intelligence/router.py` | **Deterministic** regex requirement parser: `Create a 50 mm quadcopter frame` → `{domain: cad, operation: generate, object: quadcopter_frame, parameters:{overall_size}}`. Raises if no match. No LLM anywhere | **Yes** — becomes the seed of `CommandParser`; CommandPlanner interface added around it |
| `jobs/manager.py` | `run_sync`: insert job → running → engine.execute → persist pending artifacts (copy + sha256) → validation row → response. Cache policy applied. Job rows carry request/result/error JSON | **Yes** — semantics ported into C++ async `JobSystem` (queued/running/paused/…) |
| `artifacts/manager.py` | Only writer of `storage/artifacts/<uuid>/`; sha256 checksum; SQLite metadata | **Yes** — ported as `ArtifactStore` |
| `workflows/dag.py` | `WorkflowNode{id, engine, operation, parameters, dependencies}` + `ordered()` topological sort; rejects duplicates, cycles, missing deps | **Yes** — ported as `Workflow/Dag` |
| `api/routes.py` | REST surface: health, engines, execute, jobs, job get, artifact download, math/solve, cad/generate, requirements/execute | Stays as an *optional* API host; desktop uses IPC first |
| `main.py` | FastAPI lifespan: storage layout, init_db, bootstrap engines | Replaced by desktop app lifecycle |

### Tests that must keep passing
`backend/tests/`: `test_cad.py`, `test_math.py`, `test_health.py`, `test_regression.py`
(GLB header bytes, artifact↔job linkage, DAG duplicate rejection, IR NaN rejection).
These run against the **existing** FastAPI app and define the semantics the C++
core must reproduce. They are not modified during the migration.

## 4. Frontend inventory (`app/`, `components/`, `lib/`)

| Piece | Reality | Disposition |
|---|---|---|
| `app/{page,about,projects,services,team,contact}/page.tsx`, `components/layout/*` | Marketing/website pages with hero sections | Dropped from desktop (explicitly forbidden by brief) |
| `components/sections/Workspace.tsx`, `EngineConsole.tsx` | In-browser workspace + command console concepts (React state, fetch to API) | **Feature reference** for dock layout, console, command flow |
| `components/three/EngineCanvas.tsx` | Dependency-free 2D-canvas wireframe projection of the quadcopter IR (no WebGL!) — mirrors `app.engines.cad` proportions exactly | Interaction/visual reference for the QML viewport; not carried over as code |
| `components/trinity/*` (origin/main) | Full QML-like panel suite (GeometryViewer, JobMonitor patterns, artifact panel, spec panel) | **Design reference** for the desktop panels: viewport controls (wireframe/axes/spin), job list, validation checklist display |
| `lib/api/trinity.ts` | Typed API client (`generateDesign`, `solveEquation`, `TrinityResult` shape) | Contract reference for the IPC schema |

## 5. Docs, CI, packaging

- `docs/`: `ARCHITECTURE.md`, `API.md`, `CAD.md`, `ENGINES.md`, `PHASES.md`, `SECURITY.md`, `ARCHITECTURE_AUDIT.md` — consistent with the code (verified during inspection).
- CI (`.github/workflows/ci.yml`): Backend tests (3.13), `npm ci` + lint + build.
- No packaging, no installer, no native code today. `package.json` has no Electron/Tauri.
- Dependabot config: github-actions only (stale, pre-reset PRs were all closed unmerged).

## 6. Capability truth table (what actually executes today)

| Domain | Executes | Honest scaffold | Notes |
|---|---|---|---|
| CAD (quadcopter_frame) | ✅ mesh + STL/GLB/JSON | — | STEP honestly unavailable (`CAD_KERNEL_UNAVAILABLE`) |
| Math (solve/evaluate) | ✅ SymPy | — | Root re-substitution verification |
| PCB / Firmware / Vision / Research / Simulation / Robotics | — | ✅ `CapabilityUnavailableError` | Pattern preserved in desktop |

## 7. Gaps the desktop architecture must close

1. No native shell, no offline operation independent of browser/uvicorn.
2. No migration-versioned schema (only `IF NOT EXISTS`).
3. No async job system (jobs are synchronous per request).
4. No engine registry discovery beyond in-process imports; no plugin loading.
5. No IPC layer between a native app and Python engines.
6. No settings model, no `%LOCALAPPDATA%` data layout, no workspace configurability.
7. No installer, no bundling, no crash handling, no resource monitoring.
8. No validation-state *lifecycle* beyond per-request checks (VERIFIED state exists in math identity check only).
9. `ModelProvider` protocol exists but nothing implements it (not even a mock).

These gaps define Phase D0–D7 in `MIGRATION_PLAN.md`.
