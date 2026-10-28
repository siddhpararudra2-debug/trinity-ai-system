# Trinity AI — Milestone History

The complete record of what this project shipped, milestone by milestone,
from the first commit through the standalone distribution. Written from the
work log (`opencodeworks.md`) and cross-checked against Git history; commit
hashes are verified, never from memory.

Status at the time of writing (`316ff23`): **all planned milestones M1–M9
complete**, 404/404 native doctests, 348 passed / 24 skipped Python tests,
ruff clean, everything pushed to `origin/main`.

For *how* the work was done (rules, decisions, lessons), see
[PROCESS.md](PROCESS.md). For what comes next, see [ROADMAP.md](ROADMAP.md).

---

## Timeline at a glance

| Milestone | One-line outcome | Key commit(s) | Tests at closure (native / python) |
|---|---|---|---|
| Baseline | Real engines, real CI, e2e — "everything either works or refuses" | — | 354 / 312 |
| M1 | Real LLM transports (openai / anthropic / local) behind `IModelProvider` | `d24fa6f` | 365 / — |
| M2 | SPI peripheral codegen (V1 deliberate refusal closed) | `b49e6bc` | 365 / — |
| M3 | Honest symbolic math layer (differentiate / simplify / solve) | `167820d` | 371 / — |
| M4 | CAD CSG booleans + faceted STEP export (then real OpenCascade STEP) | `5436fc5`→`da9f11a`→`3801d8b` | 376 / — |
| M6 | Vector store seam ships a working local store (refusal Gone) | `7e81aac` | — / 307+13 |
| M7 | "Restore Python engines" closed as a plan correction (premise was false) | worklog record only | — |
| M8 | Persistent native bridge daemon — ~450x (measured 615–820x) faster ops | `316c1e2`, `3ef963e` | 377+380 / 344 |
| Stage 4 | Intelligence layer: real SSE streaming, multi-step agent loop, DAG execution | `40d6b09`, `3b63b78` | 380 / 344 |
| Stage 5 | Engine depth: PlatformIO firmware, PCB trace DRC + Gerber, OpenCV contours/ORB, RK4 spring chain, robotics IK verified | `760c317`, `7b3408f`, `541934d`, `f4d1e5f`, `282bda5` | 404 / 347 |
| Stage 6 (M9) | Packaging & standalone distribution: portable zip + installer + release CI | `e87598e`…`48ff1cf` | 404 / 348 |

---

## Baseline: the foundation phase

Before the numbered milestones, the project went through a foundation phase
documented in the work log as "Phases 0–5" and a set of product-bug fixes.
The identity formed here shows up everywhere later:

- **Phase 0 — engine registry false claims.** Four problems found, one while
  verifying the first three: a hardcoded fallback marking working engines as
  scaffolds, a permanent pre-fetch status guess, capability chips falling
  back to local copy, and `BRIDGED · 501` — a 501 status for a fully
  implemented engine.
- **Phase 1 — `/EHsc` was missing.** Never set by `CMakeLists.txt`. Every
  documented build used the Visual Studio generator, which injects it
  silently. Under Ninja the engines would compile with C4530 warnings and
  *lose exception unwinding* — a warning, not a failure. Surfaced only
  because doctest `static_assert`s on disabled exceptions.
- **Phase 3 — the design zip was archived**, not iterated on: 76 hued colors
  against a zero-hue mandate, still carrying a deleted component.
- **Phase 4 — desktop shell** (`desktop.py` + optional
  `requirements-desktop.txt`).
- **Product bugs fixed**, each found by writing the test that was missing:
  - math answered "CAD part generated" — the engine solved the equation and
    the UI discarded the answer, inventing a CAD part instead
  - OpenCV stdout noise broke the bridge — seven `plugin_loader` lines on
    stdout before the envelope made `edge_detect` report "unparseable output"
  - uploads accepted undecodable formats — the API took `tif`/`tiff`/`webp`;
    the engine supports `png`/`jpg`/`jpeg`/`bmp`
  - the launcher reported healthy with every engine dead — `/api/health` only
    proved the process was up; all eight engines are native. It now makes a
    real engine call first
