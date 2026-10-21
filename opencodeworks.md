# OpenCode work log

Shared, versioned, and ticked as work lands. Codex works in this repo too, so
this is the one place both of us should look before starting.

Commits are one concern at a time. A line is ticked in the same commit as the
change it describes — never on its own.

---

## In progress

_Nothing in progress._

---

## Settled decisions — not open

- **Step 5 — Desktop shell: thin.** Decided: keep the pywebview window.
  `start.ps1` / `run.bat` already cover the need, and a native titlebar is a
  nice-to-have that would triple the build surface for no functional gain.
  Recorded here so it stops being re-opened every session.

---

## Done

- [x] **M2: SPI peripheral codegen shipped (the V1 refusal is history)**
  `configure_peripheral kind:"spi"` used to answer
  `CapabilityUnavailableError("SPI is not supported by FirmwareEngine V1")`
  while McuDatabase already defined SPI0/SPI1/SPI2 on three MCU families and
  CodeGen already emitted `#include <SPI.h>`. Closed as decided in the
  milestone plan (codegen counts as sufficient; no hardware required):
  the engine now validates mosi/miso/sck (+ optional cs) the same way
  UART/I2C validate their pins — existence on the selected MCU and
  not-already-mapped conflicts — stores a
  `{instance, mosi, miso, sck, cs?, freq_hz}` requirement, and applies the
  pin mappings (mosi/cs out, miso in, sck out); the frequency must be
  positive. Validators.cpp now checks `hasPeripheralKind("spi")` exactly
  like uart/i2c/pwm (the old always-INVALID rule is gone). CodeGen emits
  `#define <instance>_FREQ_HZ` in config.h and a real
  `SPI.begin()` + `SPI.beginTransaction(SPISettings(...MSBFIRST,
  SPI_MODE0))` in setup. The UI combo gains "spi" with
  MOSI,MISO,SCK-in-one-field entry and optional CS. Tests: renamed the
  peripheral case (supports uart i2c spi pwm) with a full-SPI success path
  asserting all four pin mappings, plus missing-pin / freq 0 / pin-conflict
  refusals; validator case asserts SPI requirement stays VALIDATED on ESP32
  and goes INVALID on an IR with an empty peripheral list; the
  deterministic-generation case gains a Config D asserting
  `#include <SPI.h>`, the SPISettings line, `SPI0_FREQ_HZ 2000000` and the
  MOSI pin define. Native: 365 cases / 5,953 assertions, all green locally.

- [x] **M1: real LLM transports behind IModelProvider (openai / anthropic / local)**
  The skeleton providers answered "networking is not implemented" — now real.
  An injectable `IHttpTransport` seam (synchronous POST, timeout budget,
  64 MiB body guard) is implemented by WinHTTP on Windows; contract tests
  inject a fixture transport and stay fully offline. `HttpChatProvider`
  implements the whole IModelProvider surface once (generate / generatePlan /
  streamPlan / configure / info) and the wire formats plumb in as thin
  subclasses: `openai` (chat/completions + `trinity_execute` function
  tool), `anthropic` (messages API, x-api-key + anthropic-version headers,
  tool_use blocks), `local` (OpenAI-compatible endpoint — default moved
  from Ollama's native /api/chat to its /v1 surface so one wire path seeves
  Ollama/LM Studio/llama.cpp; keyless by design, matching local reality).
  Secrets come only from TRINITY_MODEL_API_KEY (existing contract) or a
  programmatic api_key for fixture tests, and are never logged. All wire
  failures map to structured refusals - providers never throw. streamPlan
  is honestly single-chunk (full plan round trip); incremental SSE stays
  unshipped and documented. New doctest file test_llm_transports.cpp: 11
  cases / ~51 assertions incl. wire-body assertions (Authorization header,
  anthropic-version, tool_choice, system message position), tool-call
  mapping for both formats, unknown-tool filtering, HTTP-error mapping,
  transport-failure refusal, disabled/refusal paths. Native: 365 cases /
  5,933 assertions, all green locally. A teachable bug found en route:
  `Json{"literal"}` is an ARRAY in nlohmann braced-init — the system prompt
  silently wrapped itself into a one-element array; fixed with an explicit
  std::string constructor. The default-transport off-Windows note and the
  stale "only NullModelProvider ships" README claims are corrected too.

