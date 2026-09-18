# Trinity Desktop — Migration Plan

Author: Buffy (Codebuff) · 2026-09-18 · Branch `feat/desktop-native-shell`
Companion to `ARCHITECTURE_MAP.md` (what exists) — this is what we build and in
what order. Governing rules from the brief:

- Desktop-first engineering application, **not** a packaged website.
- No proprietary LLM/CAD-AI/PCB-AI. Model layer = replaceable interfaces + mocks.
- Never fabricate functionality; never mark unfinished work production-ready.
- Existing FastAPI app and its tests must keep passing (no blind deletion).

## 0. Architecture decision record (short form)

| Decision | Choice | Rationale |
|---|---|---|
| Native shell | C++20 core + Qt 6 / QML UI | Brief's primary direction; QML fits the panel/dock engineering IDE layout. Qt is the **only** heavy external dependency, so it is an optional CMake feature flag. |
| Core library | `trinity_core` — dependency-free C++ static lib (STL + vendored SQLite amalgamation) | Everything except the shell must build and test with **zero** external deps: reproducible CI, fast builds, no license/network traps. |
| Database | SQLite (vendored amalgamation), migration-versioned (`schema_migrations`, PRAGMA user_version) | Brief mandates SQLite + migrations. WAL + foreign keys mirror the Python semantics. |
| IPC | Length-prefixed newline JSON over the child's stdin/stdout (`IpcChannel` + `IpcServer` in-process) — upgrade path documented for named pipes/gRPC | Cleanest maintainable Windows option that is testable in-process, no deps, works identically for the Python engine host. HTTP-localhost explicitly avoided for internal traffic. |
| Python engines | `python/services/engine_host.py` — a line-JSON RPC host that imports the **existing** `app.engines` registry unchanged | Reuses mature SymPy/CAD/GLB logic; the FastAPI app and tests remain untouched. |
| 3D viewport | QML Scene3D first (orbit/pan/zoom/grid/axes/wireframe); existing web viewer stays as reference only | Native-first, no embedded browser. WebGPU/Three.js retained only if later proven necessary. |
| Installer | Inno Setup script scaffolding + packaging script | Standard Windows `Trinity-Setup.exe` path; marked scaffolding until a signed release build exists. |
| Rust | Not introduced in this phase | Brief: selective only; nothing here needs it yet. |
| CUDA | Not introduced | No computation requirement yet. |

## 1. Target tree (created by this migration)

```
/app/native/
  CMakeLists.txt
  CMakePresets.json
  shell/            # console/Qt entry points, application lifecycle
  core/             # Result, Error, Event, Logging, Paths, FileSystem, Time, Json
  db/               # Database (RAII sqlite), Migrations, repos: projects/jobs/artifacts/validations/workflows/settings
  projects/         # Project model (CAD/PCB/Math/Sim/Firmware/Research/Artifacts/Logs)
  jobs/             # JobSystem: async queue, states, logs, progress
  artifacts/        # ArtifactStore: sha-256 content addressing, metadata, validation states
  workflows/        # DAG definition, topological execution over the job system
  validation/       # ValidationEngine: GENERATED -> VALIDATED -> VERIFIED lifecycle
  commands/         # CommandParser / CommandPlanner / ToolExecutor + registry
  engines/          # IEngine, EngineRegistry, cad/, math/, scaffold engines per domain
  model/            # IModelProvider, ICADGenerator, IPCBGenerator, IEngineeringReasoner + Mock* + Null*
  ipc/              # framed JSON protocol, in-process server, child-process channel
  process/          # ProcessRunner (async, sanitized argv, output streaming)
  monitoring/       # hardware/resource monitor (process + system)
  settings/         # typed settings model + categories + persistence
  third_party/sqlite/  # vendored amalgamation
  tests/            # doctest unit tests for every core module
  python/services/  # engine_host.py (IPC RPC into app.engines), requirements.txt
  qml/              # Qt Quick shell (guarded by TRINITY_WITH_QT)
/installer/trinity.iss + packaging scripts + docs/desktop/
```

The existing `frontend/` (Next.js) stays untouched at repo root; it remains the
web surface. Nothing is deleted in this phase (migration rule 1–7).

## 2. Data & verification model (invariants carried over from V1)

