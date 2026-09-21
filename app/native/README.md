# Trinity Native (Windows C++)

Native Windows foundation for Trinity AI: C++20, CMake, Qt 6, MSVC x64,
SQLite (vendored), nlohmann/json (vendored). The Python backend in `src/`
and the Next.js frontend in `frontend/` are untouched and keep their own
CI; this tree is the future native host.

## Architecture

```
main.cpp (thin shell)
  -> ApplicationContext (core/ApplicationContext)
       configuration (core/Config: app name/version, dev/release mode,
                      data dir, db path, artifact dir, log dir)
       -> logging (core/Logger: INFO/WARNING/ERROR/DEBUG, file + buffer)
       -> filesystem (fs/Filesystem: Qt-independent)
       -> paths (core/Paths) -> SQLite init (storage/Database)
       -> repositories (storage/Repositories: jobs, workflows, artifacts)
       -> engine registry (engines/EngineRegistry)
       -> jobs/artifacts managers
       -> model provider (NullModelProvider — no LLM connected)
  -> Qt window (ui/MainWindow, reads Logger::recent()) -> clean shutdown
```

Deterministic core principles (ported from `src/`):

- Models emit structured tool-calls only (`intelligence/ToolCall`);
  `IModelProvider` is the seam for OpenAI / Anthropic / local / custom
  Trinity models. `NullModelProvider` refuses truthfully.
- Engines implement `IEngine` (`Engine.hpp`); the registry supports
  register / get / has / list. No engineering engines ship yet.
- Jobs run `queued -> running -> completed|failed` with the same
  ToolResponse-style envelope as the Python backend.
- Validation states `GENERATED | VALIDATED | VERIFIED | FAILED` stay
  separate from generation. SQLite holds metadata only (WAL mode).

## Directory structure

```
app/native/
  CMakeLists.txt / CMakePresets.json (windows-debug/release)
  include/trinity/
    core/{Error,Result,Config,Logger,Paths,Uuid,Time,Json,ApplicationContext}.hpp
    fs/Filesystem.hpp
    cad/{FrameParams,Mesh,Builder,StlWriter,Validators}.hpp
    engines/{Engine,EngineRegistry,MathEngine,CadEngine,StubEngines}.hpp
    jobs/Job.hpp
    workflows/Workflow.hpp
    validation/ValidationResult.hpp
    artifacts/Artifact.hpp
    intelligence/{ToolCall,Intent,ModelRequest,ModelResponse,IModelProvider,
                   ModelProviderFactory,ExampleProvider,Planner}.hpp
    storage/{Database,Repositories}.hpp
  src/{core,fs,engines,jobs,workflows,validation,artifacts,intelligence,storage}/...
  ui/MainWindow.{hpp,cpp}
   tests/test_{main,error,registry,workflow,database,model_provider,paths_config,
              jobs,job_lifecycle,uuid,serialization,filesystem,repositories,logging_config,
              math_engine,cad_stubs,cad_generate,validation,provider_factory,
              planner,request_pipeline,workflow_executor}.cpp
  third_party/{sqlite,json,doctest}/
  build/{debug,release}/ (gitignored)
```

## Dependencies

- C++20, MSVC x64, CMake >= 3.24, Ninja, Qt 6.8 MSVC2022 (`CMAKE_PREFIX_PATH`)
- Vendored (no network): SQLite amalgamation, nlohmann/json, doctest
- Windows system libs: `bcrypt` (UUID + SHA-256), `shell32` + `ole32`
  (`SHGetKnownFolderPath` for `%LOCALAPPDATA%`)

## Build requirements

- Windows 10/11 x64
- Visual Studio 2022 BuildTools with `VCTools` + `Windows11SDK`
  (`winget install -e --id Microsoft.VisualStudio.2022.BuildTools ...`)
- CMake >= 3.24 and Ninja (`winget install Kitware.CMake Ninja-build.Ninja`)
- Qt 6.8.x MSVC2022 64-bit, e.g. via `pip install aqtinstall`:
  `aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt`
  and `CMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64`
- No network access needed at build time: SQLite, nlohmann/json and
  doctest are vendored under `third_party/`.

## Configure / build

```powershell
cd app/native
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.3\msvc2022_64"
cmake --preset windows-debug
cmake --build --preset windows-debug
```

## Launch Trinity.exe