- **Four phantom capabilities removed** (`pcb/inspect`, `vision/ocr`,
  `vision/document_parse`, `vision/geometry_extract`) — advertised, mapped
  nowhere, absent from the C++ entirely. The guard that should have caught
  them had a `DOCUMENTED_UNMAPPED` escape hatch, and all four were inside it.

Baseline test state: 81 e2e assertions across 10 suites / 0 failed, 312
Python passed (294 without the native bridge), 354 native cases / 5,882
assertions, ruff + lint + build clean.

---

## M1 — Real LLM transports behind `IModelProvider`

**Goal:** the skeleton providers answered "networking is not implemented" —
make them real without leaking secrets or throwing from providers.

**What shipped:**
- An injectable `IHttpTransport` seam (synchronous POST, timeout budget,
  64 MiB body guard) implemented by WinHTTP on Windows; contract tests inject
  a fixture transport and stay fully offline.
- `HttpChatProvider` implements the whole `IModelProvider` surface once;
  the wire formats are thin subclasses: `openai` (chat/completions +
  `trinity_execute` function tool), `anthropic` (messages API with
  `x-api-key` + `anthropic-version` headers, `tool_use` blocks), `local`
  (OpenAI-compatible `/v1` surface — keyless by design, matching Ollama/LM
  Studio/llama.cpp reality).
- Secrets come only from `TRINITY_MODEL_API_KEY` or a programmatic api_key
  for fixture tests, and are never logged. All wire failures map to
  structured refusals — providers never throw.

**Mid-flight teachable bug:** `Json{"literal"}` is an *array* in nlohmann
braced-init — the system prompt silently wrapped itself into a one-element
array; fixed with an explicit `std::string` constructor.

**Verified:** new `test_llm_transports.cpp` — 11 cases / ~51 assertions
including wire-body assertions (Authorization header, `anthropic-version`,
`tool_choice`, system message position), tool-call mapping for both formats,
unknown-tool filtering, HTTP-error mapping, transport-failure refusal.
Native: 365 cases / 5,933 assertions.

**Commits:** `d24fa6f` (native transports ship behind IModelProvider),
`a677501` (OpenAI + Anthropic transports with honest refusal).

---

## M2 — SPI peripheral codegen

**Goal:** `configure_peripheral kind:"spi"` answered
`CapabilityUnavailableError("SPI is not supported by FirmwareEngine V1")`
even though `McuDatabase` already defined SPI0/SPI1/SPI2 on three MCU
families and `CodeGen` already emitted `#include <SPI.h>`. Close the V1
refusal — codegen counts as sufficient; no hardware required.

**What shipped:** validate `mosi`/`miso`/`sck` (+ optional `cs`) exactly the
way UART/I2C validate pins (existence on the selected MCU,
not-already-mapped conflicts); store a
`{instance, mosi, miso, sck, cs?, freq_hz}` requirement with a positive
frequency; apply the pin mappings (mosi/cs out, miso in, sck out).
`Validators.cpp` checks `hasPeripheralKind("spi")` like every other
peripheral kind — the old always-INVALID rule is gone. `CodeGen` emits
`#define <instance>_FREQ_HZ` in config.h and a real `SPI.begin()` +
`SPI.beginTransaction(SPISettings(..., MSBFIRST, SPI_MODE0))` in setup.
The UI gains a "spi" combo entry.

**Verified:** full-SPI success path asserting all four pin mappings,
missing-pin / freq-0 / pin-conflict refusals, validator stays VALIDATED on
ESP32 and goes INVALID with an empty peripheral list, and a deterministic-
generation case asserting the emitted SPI.h include, SPISettings line, and
frequency/pin defines. Native: 365 cases / 5,953 assertions.

**Commit:** `b49e6bc`.

---

## M3 — Honest symbolic math layer

**Goal:** differentiate / simplify / solve — built with a hand-rolled
symbolic MVP (no CAS dependency) and honest refusals over faked results.

