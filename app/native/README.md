# Trinity Native (Windows C++)

Native Windows foundation for Trinity AI: C++20, CMake, Qt 6, MSVC x64,
SQLite (vendored), nlohmann/json (vendored). The Python backend in `src/`
and the Next.js frontend in `frontend/` are untouched and keep their own
CI; this tree is the future native host.

## Architecture

```
bootstrap (src/main.cpp)
  configuration (core/Config) -> logging (core/Logger, JSON lines)
  -> paths (core/Paths) -> SQLite init (storage/Database)
  -> engine registry (engines/EngineRegistry)
  -> model provider (NullModelProvider — no LLM connected)
  -> Qt window (ui/MainWindow) -> clean shutdown
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
# Headless startup check (no display needed, exit 0 = healthy)
.\build\debug\Debug\Trinity.exe --selftest

# Desktop window (needs Qt runtime next to the exe — see below)
.\build\debug\Debug\Trinity.exe
```

Deploy Qt DLLs beside the exe before launching outside the dev shell:

```powershell
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe --debug .\build\debug\Debug\Trinity.exe
```

Set `TRINITY_STORAGE_ROOT` (default `./data`) and `TRINITY_DB_PATH`
to isolate state.

## Tests

```powershell
ctest --preset windows-debug
```

Suites: error envelope, engine registry, workflow ordering, SQLite
schema, null model provider, paths/config, jobs + artifact checksums.

## Implemented in this phase

- `include/trinity/...` + `src/...`: core, intelligence, engines,
  workflows, jobs, validation, artifacts, storage
- `IModelProvider` + `NullModelProvider` (LLM seam, unconnected)
- `EngineRegistry` (register/find/exists/list, planned-engine catalogue)
- Qt `MainWindow`: Trinity title, status line, core-init confirmation
- `Trinity.exe` (+ `--selftest`), `trinity_tests` via CTest

## Intentionally left for later phases

- Math / CAD / PCB / Firmware / Research / Simulation / Vision /
  Robotics engines (registry names reserved only)
- Real `IModelProvider` implementations (OpenAI, Anthropic, local,
  custom Trinity model)
- CAD mesh builders, validators beyond the status enum, exporters
- Installer/packaging (CPack/NSIS), code signing, update channel
- QML workspace, 3D viewport, project tree, settings dialogs