```powershell
# Headless startup check (exit 0 = healthy; GUI subsystem so output
# goes to the parent console, dirs/db/log are the ground truth)
$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;" + $env:PATH
$env:TRINITY_STORAGE_ROOT = "$env:TEMP\trinity-verify"
.\build\debug\Debug\Trinity.exe --selftest

# Desktop window (needs Qt runtime next to the exe — see below)
.\build\debug\Debug\Trinity.exe
```

Deploy Qt DLLs beside the exe before launching outside the dev shell:

```powershell
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe --debug .\build\debug\Debug\Trinity.exe
```

State resolution (no hardcoded machine paths):

- `TRINITY_STORAGE_ROOT` (default `%LOCALAPPDATA%\Trinity`, fallback `./data`)
- `TRINITY_DB_PATH` (default `<storage>\trinity.db`)
- `TRINITY_LOG_DIR` (default `<storage>\logs`, file `trinity.log`)
- `TRINITY_MODE` (`development`/`release`; default from `NDEBUG`)

Verify after launch: 10 storage dirs exist, `trinity.db` contains
`jobs/workflows/workflow_nodes/artifacts` (+ legacy tables),
`logs/trinity.log` has startup/config/database/job/engine entries,
exit code is 0.

## Tests

```powershell
ctest --preset windows-debug --output-on-failure
# or: .\build\debug\Debug\trinity_tests.exe
```

Suites (118 cases, GUI-independent): error envelope + source/timestamp,
engine registry (register/dup-reject/unregister/listCaps/routing +
unknown/unsupported/invalid handling), math (precedence, variables,
functions/constants, power, evaluate, linear/quadratic solve with
residual checks, structured rejections), cad (defaults → 108-triangle
validated frame, bad-param rejections, stl+json artifacts through jobs,
STEP unavailable note, deterministic encoding), validation states +
severity messages, provider factory + selection + secrets, planner
(tool execution, refusal/skip paths), workflow ordering, SQLite schema
+ transactions + prepared statements, null model provider (`generate`
+ `generatePlan`), paths/config (Windows dirs), jobs + artifact
 checksums, UUID, model serialization round-trips, filesystem ops +
 safeJoin, repositories CRUD, logging file + buffer, job lifecycle
 (`createJob`/`executeJob`/`cancel`/`recoverOnStartup`,
 `isTerminal`/`canTransition`), background `JobWorker`, deterministic
 `WorkflowExecutor` (topo order, value propagation,
 `allowFailure`/`Skipped`), and `RequestPipeline` (parse → validate →
 route → job; validation never bypassed).

## Plugging in an LLM (for the model developer)

The app runs fully without a model (`null` provider). To connect one:

1. Copy `include/trinity/intelligence/ExampleProvider.hpp` +
   `src/intelligence/ExampleProvider.cpp` to your own files and rename
   the class. The sample compiles but is never registered — it refuses
   truthfully until you fill in the transport.
2. Implement `configure()` (read `TRINITY_MODEL_API_KEY` via
   `core::modelApiKeyFromEnv()` — never store or log the key),
   `generate()` (translate `messages` → your wire format; return only
   `{text, toolCalls}` — the Planner executes, never the provider),
   `streamPlan()`, and `info()`.
3. Register once at startup:
   `ModelProviderFactory::instance().registerProvider("mine", ...);`
4. Set the environment:
   `TRINITY_MODEL_PROVIDER=mine`, `TRINITY_MODEL_ENDPOINT=...`,
   `TRINITY_MODEL_NAME=...`, `TRINITY_MODEL_API_KEY=...`
   (key is env-only; `Settings::toJson()` and logs never contain it).
5. Run `Trinity.exe --selftest` — unknown ids fall back to `null`
   with a `requested provider unavailable` warning, so a typo can't
   prevent boot.

Planner contract (`intelligence/Planner`): model output → validate
each `ToolCall` against the registry (known engine + listed
capability) → run sequentially through `JobManager::runSync`
(tracked jobs, artifacts, validations) → stop on first failure,
rest marked `skipped`. Model refusal executes nothing.

## Implemented in this phase

- Core models (`EngineRequest/EngineResult`, `ValidationResult`, `Job`,
  `Workflow/WorkflowNode/WorkflowEdge`, `Artifact`, `Intent`, `ToolCall`,
  `ModelRequest` (+`Message`), `ModelResponse`) with strong enums and
  `toJson/fromJson` for persistence/logs/future LLM + tool calls