**What shipped (MathEngine v0.4.0):**
- A grammar-mirror AST beside the untouched numeric hot path.
- `differentiate`: sum/product/quotient/chain/power rules; sin/cos/tan, exp,
  ln/log, sqrt, asin/acos/atan. `abs()` (piecewise) and
  `constant^variable` with base <= 0 are structured `RequestValidationError`
  refusals stated up front. Every single-symbol result is verified against a
  central difference at a deterministic probe (x=1.7 or the requested `at`
  point) and stamped as `derivative_check` in metadata.
- `simplify`: constant folding + identities iterated to a serialization
  fixpoint; `1/0`, negative-base fractional powers, `0^0` deliberately stay
  unfolded — eval refusals, not silently "fixed".
- `solve_linear` matrix form (up to 16 unknowns) via Gaussian elimination
  with partial pivoting; **singular is a defined success outcome** (`outcome:
  "singular"`), not a crash; solutions are per-row residual-verified.
- `solve_system` Newton (1..8 equations, 200-step budget) with a numeric
  centered-difference Jacobian and per-equation residual verification;
  contradictory and overspecified systems fail with structured errors —
  never a faked convergence.
- End-to-end wiring: intent whitelist + per-op param rules, requirement
  parser phrases, two new MainWindow combo ops with per-op hints, and
  explicit `validate()` branches per op (the previous generic branch demanded
  a numeric `value` and would have invalidated every symbolic result).

**Verified:** +6 cases / +129 assertions — derivative round-trips against
central differences at three probes, exact simplify strings including the
honest non-folds, matrix solve/singular/ragged/mismatch, circle+line Newton
(converges to √2 on both axes), contradictory/overspecified refusals, and a
pipeline e2e. Native: 371 cases / 6,082 assertions.

**Commit:** `167820d`.

---

## M4 — CAD CSG booleans and STEP export

**Goal:** boolean operations on CAD meshes, and STEP export that is not a
stub.

**What shipped:**
- A BSP mesh boolean kernel in `CadEngine` (csg.js algorithm ported to C++),
  verified against the reference implementation on Node — identical
  polygon/edge signatures including the 16-tri/12-unpaired offset-cube
  signature on triangulated operands.
- `box()` winding corrected outward, `intersect` restored to the
  `~(~A|~B)` double-inversion form, and `booleanMesh` heals T-junction
  seams BSP clipping leaves on triangle-soup operands.
- **Composite-part boolean bug:** `csgSubtract(standoff, plate)` mixed true
  facet vertices with drifted interpolated crossings from the subtract
  flow's clip passes — fixed by decomposing composite operands into single
  watertight shells via `unionMeshComponents` before clipping.
- STEP export: first faceted AP214 with planar faces sewn into shells
  (`5436fc5`→`3801d8b`), then replaced by **real OpenCascade STEP export**
  (`da9f11a`) — the STEP kernel had no coverage at all until a CI `cad-kernel`
  job asserted the kernel tests *executed* rather than trusting a green exit
  (a skip goes green too; verified in both directions).
- Intent router, validator, and UI wired for the `boolean` op with part-A/
  part-B selection.

**How it was verified:** direct CSG on cubes, cylinders, touching and offset
cases with analytic volume checks and watertight closure; composite parts
normalize into one shell with correct faceted-prism volumes; engine boolean
runs with structured watertight/finite/volume checks; STEP golden and
coordinate round-trip green. Native: 376 cases / 6,151 assertions.

**Commits:** `5436fc5` (BSP mesh booleans), `3801d8b` (composite fix),
`da9f11a` (real OpenCascade STEP export), `81d201f` (kernel into CI).

---

## M6 — The vector store seam ships a working local store

**Goal:** `src/utils/vector_store.py` raised `NotImplementedError` on
add/query — replace the refusal with a real implementation with zero new
dependencies, and keep the honest scope stated.

**What shipped:** deterministic hashed n-gram embeddings (FNV-1a bucketing,
words + 3-grams, L2-normalized, 256 dims default), cosine ranking, and
persistence in a private SQLite file (`vectors.db` under
`settings.embeddings_dir`) with insert-or-replace rows carrying JSON
metadata. API: add / add_text / query / search / delete / clear / count /
close. Hull docstring states the honest limits: it is a utility on top of
(not a replacement for) SQLite lineage; embeddings are lexical, not a
learned model; no network, no API keys; degenerate zero vectors score 0.0
rather than throwing.