- Statuses: job `QUEUED→RUNNING→PAUSED→COMPLETED|FAILED|CANCELLED` + `VALIDATING`, `VERIFIED` for artifacts; validation rows `GENERATED|VALIDATED|VERIFIED|FAILED`.
- An artifact is never `VERIFIED` because it exists. Verification requires engine-specific checks (CAD: finite verts, non-degenerate tris, dimension tolerance, arm clearance, min FDM feature; Math: root re-substitution residual < 1e-9, identity holds).
- Errors keep the V1 machine codes (`request_validation_error`, `engine_not_found`, `capability_unavailable`, …) so UI and future model layer can classify failures identically.
- Cache policy: only explicitly deterministic operations (`math.*`, `cad.validate`) are cacheable; CAD generation is never served from cache (artifact provenance).
- Artifact store is the **single writer** to the artifacts tree; sha-256 content addressing; temp dirs are cleaned after ingest.

## 3. Phases

### D0 — Architecture map + plan (this document set) ✅
Inspect everything, map it, get the plan reviewed. **No code before this.**

### D1 — Core foundation (C++, dependency-free)
`Result<T>`, `Error` (V1 codes), structured `Logger` (levels, ring buffer, file sink under the data dir), `Paths` (`%LOCALAPPDATA%/Trinity`, `Documents/Trinity Projects`, env override `TRINITY_DATA_DIR`, user-configurable workspace), `FileSystem` (path validation/traversal-safety, safe temp dirs), `Time` (ISO-8601 UTC), `Json` (small JSON value/parser/writer), `Hash` (SHA-256), `Uuid` (v4 from `std::random_device`).
Unit tests for every module. **Gate: tests green.**

### D2 — Persistence
Vendored SQLite amalgamation; `Database` (RAII handle, WAL, FKs, busy timeout); `Migrations` (versioned, transactional, `PRAGMA user_version`); repos mirroring V1 schema plus `projects` (full), `settings`, `workflows`, `logs`, `engine_meta`. Seeded default settings row.
Tests: migrate-up on fresh DB, idempotent re-open, repo CRUD.

### D3 — Engines + validation
`IEngine` (id, name, version, capabilities, in/out schema, execute, validate, health), `EngineRegistry` (register/discover/list/health-all). CAD: ported `QuadcopterFrame` IR (same parameter rules), mesh builder (same numbers), binary STL writer (byte-compat asserted), validators (same checks), C++ mesh `preview` metadata. Math: deterministic expression evaluator (shunting-yard; +,-,*,/,%,^, parentheses, functions `sin cos tan sqrt log exp abs`, constants `pi e`) + `solve` for linear/quadratic polynomial forms with residual re-check; honest `engine_execution_error` otherwise. Scaffold engines for pcb/firmware/vision/research/simulation/robotics raising `capability_unavailable`. `ICADAdapter` with `generate/import/export/validate/preview`; `MeshKernelAdapter` (local) registered; OpenSCAD/CadQuery/FreeCAD/Onshape adapters as unavailable-error stubs exactly like the Python ones.
Tests: STL byte layout, validator truth table, IR rejection cases, registry discovery, math evaluator vs known values, solve residual verification.

### D4 — Jobs, artifacts, workflows, validation lifecycle, monitoring
`JobSystem`: worker threads, priority queue, `Uuid` jobs with full record (uuid, timestamps, project, engine, input, output, logs, duration, status, error, artifacts), pause/cancel via cooperative tokens, log streaming ring per job. `ArtifactStore` (single-writer, sha-256, metadata, type registry covering STEP/STL/3MF/GLB/OBJ/JSON/CSV/TXT/PDF/PNG + source/reports). `ValidationEngine` enforcing GENERATED→VALIDATED→VERIFIED transitions with recorded evidence rows. `Workflow` DAG (port of `ordered()` semantics) executing nodes as jobs with dependency fan-in. `ResourceMonitor`: CPU/memory sampling for the process and system snapshot (pure C++, no PSAPI dependency tricks beyond Win32/pthread-compatible fallbacks).
Tests: job lifecycle incl. cancel/pause, artifact round-trip + hash stability, DAG cycle rejection, transition-guard rejections.

### D5 — Commands + model layer
`CommandParser` (deterministic grammar seeded from `intelligence/router.py`: CREATE/OPEN/IMPORT/EXPORT/CALCULATE/VALIDATE/SIMULATE/SEARCH/ANALYZE/GENERATE/COMPARE/OPTIMIZE; `create a 50 mm quadcopter frame` → CAD generate plan), `CommandPlanner` interface (deterministic `ScriptedPlanner` now), `ToolExecutor` mapping plans → engine ops → jobs. Model layer: `IModelProvider`, `ICADGenerator`, `IPCBGenerator`, `IEngineeringReasoner`, `NullModelProvider` + `Mock*` implementations that emit deterministic structured tool calls; settings UI surface exposes provider config **schema only** (endpoint/model/keys placeholders, no network code).
Tests: parser table-driven, executor end-to-end producing a real job + artifacts, mock provider emits valid plan JSON.