- Centralized `ErrorInfo` (code/message/source/details/timestamp) +
  `makeError`, used consistently
- Single UUID utility (`newUuid` via CNG, `isValidUuid`)
- `nlohmann/json` serialization for every core model
- Config manager (app name/version, dev/release mode, data dir, db path,
  artifact dir, log dir; `%LOCALAPPDATA%` defaults, env overrides)
- Filesystem service (Qt-independent: mkdir/exists/read/write/metadata/
  list/safeJoin)
- SQLite directly (open/init/schema/transactions/prepared statements;
  tables `jobs/workflows/workflow_nodes/artifacts` + legacy; repositories)
- Centralized logging (INFO/WARNING/ERROR/DEBUG → stderr + file +
  `recent()` for future UI)
- `ApplicationContext` owning config/logger/db/filesystem/services
  (`main.cpp` is a thin shell)
- `IModelProvider` plugin seam + `NullModelProvider` (app runs fully
  without an LLM) + `ModelProviderFactory` (register/create/list,
  env selection `TRINITY_MODEL_PROVIDER`, null fallback) +
  `ExampleProvider` skeleton + `Planner` (model → validated tool
  calls → `JobManager`, stop-on-first-failure, refusal runs nothing)
- Engine architecture: `IEngine::{name,capabilities,execute,validate}` +
  `EngineBase` helpers (capability check, structured errors, timing,
  logging, metadata) + `EngineRegistry::{register,unregister,get,has,
  list,listCapabilities,execute}` with duplicate rejection and
  Request→Registry→Capability-check→Execute→Validate→Result routing
- `MathEngine` (`evaluate_expression` plain arithmetic; `evaluate` with
  variables/functions/constants/power; `solve` single-variable numeric
  roots with residual verification, identity checks, `solve_for`)
- `CADEngine::generate` — parametric quadcopter frame over the
  dependency-free mesh backend (`cad/FrameParams,Mesh,Builder,
  StlWriter,Validators`): 108-triangle validated geometry, binary STL +
  spec JSON artifacts via `JobManager`, STEP reported
  `CAD_KERNEL_UNAVAILABLE`; PCB/Firmware/Vision/Research/Simulation/
  Robotics remain stubs registering metadata and refusing without fake
  results
- Validation `GENERATED/VALIDATED/VERIFIED/INVALID` (+`FAILED` alias) with
  `ValidationMessage{rule,severity(INFO/WARNING/ERROR),passed,message,details}`
- Qt `MainWindow` engine list (MATH/CAD implemented with capabilities +
  last results vs scaffolded/unavailable)
- `Trinity.exe` (+ `--selftest`: 8 engines, math check, cad 108-triangle
  check, refusal check, planner/model/provider checks),
  `trinity_tests` via CTest

### Engine quick reference

```powershell
# Math: plain, with variables, and solving (via JobManager envelope)
# {engine: math, operation: evaluate_expression, parameters: {expression: "2 + 3 * 4"}}
# → {value: 14}
# {engine: math, operation: evaluate,
#  parameters: {expression: "x^2 + 2*x + 1", variables: {x: 3}}} → {value: 16}
# {engine: math, operation: solve,
#  parameters: {expression: "x^2 - 4 = 0"}} → {solved_for: x, solutions: [-2, 2]}

# CAD: 50 mm quadcopter frame (via JobManager envelope)
# {engine: cad, operation: generate,
#  parameters: {type: quadcopter_frame,
#               parameters: {overall_size: 50, motor_count: 4},
#               outputs: [stl, json]}}
# → {triangle_count: 108, bounding_box_mm, spec} + VALIDATED + stl/json artifacts
# outputs may include "step" → reported under unavailable_formats, not a failure
```

## Intentionally left for later phases

- Math beyond single-variable numeric roots (transcendental systems,
  multi-variable solving, symbolic algebra)
- CAD GLB export, kernel-backed STEP (CadQuery/OpenCascade adapters),
  mesh booleans beyond box composition
- Real PCB/Firmware/Vision/Research/Simulation/Robotics implementations
- Real `IModelProvider` transport implementations (OpenAI, Anthropic,
  local, custom Trinity model — the factory + example + planner are
  ready; only the transport is missing)
- Installer/packaging (CPack/NSIS), code signing, update channel
- QML workspace, 3D viewport, project tree, settings dialogs