**Verified:** 13 cases in `test_vector_store.py` — determinism,
normalization, empty-text zero vector, tiny-dim rejection, cosine bounds,
exact-match-first ranking, metadata round trip + upsert, persistence across
reopen. One test was *wrong en route* and fixed: it asserted score exactly
1.0 on a non-identical query text; cosine 0.82 was the correct answer.
Full Python suite: 307 passed (+13) / 20 skipped.

**Commit:** `7e81aac`.

---

## M7 — "Restore Python engines": closed as a plan correction, not code

The milestone plan said to restore RoboticsEngine/SimulationEngine/
ResearchEngine from git history behind a `TRINITY_PYTHON_ENGINES` gate.
**The premise was false**: those commits are native (C++) engine feature
commits — there was never a Python class of any of those names at any point
in the repo's full history (checked with `git log -S` over `*.py`: zero
hits). The Python side already implements the designed state:
`native_bridge` routes those engines to `Trinity.exe --exec` when the exe
exists, `ScaffoldEngine` refuses truthfully when it does not, and the
registry prefers native.

Restoring "Python engines from history" would have reinvented what shipped
natively and violated the settled bridge-over-duplication decision. **No
code change** — the work log entry is the record. The remaining genuinely
open Python seams became M8 (persistent bridge) and Stage 6 (packaging).

---

## M8 — Persistent native bridge (~450x faster ops, measured 615–820x)

**Goal:** every engine op paid a full `subprocess.run --exec` — Windows
DLL/Qt bootstrap ~110–440 ms per op. Replace it with one hot daemon
process.

**What shipped:**
- `Trinity.exe --daemon` (C++ side: `runDaemon` + NDJSON pipes, doctested —
  ping/pong, sequential engine ops through one hot `EngineRegistry`,
  recovery on malformed input).
- Python `NativeDaemon` in `src/engines/native_bridge.py`: binary pipes
  (a text-mode pipe would deliver `"exit\r"`, which `std::getline` executes
  as an op instead of shutting the child down), one reader thread + queue so
  per-request deadlines work on Windows, a single lock serializing
  write→read so responses pair 1:1 with requests, automatic Qt/vcpkg PATH
  injection, OpenCV stdout-noise tolerance with a line cap, a boot ping
  handshake, lazy respawn after a child crash, deadline kills, atexit
  shutdown, and a daemon-lifetime private storage root whose reported
  artifacts are copied out and deleted per call.
- Default on; `TRINITY_NATIVE_DAEMON=0` (or a failed handshake) falls back
  to the single-shot transport so an older binary still works.

**Verified:** 15 mocked tests (LF-only writes, PID stability, kill/timeout
recovery, noise caps, cleanup, singleton caching) + a live block: PID
stable across 10 HTTP calls, kill→respawn, 8-thread multiplexing, and a
benchmark asserted ≥50x — **measured 615–820x** (single-shot ~113 ms/op vs
daemon ~0.2 ms/op). Python suite: 344 with the bridge, 322 without, both
green. **Commits:** `316c1e2`, `3ef963e`.

---

## Stage 4 — Intelligence layer

**Goal:** real streaming, a multi-step agent loop, and DAG workflow
execution — the "LLM-independent OS" claim made operational.

**What shipped:**
- **Real SSE streaming (native):** `IHttpTransport::postStream(request,
  onChunk)` (default = one whole-body chunk, so legacy fixture transports
  stay valid), streaming body builders with a `stream` flag
  (default false — byte-stable body tests), `SseDeltaParser` line-buffers
  chunks (safe mid-UTF-8 and mid-JSON splits), parses OpenAI
  `delta.content`/`tool_calls[].function.arguments` fragments AND Anthropic
  `text_delta`/`input_json_delta` on its own wire via a
  `usesAnthropicWire()` virtual, and `HttpChatProvider::streamPlan` streams
  incremental `(chunk,false)` deltas with exactly one `(chunk,true)` final
  payload. A server that ignores `stream:true` gets its whole-body JSON
  mapped honestly instead of fake deltas. (380 native cases.)