- [x] **PR #31 — the Actions bump was still applicable, so it is merged**
  Dependabot's checkout 4→7 / setup-python 5→7 / setup-node 4→7 / cache 4→6
  PR had sat open since Sept 24; its failing native leg was the same
  Verify-OpenCV bug just fixed, not the bump. Merged locally (conflicts
  auto-resolved) rather than left dangling: every workflow now pins
  checkout@v7, setup-python@v7, setup-node@v7, cache@v6 — verified by grep
  across .github/workflows before pushing.

- [x] **CI: the vcpkg OpenCV leg hardened after the Verify-OpenCV failure**
  The leg failed at "Verify OpenCV found" on `0bc105a` — Configure had exited 0
  because `find_package(OpenCV QUIET)` does not fail on a miss, while the
  three `cancel-in-progress` kills immediately before it left the binary-cache
  state unverifiable (observed, not asserted — no cache contents forensics are
  possible). Three changes make a miss loud and the entry set trustworthy:
  the cache key gets a `v2` prefix (invalidates every earlier entry), the
  unconditional fallback restore is removed (a fallback hit is exactly where
  an incompatible older entry sneaks in), and the leg configures with
  `-DTRINITY_REQUIRE_OPENCV=ON` — a new CMake option that turns a silent
  `OpenCV not found` into a Configure `FATAL_ERROR`. The Verify step remains
  as belt-and-braces (its mojibake error string is fixed too). Verified
  locally: a reconfigure with the option ON still finds OpenCV 4.14.0.

- [x] **CI: the e2e suite stops being cancelled mid-run**
  The commit that first added the e2e job was cancelled before it ran, as was
  the push before it — the suite could pass once by luck and then never run
  again on an active repo. Cause was workflow-level `cancel-in-progress:
  true`, which kills the entire run.

  **The first fix was wrong.** Job-level `concurrency` with
  `cancel-in-progress: false` does *not* exempt a job from its workflow's
  cancellation policy — the run still came back `cancelled`. A job cannot opt
  out of the workflow that contains it.

  The working fix was splitting the file: `.github/workflows/e2e.yml` with
  `cancel-in-progress: false` at workflow level, which is the only level that
  governs. `native-windows.yml` keeps `true`, so the cheap vcpkg builds still
  cancel on push.

  **Observed, not asserted**: `222fb03` stayed `in_progress` while `0bc105a`
  landed, and the new run went `pending` behind it — running one finishes,
  newest queued one follows. `native-windows` on the same pair was
  `cancelled`, as intended.

- [x] **Step 4 — First real CI run, verified against the API**
  Checked rather than assumed. On `2d9d252`: `CI` **success**, `Native
  Windows` **success**, `PR Hygiene` **success**.

  Two claims that were previously only intentions are now settled:
  - **`cad-kernel` passed**, which means the STEP kernel tests *executed* in
    CI rather than skipping. That job asserts it explicitly and would have
    failed the workflow otherwise — so a green result here is evidence the
    assertion held, not just that nothing crashed.
  - **The `e2e` job passed**, so all 81 browser assertions ran on a real
    GitHub runner with a real native build, headless Edge and live ports.

  The per-job endpoint requires auth, so individual job names could not be
  listed — but a workflow conclusion of `success` means every job in it
  passed. Ticking this on "the YAML parses" would have been the same error as
  the engine registry labelling working engines `501`.

- [x] **Step 6 — Canonical binary rebuilt, launcher decoupled from path length**
  The launcher had been silently running `build/verify-vcpkg` — a scratch
  directory created to avoid clobbering a known-good binary — purely because
  its path was shorter. Delete that folder and the launcher would flip to a
  different executable with no signal.

  Fixed at the cause rather than papered over: rebuilt
  `build/debug-vcpkg` through the **documented preset**, so the build the
  README tells people to use is the one that produces the binary we run. It
  reports `OpenCV found: 4.14.0` and passed 354 native cases / 5,882
  assertions plus all five vision ops. Then deleted the scratch tree — 847 MB
  reclaimed — and confirmed the launcher now resolves
  `build/debug-vcpkg/Debug/Trinity.exe`, matching `CMakePresets`.

  No tracked files changed: this was build state, not source.

