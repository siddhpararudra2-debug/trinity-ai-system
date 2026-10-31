# OpenCode work log

Shared, versioned, and ticked as work lands. Codex works in this repo too, so
this is the one place both of us should look before starting.

Commits are one concern at a time. A line is ticked in the same commit as the
change it describes — never on its own.

---

## In progress

**Stage 8 — M14 CAD depth closed (one commit per op: `ca61424` families,
`07e1d69` nesting, `fe3e58f` drawings; suite 375 passed / 40 skipped,
ruff clean).**
Nothing further is defined in docs/ROADMAP.md after M14 — new milestones
get planned when needed. Until then the standing rule holds: any release
run re-tags `v0.9.0` at the newest commit (the workflow already carries
the python pin, strict gates, signing env, and the latest.yml
verify-as-published gate); `v1.0.0` stays untouched.

Post-M14 pipeline repair is recorded below: the CI updater fixture and the
never-green cad e2e suite are both fixed, locally verified at 377 passed /
40 skipped and 105 e2e assertions / 0 failed. Both live workflows verified
green (#184 CI, #22 Browser E2E). Stage 9 (M15 agentic workflow depth) has
started — plan contract first, see its section below.

Highlights of the M14 loop (all three ops landed green; code lessons in PROCESS.md):

- Families `ca61424`: truth per member — a bound-violating variant is
  reported `valid: false` with the part's verbatim reason and never
  dropped; per-member checksum = sha256 over canonical JSON of the
  triangle soup + spec, so equal spec ⇒ equal checksum and different
  geometry ⇒ different checksum.
- Nesting `07e1d69`: the published layout ships WITH its pairwise
  proof (no overlap, spacing held, all inside stock); the semantics
  lesson corrected mid-proof — parts side by side share their y-range,
  so the spacing rule is "either axis separates by the full spacing,"
  i.e. **max(gap_x, gap_y) ≥ spacing**, not min.
- Drawings `fe3e58f`: projections are the mesh's full unique wireframe
  (documented as engineering truth about THIS tessellation, not a
  hidden-line-removed print); pinned box counts (top/front/side=6,
  iso=18) verified against the 18-edge enumeration; the emitted DXF is
  recounted by parse-back before the op returns — a rendering drop
  raises instead of publishing a document that lost strokes.
- The gotcha of the stage: inserting `CADFamilySelector`/`CADFamilyRequest`
  between `CADGenerateRequest`'s fields and its `@field_validator`
  silently re-homed the validator onto the new class — unknown part
  types turned into 200 success:false envelopes instead of 422s. The
  existing suite caught it on the next full run. Rule recorded in
  PROCESS.md: schema-edge rules get route tests immediately, and any
  class insertion in `schemas.py` is followed by the schema-422 tests.

---

## Settled decisions — not open

- **Step 5 — Desktop shell: thin.** Decided: keep the pywebview window.
  `start.ps1` / `run.bat` already cover the need, and a native titlebar is a
  nice-to-have that would triple the build surface for no functional gain.
  Recorded here so it stops being re-opened every session.

---

## In Progress

- [ ] **Stage 6: M9 — packaging & standalone distribution**
  - [x] Native release preset: `windows-release-vcpkg` (Release + vcpkg manifest + `TRINITY_REQUIRE_OPENCV=ON`), built green, all 404 doctests pass in Release → commit `e87598e`
  - [x] Frontend opt-in static export (`TRINITY_STATIC_EXPORT=1` → `output: 'export'`; 12 routes prerendered) → commit `8edfc04`
  - [x] Backend serves the static export from the API process (`TRINITY_UI_DIR` mount after the router; `_StaticHtml` maps `/chat` → `chat.html` through a response-status retry since `StaticFiles(html=True)` returns a 404 FileResponse, never raises, when Next's `404.html` is present; 4 new tests) → commit `e4973ab`
  - [x] PyInstaller onedir bundle (`packaging/trinity-api.spec` + `packaging/trinity_api.py`; config/*.yaml land in `_internal/config/` so frozen `model_config` resolution works; packaged-mode smoke: health/UI/chat/404 all answered) → commit `3ac8f21`
  - [x] Qt standalone bundle: `packaging/bundle_native.ps1` (14 vcpkg/OpenCV DLLs + windeployqt Qt6 runtime; hermetic selftest with system PATH fully scrubbed — `bundle --selftest passed` with zero Qt on PATH) → commit `e122ba0`
  - [x] Portable distribution: `packaging/package.ps1` assembles `dist/TrinityAI-1.0.0-win64/` (native/ + api/ + web/ + `Trinity.bat` launcher) and smoke-tests it end to end (`SMOKE OK: health ok; ui served; chat route ok; engine probe ok`), zips `TrinityAI-1.0.0-win64.zip` → commit `829ef6e`
  - [x] `packaging/smoke_bundle.py` committed; smoke gitignore narrowed (`packaging/smoke-data/`, `packaging/smoke.log`) → commit `5f666d2`
  - [x] Windows installer: Inno Setup 6 (`packaging/installer.iss` + `build_installer.ps1`; per-user `%LOCALAPPDATA%\Programs\TrinityAI`, Start Menu shortcut, silent-install + installed-build smoke verified) → commit `d458477`
  - [x] CI release workflow: `.github/workflows/release.yml` on `v*` tags (portable zip + installer + SHA256SUMS attached to the GitHub Release only after every smoke gate passes) → commit `48ff1cf`; README install-from-release section → commit `9048b58`; Stage 6 closed, 348 passed / 24 skipped, ruff clean

---

### Stage 7: Release Hardening (M10-M13 done)

- [x] **M10** — tag `v0.9.0` pipeline-test cycle: release run 9 green end-to-end (`a1479fe`), prove-as-published gate enforced (`M10 GATE PASS`), tag-repoint draft-flip fixed with post-upload re-publish
- [x] **M11** — OpenCascade `[cad]` in the PyInstaller bundle; real STEP export in the bundle smoke (runs 10-12 failing OCP collection root-caused to masked pwsh failures; runner Python pinned 3.12 inside `61a50b4`); run 13 proves the kernel as published
- [x] **M12** — code-signing harness, sign → smoke → compress → hash → publish order, 60-day renewal gate, self-signed decision documented (`dcd4dbd`)
- [x] **M13** - auto-update stack: updater core + gated routes (75bf618), `latest.yml` + as-published manifest gate (`12cfd04`), launcher repair (`66db7d5`), version stamp + swap-at-boot + frontend banner (`deb4882`); 10 updater tests; run 13 verifies `latest.yml` against the published zip
- [x] **M14** - delivered as Stage 8 below (one commit per op: families, nesting, drawings)

---

### Stage 8: M14 — CAD depth (done)

- [x] Parameter families - `build_family` (`ca61424`): every member is a full variant (own params, real validator verdict + verbatim refusal reason, geometry stats, content checksum over triangle soup + spec); unknown family parameter → 422 with `supported_parameters`; bound-violating members reported invalid, never dropped, never blamed on the family; POST `/api/cad/families`; 7 tests
- [x] Nesting / plate packing - shelf packer (`07e1d69`): measured XY footprints from real builders, deterministic order (longest side + stable tie-break), half-turn rotation for taller-than-wide parts, spacing rule with EITHER-axis separation semantics, published layout carries its own pairwise overlap+spacing proof (violation raises); bad parts land in `unplaced` with verbatim reasons; POST `/api/cad/nest`; 10 tests
- [x] Drawings / assembly export - multi-sheet DXF (`fe3e58f`): top/front/side/iso views from the mesh's full unique wireframe (zero-length view-axis projections dropped, true isometric with 120-degree axis separation), framed+labeled 1:1 sheets on per-view layers; emitted DXF recounted against projections before the op returns; box truth pinned (top/front/side=6, iso=18); POST `/api/cad/drawings`; 16 tests
- Suite at close: 375 passed / 40 skipped, ruff clean; catalog honesty rule held throughout - no op advertised until implemented + verified

---

### Post-M14: pipeline repair (CI + Browser E2E)

- [x] CI updater failure - `tests/test_updater.py::_fake_bundle` wrote the
  bundle root with `arcname=str(root)` (an absolute path), so Linux zips
  extracted a `tmp/pytest-.../TrinityAI-...` tree before the bundle and
  `apply_update` accepted whichever directory listed first; Windows only
  passed because NTFS happened to list the bundle first. The fixture now
  writes relative, forward-slash entries, and `_bundle_root` locates the
  tree by `native/api/web` layout instead of directory order (archive root
  accepted, stray top-level folders ignored). Regression tests cover a
  stray folder and a flat archive root. Suite 377 passed / 40 skipped,
  ruff clean.

- [x] Browser E2E cad suite - three suite bugs, red since it landed
  (`370dc31`) and never green: it asserted validation labels no CAD
  engine emits (now the real `dimensions_within_tolerance`,
  `all_vertices_finite`, `arm_length_positive`,
  `manufacturable_min_feature` checks), read the wireframe
  `aria-pressed` in the same tick as the click (now waits for the React
  re-render), and counted the `PROCESSING —` placeholder as the bot
  reply (now waits for the settled result). Full local run: 105
  assertions, 11 suites, 0 failed.

---

### Stage 9: M15 — agentic workflow depth

Ops in flight: plan contract → plan in the UI → DagRunPanel
productization → agent surface → model-attach docs → honesty pass.

- [x] M15.1 - plan contract (`/api/intelligence/stream-plan`). The model
  path is prompted for a JSON plan — the system prompt is built from the
  live registry roster, so it cannot name an engine that does not exist —
  the reply is parsed with the agent loop's balanced-JSON extractor, and
  validated for shape and workflow DAG semantics (reuses
  `src/workflows/dag.ordered`, so plan-time and execution-time dependency
  truth are the same code). The final SSE frame ships `plan.nodes[]` in
  POST /api/workflows/execute's exact `WorkflowNodeModel` shape; a route
  test executes a model plan unchanged through the workflow endpoint.
  Every bad reply (no JSON object / no nodes array / bad node shape /
  parameters not an object / duplicate ids / undeclared dependency /
  cycle / more than 32 nodes) ends the stream as an honest
  `{type:"error", code:"plan_invalid"}` with a verbatim reason; the
  previous model path that streamed prose and closed with a bare `final`
  no longer exists, and its old test now documents that contract change.
  The no-model deterministic fallback keeps `fallback`/`tool`/`job`
  backwards-compatible and now also carries its executed call as a
  runnable single node. Suite: 386 passed / 40 skipped, ruff clean.
  CI #185 + Browser E2E #23 verified green on `864579b` before M15.2
  started.

- [x] M15.2 - plan in the UI. `frontend/lib/api/trinity.ts` gains
  `streamPlan()` — a POST SSE client that forwards `delta` frames to an
  `onDelta` callback and resolves on the first `final`/`error` frame,
  filtering `plan.nodes` through a shape predicate so a malformed node
  never reaches the graph. `DagRunPanel` gains the PLAN affordance: input
  + button stream the request, raw deltas render in a scrolling `<pre>`
  while the model thinks, and the final frame's validated nodes load into
  the graph for an explicit EXECUTE — the plan is a proposal until the
  operator presses the button, exactly the pipeline's loaded-by-plan path.
  Source shown verbatim: "PLAN FROM MODEL — EDIT THEN EXECUTE", "PLAN
  FROM DETERMINISTIC ROUTER — NO MODEL ATTACHED" (the no-endpoint truth),
  or "PLAN REFUSED (code)" with the backend's verbatim reason. The panel's
  workflow POST moved onto the shared `TRINITY_API_URL` seam (bare `/api`
  only ever worked in the packaged same-origin mode). Two suite bugs fixed
  while landing the new `dag` suite: the plan client must not read a 200
  stream's body before streaming (`.json()` locks it — the fetch SUCCEEDED
  while the client threw "already locked to a reader"), and the `+ NODE`
  assertion may not read the React re-render in the click's tick.
  Verification: E2E #23 (M15.1) concluded success first; lint + tsc +
  production build clean; new dag suite 10/10; full browser run 115
  assertions, 12 suites, 0 failed. Files: frontend/lib/api/trinity.ts,
  frontend/components/trinity/DagRunPanel.tsx, frontend/e2e/suites/dag.mjs.

- [x] **Stage 5: engine feature depth — PlatformIO firmware, PCB trace DRC + Gerber, OpenCV contours/ORB, RK4 multi-body, robotics IK verification**
  Native: FirmwareEngine integrates PlatformIO (`platformIoSpecFor` maps ESP32/STM32/RP2040 to their platformio.ini boards; `runPlatformIoBuild` writes ini+source, runs the real `pio` binary, collects `.pio/build/*/firmware.{hex,bin,elf,uf2}` as `firmware_hex`/`firmware_binary`/`firmware_image` artifacts; truthful `capability_unavailable` when no toolchain exists — `test_firmware_platformio` drives a compiled fake `platformio.exe` including an env-switched failure). PCB gains `TrackSegment` in the IR plus geometric trace DRC (`trace.width`, `trace.clearance` via a public segment-distance seam, `trace.in_bounds`, `trace.keepout`, `trace.net_known`) and an `add_track` op, and RS-274X Gerber export (`GerberExport`, Mo=2/Nd=6, `D01` draws + `D03` aperture flashes, Edge.Cuts outline, per-layer self-verification before success — export now produces kicad + json + F_Cu + Edge_Cuts). Vision adds real `extract_contours` (Canny → `cv::findContours` with per-contour area/centroid/bbox and min-area filtering) and `match_features` (ORB `detectAndCompute` + Hamming knn + Lowe ratio test; honest zero keypoints/matches for flat images, refuses without OpenCV). Simulation adds a `method` field (`semi_implicit_euler` | `rk4`; RK4 integrates constant-force dynamics to machine precision — doctest-pinned against the closed-form trajectory) and a `multi_body`/`spring_chain`: bodies from an explicit `objects[]` array, coupled Hooke integration RK4 or Euler, body-0 samples, per-body `final_states` with kinetic/spring/total energy, energy conservation validated at 1%. Robotics verified: DLS IK already clamps every iterate to joint limits and reports `withinLimits` truthfully; doctest coverage existed. Python: `native_bridge.py` NATIVE_ENGINES/OP_MAP gained `pcb/add_track`, `image_contours`→`extract_contours`, `image_match_features`→`match_features`, and `simulate_spring_chain`, keeping the bidirectional advertised↔mapped pin green. Doctest counts after each commit: firmware 385, PCB 394, simulation 400, vision 404 (all green); Python 344 passed / 24 skipped; ruff clean.
  Commits: `760c317` firmware PlatformIO · `7b3408f` pcb trace DRC + Gerber · `541934d` simulation RK4 + spring chain · `f4d1e5f` vision contours + ORB · bridge/README commit (see git log).

- [x] **Stage 4: intelligence layer — real SSE streaming, multi-step agent loop, DAG execution + visualization**
  Native: `IHttpTransport::postStream(request, onChunk)` seam (default =
  post() + one whole-body chunk, so legacy fixture transports stay valid;
  DefaultHttpTransport streams the WinHTTP read loop incrementally), ChatWire
  body builders gain a `stream` flag (default false — byte-stable body tests),
  `SseDeltaParser` line-buffers chunks (safe mid-UTF-8 and mid-JSON splits),
  parses OpenAI `delta.content`/`tool_calls[].function.arguments` fragments AND
  Anthropic `text_delta`/`input_json_delta` on its own wire via a
  `usesAnthropicWire()` virtual, surfaces streamed error objects truthfully,
  and `HttpChatProvider::streamPlan` now streams incremental `(chunk,false)`
  deltas with exactly one `(chunk,true)` final payload; a server that ignores
  `stream:true` gets its whole-body JSON mapped honestly instead of fake
  deltas (380 native cases green). Python: `src/llm/local_client.py` is the
  keyless OpenAI-compatible seam to the user's own LLM (TRINITY_MODEL_ENDPOINT /
  TRINITY_MODEL_NAME; honest capability_unavailable without it);
  `POST /api/intelligence/stream-plan` forwards real token deltas as SSE with
  a deterministic-router fallback when no model is attached;
  `src/agents/loop.py` runs model→tool-call→engine→observation iterations
  (errors framed for self-correction, step budget reported as
  step_budget_exhausted), powering real `CoderAgent`/`ResearcherAgent.run()`
  behind `POST /api/agents/{coder|researcher}/run`;
  `POST /api/workflows/execute` executes multi-node DAGs with per-node
  visible status, failed nodes truthfully block dependents as `queued`, and
  `{node:<id>}.<path>` references thread upstream results into child
  parameters (live-tested: eval 2+3 → solve x+5=10 → failed pcb validate →
  blocked orphan). Frontend: `DagRunPanel` renders the pipeline with
  truthful status styling, mounted in TrinityWorkspace. Tests: 22 new
  local/loop/API tests scripted with fakes; Python suite 344 passed / 24
  skipped; native 380 cases. Everything degrades without a connected
  model — nothing pretends.

- [x] **M8: persistent native bridge — one hot daemon process, ~450x faster ops**
  `Trinity.exe` gains `--daemon` (app/native/src/cli/Exec.cpp `runDaemon` +
  `executeRequestJsonWithRegistry`, doctested in test_exec.cpp — ping/pong,
  sequential engine ops through one hot EngineRegistry, and recovery on
  malformed input; 377 native cases green). Python replaces the single-shot
  `subprocess.run --exec` per call (every request re-paid the Windows DLL/Qt
  bootstrap, ~110-440 ms/op) with a `NativeDaemon` client in
  src/engines/native_bridge.py: binary pipes (raw \n — a text-mode pipe would
  deliver "exit\r", which std::getline executes as an op instead of shutting
  the child down), one reader thread + queue so per-request deadlines work on
  Windows, a single lock serializing write->read so responses pair 1:1 with
  requests, automatic Qt/vcpkg PATH injection (TRINITY_QT_BIN override, C:/Qt
  scan, app-local exe dir), OpenCV stdout-noise tolerance with a line cap, a
  boot ping handshake, lazy respawn after a child crash, deadline kills for
  hung engines, atexit shutdown, and a daemon-lifetime private storage root
  whose reported artifacts are copied out and deleted per call. Daemon is the
  default; TRINITY_NATIVE_DAEMON=0 (or a failed handshake) falls back to the
  single-shot transport so an older binary still works. The statefulness
  probe held: bridged ops are fully parameterized (research/search reads an
  in-memory index only index_document — unbridged — can populate). Tests:
  tests/test_native_daemon.py (15 mocked: LF-only writes, PID stability,
  kill/timeout recovery, noise caps, workspace cleanup, singleton caching) +
  the daemon block in test_native_bridge_live.py (PID stable across 10 HTTP
  calls, kill→respawn, 8-thread multiplexing, benchmark asserted >=50x —
  measured 615-820x: single-shot ~113 ms/op vs daemon ~0.2 ms/op on this
  box). Python suite: 344 with the bridge, 322 without, both green.

- [x] **M4: CAD CSG booleans and faceted STEP export**
  CadEngine gains a BSP mesh boolean kernel (csg.js algorithm ported to C++,
  verified against the reference implementation on Node — identical
  polygon/edge signatures including the 16-tri/12-unpaired offset-cube
  signature on triangulated operands). `box()` winding corrected outward,
  `intersect` restored to the ~(~A|~B) double-inversion form, and
  `booleanMesh` heals T-junction seams BSP clipping leaves on triangle-soup
  operands (pure subdivision, volume preserved). Faceted STEP export
  (AP214 with planar faces sewn into shells) ships alongside STL/GLB/JSON.
  The composite-part boolean bug — where `csgSubtract(standoff, plate)`
  mixed true facet vertices with drifted interpolated crossings from the
  subtract flow's clip passes — was fixed by decomposing composite operands
  into single watertight shells via `unionMeshComponents` before clipping.
  Intent router and validator wired for the `boolean` operation; UI gains
  boolean op with part-A/part-B selection. Tests: direct CSG on cubes,
  cylinders, touching + offset cases with analytic volume checks and
  watertight closure; composite parts (standoff/mounting_plate) normalize
  into one shell with correct faceted-prism volumes; engine boolean runs
  with structured watertight/finite/volume checks; STEP golden and coordinate
  round-trip green. Native suite: 376 cases / 6,151 assertions, all green.
  Diagnostic dump file removed from tracking.

- [x] **M3: the math engine gained an honest symbolic layer (differentiate /
  simplify / solve_system), a matrix solve, and a Newton system solver**
  Built exactly as planned (AST beside the verified direct evaluator;
  hand-rolled symbolic MVP — no CAS dependency; honest refusals over faked
  results). What shipped in MathEngine (v0.4.0):
  - A grammar-mirror AST (`AstParser`, `serializeAst` with
    precedence-aware reparse-identical serialization) beside the untouched
    numeric hot path; constants/functions resolve at parse exactly like the
    direct Parser, free symbols are the point and resolve at eval.
  - `differentiate`: sum/product/quotient/chain/power rules, sin/cos/tan,
    exp, ln/log, sqrt, asin/acos/atan; `abs()` (piecewise) and
    constant^variable with base <= 0 are structured RequestValidationError
    refusals stated up front. Every single-symbol result is verified against
    a central difference at a deterministic probe (x=1.7, or the requested
    `at` point) and stamped as `derivative_check` in metadata; skipped —
    and labeled skipped — when the expression is not finite at the probe.
  - `simplify`: constant folding + identities (x+0, 0-x, x*0, x*1, x/1,
    x^0, x^1, x-x) iterated to a serialization fixpoint; 1/0, negative-base
    fractional powers and 0^0 deliberately stay unfolded (eval refusals,
    not silently "fixed").
  - `solve_linear` matrix form (A/b arrays, up to 16 unknowns) via Gaussian
    elimination with partial pivoting; singular is a defined success outcome
    (`outcome: "singular"`, honest message), not a crash; solutions are
    per-row residual-verified into `equation_checks`.
  - `solve_system` Newton (1..8 equations, 200-step budget) with a numeric
    centered-difference Jacobian and per-equation residual verification;
    contradictory (singular Jacobian) and overspecified systems fail with
    structured errors — never a faked convergence; honest non-convergence
    carries the final max residual.
  - Intent art end to end: whitelist + per-op param rules in
    IntentValidator, router pass-through for the expression ops,
    RequirementParser phrases ("Differentiate x^2 with respect to t",
    "Derivative of …", "Simplify …", "Solve system e1; e2 with x = 1.4,
    y = 1"), two new MainWindow combo ops with per-op hints, and explicit
    validate() branches per new op (the previous generic branch demanded a
    numeric `value` and would have invalidated every symbolic result).
  - Tests: +6 cases / +129 assertions — derivative round-trips vs central
    differences at three probes, exact simplify strings incl. the honest
    non-folds, matrix solve/singular/ragged/mismatch, circle+line Newton
    (converges to sqrt(2) on both axes), contradictory/unbound/
    overspecified refusals, validator whitelist coverage, parser phrase
    coverage, and a pipeline e2e for "Differentiate …"/"Solve system …".
    Native suite: 371 cases / 6,082 assertions, all green; ctest preset
    green; full build (core + tests + Qt desktop app) clean.

- [x] **M6: the vector store seam ships a working local store (refusal Gone)**
  `src/utils/vector_store.py` used to raise NotImplementedError on add/query;
  the seam now carries a real implementation with zero new dependencies:
  deterministic hashed n-gram embeddings (FNV-1a bucketing, words + 3-grams,
  L2-normalized, 256 dims by default), cosine ranking, and persistence in a
  private SQLite file (vectors.db under settings.embeddings_dir) with
  insert-or-replace rows carrying JSON metadata. API: add / add_text /
  query / search / delete / clear / count / close, plus module-level embed
  and cosine. Honest scope is stated in the docstring: it is a utility on
  top of (not a replacement for) SQLite lineage; embeddings are lexical, not
  a learned model; no network, no API keys; cross-source vectors may be
  stored but mixing dims stays the caller's responsibility; degenerate zero
  vectors score 0.0 rather than throwing. Tests: new test_vector_store.py
  (13 cases: determinism, normalization, empty-text zero vector, tiny-dim
  rejection, cosine bounds incl. degenerates, exact-match-first ranking,
  metadata round trip + upsert, delete/count/clear, empty-store query, k
  bounding + descending scores, bad-input refusals, mixed explicit/text
  vectors, persistence across reopen — with one fix: the test asserting
  score exactly 1.0 on a non-identical query text was wrong, cosine 0.82 was
  the correct answer for "quadcopter frame CAD" vs the stored
  "...generation"). test_new_modules.test_utils once asserted the refusal;
  it now asserts the default-path contract while functional behavior lives
  in the dedicated file. Full Python suite: 307 passed (+13) / 20 skipped
  (the settled no-key/no-exe gates), no regressions.

- [x] **M7: "restore Python engines" closed as a plan correction, not code**
  The milestone plan said to restore RoboticsEngine/SimulationEngine/
  ResearchEngine from git history (fc40fec / cebb8c1 / f2395ca) behind a
  TRINITY_PYTHON_ENGINES gate. The premise was wrong, and the history
  proves it: those commits are NATIVE (C++) engine feature commits —
  there was never a Python class of any of those names at any point in the
  repo's full history (checked with git log -S over *.py: zero hits). The
  Python side already implements the designed state: native_bridge routes
  those engines to Trinity.exe --exec when the exe exists, ScaffoldEngine
  refuses truthfully with status="scaffolded" when it does not, and the
  registry prefers native. Restoring "Python engines from history" would
  have reinvented what shipped natively and violated the settled
  bridge-over-duplication decision. No code change; the work log is the
  record this milestone leaves behind, and the remaining genuinely-open
  Python seams (persistent bridge, packaging) are M8/M9.

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