- **Python side:** `src/llm/local_client.py` is the keyless
  OpenAI-compatible seam to the user's own LLM (`TRINITY_MODEL_ENDPOINT` /
  `TRINITY_MODEL_NAME`; honest `capability_unavailable` without it);
  `POST /api/intelligence/stream-plan` forwards real token deltas as SSE
  with a deterministic-router fallback; `src/agents/loop.py` runs
  model→tool-call→engine→observation iterations (errors framed for
  self-correction, step budget reported as `step_budget_exhausted`),
  powering real `CoderAgent`/`ResearcherAgent.run()`;
  `POST /api/workflows/execute` executes multi-node DAGs with per-node
  visible status, failed nodes truthfully block dependents as `queued`, and
  `{node}.<path>` references thread upstream results into child parameters.
- **Frontend:** `DagRunPanel` renders the pipeline with truthful status
  styling, mounted in TrinityWorkspace.

Everything degrades without a connected model — nothing pretends. Tests:
22 new local/loop/API tests; Python 344 passed / 24 skipped; native 380
cases. **Commits:** `40d6b09` (native streaming), `3b63b78` (loop, DAG,
local seam).

---

## Stage 5 — Engine feature depth (five engine commits)

**Goal:** make the advertised engines genuinely deep instead of scaffolded,
one engine per commit.

1. **Firmware — PlatformIO (`760c317`).** `platformIoSpecFor` maps
   ESP32/STM32/RP2040 to real boards; `runPlatformIoBuild` writes
   `platformio.ini` + source, runs the *real* `pio` binary, collects
   `.pio/build/*/firmware.{hex,bin,elf,uf2}` as artifacts; truthful
   `capability_unavailable` when no toolchain exists. Doctests drive a
   compiled fake `platformio.exe`, including an env-switched failure path.
2. **PCB — trace DRC + Gerber (`7b3408f`).** `TrackSegment` in the IR;
   geometric checks `trace.width`, `trace.clearance` (public
   segment-distance seam), `trace.in_bounds`, `trace.keepout`,
   `trace.net_known`; `add_track` op; RS-274X Gerber export (Mo=2/Nd=6,
   D01 draws + D03 aperture flashes, Edge.Cuts outline, per-layer
   self-verification before success).
3. **Simulation — RK4 + spring chain (`541934d`).** `method: "rk4"`
   integrates constant-force dynamics to machine precision — doctest-pinned
   against the closed-form trajectory while semi-implicit Euler deviates
   >1e-4; `spring_chain` couples Hooke integration over an explicit
   `objects[]` array with a KE+PE+total energy summary, energy conservation
   validated at 1%.
4. **Vision — contours + ORB (`f4d1e5f`).** `extract_contours` (Canny →
   `cv::findContours`, per-contour area/centroid/bbox, min-area filter);
   `match_features` (ORB detectAndCompute + Hamming knn + Lowe ratio test;
   honest zero keypoints/matches for flat images; refuses without OpenCV).
5. **Robotics — verification only.** DLS IK + joint-limit clamping already
   implemented; doctest coverage existed. Honest finding: no changes needed.

**Bridge:** `native_bridge.py` gained `pcb/add_track`,
`image_contours`→`extract_contours`, `image_match_features`→
`match_features`, `simulate_spring_chain` — the bidirectional
advertised↔mapped pin stays green.

Doctest counts after each commit: firmware 385, PCB 394, simulation 400,
vision **404/404 final**; Python 347 passed / 24 skipped; ruff clean.
**Commits:** `760c317`, `7b3408f`, `541934d`, `f4d1e5f`, `282bda5`.

**CI hardening en route (from the log):**
- The vcpkg OpenCV leg failed at "Verify OpenCV found" because
  `find_package(OpenCV QUIET)` does not fail on a miss — fixed with
  `TRINITY_REQUIRE_OPENCV=ON` (a new CMake option turning a silent miss
  into a Configure `FATAL_ERROR`), a cache-key `v2` bump, and removal of
  the fallback restore that let incompatible older entries sneak in.
