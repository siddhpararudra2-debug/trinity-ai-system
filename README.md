# ♾️ Trinity AI System

[![Python 3.11+](https://img.shields.io/badge/Python-3.11%2B-blue.svg)](https://www.python.org/)
[![FastAPI](https://img.shields.io/badge/FastAPI-0.110%2B-00a393.svg)](https://fastapi.tiangolo.com/)
[![Next.js 16](https://img.shields.io/badge/Next.js-16-black.svg)](https://nextjs.org/)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

> **An LLM-Independent Engineering Operating System.**  
> Models reason and emit structured tool-calls, deterministic engines execute them, and independent checks validate the results.

Trinity AI is a modular, multi-platform engineering operating system designed to separate AI reasoning from deterministic execution. By acting as an orchestrator, Trinity ensures that Large Language Models (LLMs) only define *what* needs to be done via structured tool-calls, while highly reliable, domain-specific deterministic engines handle the *execution* and *validation*.

```text
API Request 
 └─> Structured Request 
      └─> Job Manager 
           └─> Engine Registry 
                └─> Domain Engine (Math, CAD, PCB, etc.)
                     └─> Validation 
                          └─> Artifact Manager 
                               └─> SQLite Lineage
```

## 🚀 Key Features

- **🧠 LLM-Independent Architecture**: Agnostic to the underlying model (OpenAI, Anthropic, Local). Models act solely as planners/reasoners.
- **⚙️ Deterministic Execution Engines**: Ships with robust built-in engines for diverse domains including Math, CAD Generation, PCB Design, Firmware, Vision, Simulation, Research, and Robotics. Beyond MVP stubs: PCB delivers geometric trace DRC plus RS-274X Gerber manufacturing export; Firmware performs genuine PlatformIO builds into real `.hex`/`.bin` artifacts; Vision runs real OpenCV contour extraction and ORB feature matching; Simulation integrates multi-body spring chains with RK4 and energy-conservation checks; Robotics solves IK with joint-limit enforcement.
- **🛡️ Strict Validation**: Output generation is strictly separated from validation and verification states (`GENERATED` | `VALIDATED` | `VERIFIED` | `FAILED`).
- **💻 Cross-Platform Foundations**:
  - **Python Backend**: High-performance FastAPI server handling jobs, storage, and AI interactions.
  - **Web Frontend**: Modern, reactive Next.js 16 / React 19 web application.
  - **Native Windows App**: A blazing-fast C++20 / Qt 6 native application layer for high-performance desktop execution.

## 📂 Project Layout

```text
trinity-ai-system/
├── run.bat                  # double-clickable launcher
├── start.ps1                # starts API + frontend, health-checks, shuts down clean
├── desktop.py               # same stack in a native window
├── opencodeworks.md         # shared work log (Codex and OpenCode both tick this)
├── app/native/              # C++20 / Qt6 native engine (Trinity.exe)
├── config/                  # system configurations (YAML)
├── data/                    # caches, artifacts, evaluation sets
├── docs/                    # architectural documentation
├── frontend/                # Next.js 16 web application
│   └── e2e/                 # browser suites driven over the DevTools protocol
├── notebooks/               # Jupyter notebooks for prompt experimentation
├── src/                     # Python FastAPI backend
│   ├── agents/              # domain-specific AI agents
│   ├── api/                 # FastAPI endpoints
│   ├── engines/             # deterministic execution engines
│   ├── llm/                 # model provider clients
│   └── ...
└── tests/                   # Python test suites
```

## 📚 Documentation

- [`docs/MILESTONES.md`](docs/MILESTONES.md) — the full project history, baseline through M9, verified against git
- [`docs/PROCESS.md`](docs/PROCESS.md) — how we build: working rules, settled decisions, hard-won lessons, the verification chain
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — Stage 7: release hardening (real v1.0.0 tag, STEP kernel in the bundle, signing, auto-update) plus CAD depth

## 🛠️ Getting Started

### Install from a release (Windows, no toolchain)

Grab the newest tagged release from
[GitHub Releases](https://github.com/siddhpararudra2-debug/trinity-ai-system/releases). Two
equivalent artifacts ship, both smoke-verified by CI (health, UI, chat page
and a live engine execution must all answer before anything is uploaded):

| Artifact | Use it when |
| --- | --- |
| `TrinityAI-<v>-win64-setup.exe` | Normal install. Per-user, no admin; Start Menu shortcut. |
| `TrinityAI-<v>-win64.zip` | Portable / no-install. Unzip anywhere, run `Trinity.bat`. |

`SHA256SUMS.txt` covers both files — verify before running. Everything
(native engine, API backend, web UI) is bundled; user data lands in
`%LOCALAPPDATA%\TrinityAI\data`. No Python, Node or Qt install is needed.

The release workflow (`.github/workflows/release.yml`) builds these on
`v*` tags: Release+vcpkg native build with the full doctest suite and
`--selftest`, Next.js static export, PyInstaller bundle, windeployqt, the
portable zip plus installer, and the SHA-256 manifest.

### The short version (from source)

```powershell
python -m pip install -e .
npm --prefix frontend install

run.bat          # or: .\start.ps1
```

`start.ps1` finds `Trinity.exe` and Qt by itself, starts the API and the
frontend, waits until **both actually answer**, opens a browser, and shuts
both down on Ctrl+C. It builds the frontend on first run.

| Entry point | What it does |
| --- | --- |
| `run.bat` | Double-clickable. Hands straight to `start.ps1`. |
| `start.ps1` | The launcher. `-Dev` for hot reload, plus `-NoBuild`, `-NoBrowser`, `-ApiPort`, `-WebPort`. |
| `python desktop.py` | Same stack in a native window. Needs `pip install -r requirements-desktop.txt`; falls back to the system browser without it. |

The native engine is optional. If `Trinity.exe` has not been built, everything
still starts — `cad` and `math` run in-process, and the six bridged consoles
report the backend unavailable rather than faking output. `start.ps1` prefers
the vcpkg/OpenCV build, because the plain build has no OpenCV and the vision
engine says that capability is missing instead of guessing.

> `start.ps1` does not open on a "healthy" API alone. It makes a real engine
> call first and reports what actually answered, because all eight engines are
> native and a healthy API with a dead bridge is a dead shell. If it cannot
> find Qt it says so — `Trinity.exe` links `Qt6Core` and ships no Qt DLLs
> beside itself.

### Persistent bridge (M8)

The Python bridge keeps **one long-lived `Trinity.exe --daemon` child** per
process instead of spawning a fresh one per operation. The child holds a
single engine registry hot in memory, so an op costs one pipe round-trip
(~0.2 ms) instead of a full Windows DLL/Qt bootstrap (~110 ms per spawn on
this machine — measured 800x+ on the pure transport path). A boot ping
handshake verifies the child before first use; a Qt runtime directory is
injected into the child's `PATH` automatically (`TRINITY_QT_BIN` override,
else `C:/Qt`); a child that dies is lazily respawned on the next call; and
a per-request deadline force-kills a hung engine. Toggles and verification:

- `TRINITY_NATIVE_DAEMON=0` forces the legacy single-shot `--exec` transport
  (also used automatically if a binary fails the daemon handshake).
- Tests: `tests/test_native_daemon.py` (mocked transport) and the daemon
  block in `tests/test_native_bridge_live.py` (PID reuse, kill-recovery,
  concurrent threads, and the >50x benchmark, gated on `TRINITY_NATIVE_EXEC`).

### Running the two servers by hand

Useful when you want one server restarting independently.

```bash
# backend — http://localhost:8000/api
python -m pip install -e .
uvicorn src.main:app --reload

# frontend — http://localhost:3000
npm --prefix frontend install
npm --prefix frontend run dev
```

The frontend reads `NEXT_PUBLIC_TRINITY_API_URL`, which is **inlined at build
time** — set it before `npm run build`, not just before starting the server.

### Native Windows App (C++ / Qt 6)

See [`app/native/README.md`](app/native/README.md) for architecture and build
instructions. Two things there are not optional: CMake needs an initialized
MSVC environment (a Developer Command Prompt, or `vcvars64.bat` sourced), and
the OpenCV leg needs vcpkg's manifest installed first.

### Optional: the CAD kernel

STEP export needs OpenCascade — about 1 GB of wheels, and deliberately not a
core requirement.

```bash
pip install ".[cad]"
```

Without it the catalog reports `step` as unavailable **with a reason**, and
STL/GLB/JSON are unaffected.

## 📡 API Reference

The backend provides a comprehensive REST API for interacting with the engines and managing jobs.

- **System**: `GET /api/health`, `GET /api/engines`
- **Execution**: `POST /api/execute`, `POST /api/math/solve`, `POST /api/cad/generate`, `POST /api/requirements/execute`
- **Intelligence (Stage 4)**: `POST /api/intelligence/stream-plan` (SSE token
  streaming), `POST /api/agents/coder/run`, `POST /api/agents/researcher/run`
  (multi-step loops), `POST /api/workflows/execute` (multi-node DAG with
  per-node status and `{node:<id>}.<path>` dependency threading)
- **State**: `GET /api/jobs`, `GET /api/jobs/{job_id}`, `GET /api/artifacts/{artifact_id}`

### Connecting your own LLM

Set `TRINITY_MODEL_ENDPOINT` (a full OpenAI-compatible chat-completions URL —
Ollama, LM Studio, llama.cpp-server, vLLM or your own server) and optionally
`TRINITY_MODEL_NAME`; no API key is needed. Availability is competitive:
until an endpoint is configured, streaming and agent endpoints degrade
truthfully (deterministic-router fallback for stream-plan, explicit
`capability_unavailable` refusal for agents) and never pretend a model is
attached.

### Example Request (CAD Generation)

```json
POST /api/cad/generate
{
  "type": "quadcopter_frame",
  "parameters": {
    "overall_size": 50
  },
  "outputs": ["stl", "glb", "json", "step"]
}
```

### Output Formats

| Format | Availability | What it is |
| --- | --- | --- |
| `stl`, `glb`, `json` | always | Binary STL, GLB and the IR spec — pure Python, no kernel |
| `step` | needs `pip install ".[cad]"` | AP214 faceted B-rep from OpenCascade |

STEP availability is decided per environment, not assumed. `GET /api/cad/catalog`
reports it under `outputs.step` with `available`, `schema`, `unit`,
`representation` and — when it is off — the reason. Asking for a format that
cannot be written never fails silently: the engine returns what it did write
and names the rest under `result.unavailable_formats`.

`step` is **faceted**: every triangle becomes a planar face, and each
connected body of the mesh becomes its own solid. A cylinder arrives as a
polygonal prism. It is a valid, importable, correctly-scaled solid — it is
not parametric and not the original analytic surface. Parts are assembled
from interpenetrating boxes, and STEP keeps those surfaces rather than
booleaning them, so the file inherits the mesh's own self-intersections.
Reported volume is a geometry self-check, not a bill of materials.

Every engine operation securely returns a standardized envelope:
`{ success, engine, operation, result, artifacts, validation, errors, job_id }`

## 📖 Documentation

For a deeper dive into the system's design patterns and capabilities, please refer to the [Architecture Documentation](docs/ARCHITECTURE.md).

## 🧪 Testing

Verification is the point of this system, so the tests are the interesting part.

```bash
# Python — 344 with the native bridge, 322 without
python -m pytest -q

# With engines: point at the binary. The bridge injects the Qt runtime
# directory into the child's PATH itself (TRINITY_QT_BIN overrides).
$env:TRINITY_NATIVE_EXEC = "app\native\build\debug-vcpkg\Debug\Trinity.exe"
python -m pytest -q
```

The **browser suites** are the ones that earn their keep. They drive a real
browser over the DevTools protocol — no Playwright, no puppeteer, nothing to
install but Node — and they found 8 real product bugs, 6 of them in paths that
had no coverage at all.

```bash
node frontend/e2e/run.mjs                  # all 10 suites, 81 assertions
node frontend/e2e/run.mjs pcb vision       # just these two
```

It needs the stack already running and a browser listening:

```powershell
$edge = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
Start-Process $edge -ArgumentList '--headless=new','--remote-debugging-port=9222',`
  '--user-data-dir=$env:TEMP\e2e','--no-first-run','about:blank'
```

Suites: `chat`, `firmware`, `math`, `pcb`, `registry`, `research`, `robotics`,
`simulation`, `theme`, `vision` — one per engine, plus the registry. They run
in CI too, in the `e2e` job of `native-windows.yml`.

Native tests: `ctest` after a CMake build, or
`app/native/build/<preset>/trinity_tests.exe`.

### What is deliberately not tested

LLM transport tests are skipped by design — they need a real `OPENAI_API_KEY`
or `ANTHROPIC_API_KEY`, and a mocked one would only prove the mock. Without a
key, `generate()` raises `CapabilityUnavailableError` rather than inventing an
answer, and there are tests for *that*.
