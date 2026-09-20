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
    engines/{Engine,EngineRegistry,MathEngine,CadEngine,StubEngines}.hpp
    jobs/Job.hpp
    workflows/Workflow.hpp
    validation/ValidationResult.hpp
    artifacts/Artifact.hpp
    intelligence/{ToolCall,Intent,ModelRequest,ModelResponse,IModelProvider}.hpp
    storage/{Database,Repositories}.hpp
  src/{core,fs,engines,jobs,workflows,validation,artifacts,intelligence,storage}/...
  ui/MainWindow.{hpp,cpp}
  tests/test_{main,error,registry,workflow,database,model_provider,paths_config,
             jobs,uuid,serialization,filesystem,repositories,logging_config,
             math_engine,cad_stubs,validation}.cpp
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

Suites (47 cases, GUI-independent): error envelope + source/timestamp,
engine registry (register/dup-reject/unregister/listCaps/routing +
unknown/unsupported/invalid handling), math evaluation, cad skeleton +
domain stubs, validation states + severity messages, workflow ordering,
SQLite schema + transactions + prepared statements, null model provider
(`generate` + `generatePlan`), paths/config (Windows dirs), jobs +
artifact checksums, UUID, model serialization round-trips, filesystem
ops + safeJoin, repositories CRUD, logging file + buffer.

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
- `IModelProvider::generate` seam + `NullModelProvider` (app runs fully
  without an LLM)
- Engine architecture: `IEngine::{name,capabilities,execute,validate}` +
  `EngineBase` helpers (capability check, structured errors, timing,
  logging, metadata) + `EngineRegistry::{register,unregister,get,has,
  list,listCapabilities,execute}` with duplicate rejection and
  Request→Registry→Capability-check→Execute→Validate→Result routing
- `MathEngine::evaluate_expression` deterministic (`2 + 3 * 4` → `14`);
  `CADEngine` skeleton (describe + truthful `CAPABILITY_UNAVAILABLE`);
  PCB/Firmware/Vision/Research/Simulation/Robotics stubs registering
  metadata and refusing without fake results
- Validation `GENERATED/VALIDATED/VERIFIED/INVALID` (+`FAILED` alias) with
  `ValidationMessage{rule,severity(INFO/WARNING/ERROR),passed,message,details}`
- Qt `MainWindow` engine list (MATH implemented vs scaffolded/unavailable)
- `Trinity.exe` (+ `--selftest`: 8 engines, math check, refusal check),
  `trinity_tests` via CTest

## Intentionally left for later phases

- Full Math (symbolic/numeric beyond `evaluate_expression`)
- Full CAD quadcopter geometry, mesh builders, exporters
- Real PCB/Firmware/Vision/Research/Simulation/Robotics implementations
- Real `IModelProvider` implementations (OpenAI, Anthropic, local,
  custom Trinity model)
- Installer/packaging (CPack/NSIS), code signing, update channel
- QML workspace, 3D viewport, project tree, settings dialogs