- The e2e suite was being *cancelled mid-run* by workflow-level
  `cancel-in-progress: true`. **The first fix was wrong** — job-level
  `concurrency` with `cancel-in-progress: false` does not exempt a job
  from its workflow's cancellation policy. The working fix was splitting
  the file: `e2e.yml` with `cancel-in-progress: false` at workflow level,
  which is the only level that governs.
- Dependabot's stale Actions-bump PR (checkout 4→7 etc.) merged after
  verifying its failing native leg was the Verify-OpenCV bug, not the bump.

---

## Stage 6 (M9) — Packaging & standalone distribution

**Goal:** ship the whole stack — native engine, Python API, web UI — as
something a user runs with no Python, Node, or Qt installed. Every bundle
must be verified hermetically, not "should work".

**What shipped, in order:**
1. **Native release preset (`e87598e`)**
   `windows-release-vcpkg` — Release + vcpkg manifest + `TRINITY_REQUIRE_OPE
   NCV=ON`. Built green; all 404 doctests pass in Release.
2. **Frontend opt-in static export (`8edfc04`)**
   `TRINITY_STATIC_EXPORT=1` switches `next.config.ts` to
   `output: 'export'` (Next 16, verified against the bundled docs) — 12
   routes prerendered to `frontend/out/`. Opt-in so `next start` keeps
   working in dev.
3. **API serves the UI (`e4973ab`)**
   `TRINITY_UI_DIR` makes FastAPI mount the static export at `/` *after*
   the router. The `_StaticHtml` subclass maps `/chat` → `chat.html`
   through a **response-status retry**: `StaticFiles(html=True)` returns a
   404 FileResponse (never raises) when Next's `404.html` is present, so
   the first exception-based attempt was wrong and got rewritten. 4 new
   tests.
4. **PyInstaller bundle (`3ac8f21`)**
   onedir spec + `packaging/trinity_api.py` entry (imports `src.main`
   directly — no import string). `config/*.yaml` land in
   `_internal/config/` so frozen `model_config` resolution keeps working.
   Frozen smoke: health / UI / chat / 404 all answered.
5. **Qt standalone bundle (`e122ba0`)** — `packaging/bundle_native.ps1`:
   14 vcpkg/OpenCV DLLs + windeployqt Qt6 runtime, then a **hermetic
   selftest with the system PATH fully scrubbed** — the bundled DLLs are
   the only option, and it passed.
6. **Portable distribution (`829ef6e`)**
   `packaging/package.ps1` assembles `dist/TrinityAI-1.0.0-win64/` (native/
   + api/ + web/ + `Trinity.bat` launcher) and runs the full smoke:
   `SMOKE OK: health ok; ui served; chat route ok; engine probe ok` —
   including the native bridge answering from *inside* the bundle. Zips
   the release artifact.
7. **Inno Setup installer (`d458477`)**
   Per-user install to `%LOCALAPPDATA%\Programs\TrinityAI` (no admin) with
   a Start Menu shortcut. `build_installer.ps1` silent-installs into a
   scratch dir and smoke-tests the **installed** copy — not the staging
   directory — before the setup exe counts as done.
8. **CI release workflow (`48ff1cf`)**
   `.github/workflows/release.yml` on `v*` tags: native release-vcpkg build
   (vcpkg cache + install-qt-action), doctests + `--selftest`, static
   export, PyInstaller, windeployqt, portable zip + installer, a SHA-256
   manifest, and `gh release upload`. Nothing reaches a GitHub Release
   until every smoke gate passes.
9. **README (`9048b58`)** — install-from-release section: setup exe vs
   portable zip, SHA-256 verification, no-toolchain note.

Close state: python 348 passed / 24 skipped; ruff clean. Commits
`e87598e`…`48ff1cf`, docs close `316ff23`.

---

## Where this leaves the project

Everything planned, shipped and verified end to end. The known gaps are
deliberate and documented in [ROADMAP.md](ROADMAP.md): unsigned binaries,
Windows-only packaging, no auto-update, and the OpenCascade STEP kernel not
carried into the bundle (the catalog truthfully reports it unavailable).