- [x] **Step 3 — Desktop shell's window, proven**
  Added `--window-timeout N`, which closes the window from another thread so
  `webview.start()` returns and the shutdown path actually runs. That converts
  "closing the window kills both processes" from a hand-waved claim into
  something observed: window opened, auto-closed, **ports 3000 and 8000
  released, no leak**.

  Testing it surfaced a bug I had just introduced. `find_qt()` returned
  `None` both for "Qt is already on PATH, nothing to add" and "Qt is not
  installed", so the caller warned *"the native engine will not start"* and
  then watched the bridge answer a call two lines later. It now returns
  `(directory_to_add, ok)` and all three states are verified separately.

- [x] **Step 2 — Root `README.md`**
  The gap was real but narrower than claimed: there *was* a Getting Started
  section, it just documented the old two-terminal manual flow, and there was
  no testing section at all in a project whose identity is verification. Now:
  launcher-first quick start, the optional native engine, the Qt gotcha, a
  Testing section covering pytest + the browser suites, and an explicit note
  on why LLM transport tests are skipped. Every command in it was executed and
  every file and number in it checked before committing.

- [x] **Step 1 — STEP kernel into CI**
  Added a `cad-kernel` job and a `cad` extra in `pyproject.toml`, so the
  dependency finally has one named home instead of a comment pointing at pip.
  The job asserts the kernel tests **executed** rather than trusting a green
  exit — a skip goes green too. Verified both directions: an all-skipped log
  fails the job, an all-passed log passes.

- [x] **Phase 0 — Engine registry false claims**
  Three found: a hardcoded fallback marking working engines as scaffolds, a
  permanent pre-fetch status guess, and capability chips falling back to
  local copy. A fourth (`BRIDGED · 501` — the 501 for a fully implemented
  engine) was caught while verifying my own fix.

- [x] **Phase 1 — `/EHsc` was missing**
  Never set by `CMakeLists.txt`. Every documented build used the Visual Studio
  generator, which injects it silently, so the project had never been built
  without exception unwinding. Under Ninja the engines would compile with
  C4530 warnings and lose unwind semantics on every `try`/`catch` — a warning,
  not a failure. Surfaced only because doctest `static_assert`s on disabled
  exceptions. vcpkg itself was never missing; it was off `PATH`.

- [x] **Phase 2 — e2e into CI**
  Job added to `native-windows.yml`, reusing the existing vcpkg/OpenCV leg and
  driving the stack through `start.ps1` so the launcher is covered too.

- [x] **Phase 3 — Redesign zip**
  Archived to `_archive/` with rationale. 76 hued colours against a zero-hue
  mandate, and it still carried the deleted `Projects.tsx`.

- [x] **Phase 4 — Desktop shell**
  `desktop.py` + optional `requirements-desktop.txt`.

- [x] **Phase 5 — SPI peripheral**
  Already refused honestly by `FirmwareEngine`; reasoning documented so it
  reads as deliberate rather than missed.

- [x] **Product bug: math answered "CAD part generated"**
  The engine solved the equation and the UI discarded the answer, inventing a
  CAD part instead. Found by writing the math e2e suite — the last engine with
  no coverage.

- [x] **Product bug: OpenCV stdout noise broke the bridge**
  Seven `plugin_loader` lines on stdout before the envelope made
  `edge_detect` report "unparseable output". Canny is the first op to reach
  `cv::parallel`.

- [x] **Product bug: uploads accepted undecodable formats**
  API took `tif`/`tiff`/`webp`; `VisionEngine::supportedFormats()` is
  `png`/`jpg`/`jpeg`/`bmp`.

- [x] **Product bug: launcher reported healthy with every engine dead**
  Qt was missing from `PATH`, and `/api/health` only proves the process is up.
  All eight engines are native. It now makes a real engine call first.

- [x] **Four phantom capabilities removed**
  `pcb/inspect`, `vision/ocr`, `vision/document_parse`,
  `vision/geometry_extract` — advertised, mapped nowhere, and absent from C++
  entirely. The guard that should have caught them had a
  `DOCUMENTED_UNMAPPED` escape hatch, and all four were inside it. Removed.

- [x] **Repo hygiene: `app/native/vcpkg_installed/` was unignored**
  Created inside the repo by manifest mode. Git tried to stage 25 MB of
  vendored binaries.

---

## Baseline

Last full run, against a freshly built native binary:

| | |
|---|---|
| e2e | 81 assertions, 10 suites, 0 failed |
| python | 312 passed (294 without the native bridge) |
| native | 354 cases / 5,882 assertions |
| ruff + lint + build | clean |

LLM transport tests are permanently skipped by decision — no API keys.