### D6 — IPC + Python engine host + process runner
Framed JSON protocol (`IpcFrame`: id, type, payload; methods: `hello`, `engines.list`, `engine.execute`, `job.stream`, `ping`). `IpcServer` (in-process, for tests + future named-pipe transport) and `ChildIpcChannel` over a `ProcessRunner`-launched child with sanitized argv, env sanitization, output streaming, graceful shutdown + kill timeout. `python/services/engine_host.py`: line-delimited JSON-RPC over stdio wrapping the existing `app.engines.registry` (SymPy math, CAD GLB, scaffolds) — **no changes to `backend/app`**. `requirements-enginehost.txt` pinned.
Tests: in-process IPC echo/error paths; engine host round-trip (`math.solve` returns verified roots) — executed in CI where Python 3.13 exists, skipped with explicit "SKIP" when the interpreter is absent.

### D7 — Qt 6 shell (optional build) + console shell
Console app `trinity-shell` (ships now): REPL command console over the core, job monitor text UI — proves the full stack headless. Qt Quick app `trinity` (ships when Qt 6 is present, `TRINITY_WITH_QT=ON`): menu bar (File/Edit/View/Project/Tools/Help), dockable panels (Project tree, 3D viewport, Properties, Command console, bottom dock: Jobs/Logs/Artifacts/Validation/Output), command palette (Ctrl+K), settings dialog (brief's categories), native file dialogs, window-state persistence, high-DPI. Viewport: grid, axes, orbit/pan/zoom/fit, front/top/right, ortho/persp, wireframe/solid, object tree, selection, bounding box; measurement/section left as stubs marked "foundation only".
UI smoke tests where Qt test libs are available.

### D8 — Packaging + installer + CI
`installer/trinity.iss` (Inno Setup: bundle exe + runtime DLLs + python/ engine env + assets + config templates), `scripts/package_windows.py` (collects CMake output + wheel-able python env), CI job building `trinity_core` + tests on windows-latest (MSVC) with `-DTRINITY_WITH_QT=OFF` and running the doctest suite + backend pytest. **Release exe remains explicitly "not production-ready" until signed + Qt path verified on a real Windows desktop.**

## 4. Honesty ledger (what ships in which state)

| Capability | State after this migration |
|---|---|
| Native core (db/jobs/artifacts/validation/engines/commands/IPC) | Working, tested |
| CAD quadcopter_frame → STL + preview metadata | Working, byte-compat with V1 |
| CAD STEP/3MF | Not implemented — reported `capability_unavailable` |
| Math evaluate + linear/quadratic solve (C++) | Working, verified residuals |
| Math full SymPy (solve transcendental, symbolic) | Via Python engine host over IPC; absent interpreter ⇒ engine absent (health reflects it) |
| GLB export | Via Python engine host (existing writer) |
| PCB / Firmware / Vision / Research / Simulation | Scaffolds — `capability_unavailable`, UI shows "Scaffolded" |
| Proprietary model / CAD-AI / PCB-AI | **Not implemented by design** — interfaces + mocks only |
| Qt desktop UI | Builds only with Qt 6 installed; console shell is the always-ships UI |
| Trinity-Setup.exe | Installer scaffolding; not signed, not release-ready |

## 5. Verification checklist before merge

1. `cmake --preset windows-debug && cmake --build --preset windows-debug && ctest --preset windows-debug` green.
2. `pip install -r backend/requirements.txt && pytest backend/tests` still green (untouched).
3. `python app/native/python/services/engine_host.py --selftest` green.
4. `trinity-shell.exe` smoke: `new project`, `create a 50 mm quadcopter frame`, job completes, artifact stored with sha-256, `validate` moves GENERATED→VALIDATED.
5. CI: backend job green, native job green.

## 6. Implementation status — native core increment

This section records what the native core actually contains **today**, after the
increment that added the event bus, the platform/storage abstractions, the
plugin and command registries, the canonical interface set and the `Application`
lifecycle. It supersedes the aspirational wording above wherever the two
disagree.

### Brief items 1–14, and where each one lives

| # | Item | Implementation | State |
|---|---|---|---|
| 1 | Application lifecycle | `app/Application.{hpp,cpp}` — ordered bring-up, reverse teardown, idempotent, `status()` document | Implemented, tested |
| 2 | Configuration | `ApplicationConfig` + `settings/Settings` (SQLite-backed, seeded descriptors) + env overrides in `storage/Storage.cpp` | Implemented, tested |
| 3 | Logging | `core/Logging` (levels, ring buffer, file sink, pluggable sinks) | Implemented (pre-existing) |
| 4 | Error handling | `core/Error` + `core/Result` (typed codes, `TrinityException`, no exceptions across public APIs) + `platform::install_terminate_handler` | Implemented, tested |
| 5 | Event bus | `core/EventBus.{hpp,cpp}` — topic patterns, replay ring, handler-exception isolation, concurrent publish | Implemented, tested |
| 6 | Command system | `commands/Commands` (parser → planner → executor) + `commands/CommandRegistry` + `commands/BuiltinCommands` | Implemented, tested |
| 7 | Project manager | `projects/Project` (`ProjectStore : IProjectStore`) | Implemented (pre-existing) |
| 8 | Job manager | `jobs/JobSystem` (`JobSystem : IJobManager`, `JobHandle : IJob`), worker pool, pause/resume/cancel, event publishing | Implemented, tested |
| 9 | Artifact manager | `artifacts/Artifact` (`ArtifactStore : IArtifactStore`, single writer, sha-256) | Implemented, tested |
| 10 | Engine registry | `engines/Engine` (`IEngine`, registry, `bootstrap_builtin_engines`) | Implemented (pre-existing) |
| 11 | Validation manager | `validation/ValidationEngine` (`IValidator`) — GENERATED→VALIDATED→VERIFIED with recorded evidence | Implemented, tested |
| 12 | Workflow manager | `workflows/Workflow` (`WorkflowRunner : IWorkflow`) — DAG ordering over the job system | Implemented (pre-existing) |
| 13 | Plugin manager | `plugins/PluginManager.{hpp,cpp}` — validated manifests, discovery, lifecycle, `plugin.changed` events | Implemented, tested (manifest scope, see limitation) |
| 14 | Settings manager | `settings/Settings` (`SettingsStore`) — categories, defaults, typed helpers | Implemented (pre-existing) |

### Canonical interfaces

`interfaces/CoreInterfaces.hpp` is the single include point that names all ten
contracts, and every one of them has a real implementation that is checked by
`static_assert(std::is_base_of<...>)` in `tests/test_application.cpp`:

| Interface | Implementation |
|---|---|
| `IEngine` | `engines::cad::CadEngine`, `engines::math::MathEngine`, `ScaffoldEngine` |
| `IJob` | `jobs::JobHandle` |
| `IJobManager` | `jobs::JobSystem` |
| `IArtifactStore` | `artifacts::ArtifactStore` |
| `IProjectStore` | `projects::ProjectStore` |
| `IValidator` | `validation::ValidationEngine` |
| `IWorkflow` | `workflows::WorkflowRunner` |
| `IModelProvider` | `model::NullModelProvider`, `model::MockModelProvider` (never a proprietary model) |
| `IPlugin` | `plugins::ManifestPlugin` |
| `ICommand` | `commands::NewProjectCommand` and the other built-ins |

### Threading contract (jobs never block the UI)

The job system previously invoked user callbacks while holding its internal
mutex — and `JobContext::report_progress` unlocked a mutex it did not own. Both
would have deadlocked or corrupted state the moment an observer (the event bus,
the Qt bridge) subscribed. The implementation now guarantees:

* every public method locks internally;
* every callback and every published event runs with **no** internal lock held,
  so a subscriber may call back into the job system (query, cancel) safely;
* terminal events are published before the idle signal, so `wait_for_idle()`
  returning implies all side effects are observable.

### Tests added in this increment

`tests/test_platform_storage.cpp` (layout resolution, injected environment,
traversal rejection, round-trips), `tests/test_events_plugins.cpp` (topic
matching, wildcard, unsubscribe, throwing handler, concurrent publish, manifest
validation, discovery, lifecycle, command registry + failure containment),
`tests/test_application.cpp` (boot/shutdown, command→job→event pipeline, CAD
artifact store→validate→verify, illegal transition refusal, plugin discovery,
argument validation).

### Still open (unchanged by this increment)

* **Plugin execution**: manifests are discovered, validated and reported; this
  build deliberately does **not** load third-party binaries or scripts.
* **Packaging drift**: the plan above sketched `installer/trinity.iss` (Inno
  Setup) and `scripts/package_windows.py`. What exists instead is CPack with the
  `ZIP` + `NSIS` generators plus `installer/scripts/package_windows.ps1`; the
  Python runtime is not bundled yet. See `installer/README.md` for the honest
  gap list.
* **Qt desktop shell**: still optional (`TRINITY_WITH_QT=ON`), not packaged, not
  smoke-tested on a clean machine.
* **Native equivalents** for PCB / vision / firmware / simulation / research: still
  scaffolded engines that report `capability_unavailable`; the Python engines
  remain reachable through the IPC bridge and are intentionally untouched.
