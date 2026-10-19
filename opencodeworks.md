# OpenCode work log

Shared, versioned, and ticked as work lands. Codex works in this repo too, so
this is the one place both of us should look before starting.

Commits are one concern at a time. A line is ticked in the same commit as the
change it describes — never on its own.

---

## In progress

- [ ] **Step 3 — Prove the desktop shell's window**
  `desktop.py --no-window` is verified end to end. The pywebview path has
  never opened a window, so its shutdown has never been observed. Needs
  `--window-timeout N` so the window closes itself and shutdown becomes a
  tested claim rather than an asserted one.

---

## Pending — cannot be completed here

- [ ] **Step 4 — First real CI run of the e2e job**
  Not self-verifiable. It completes when a runner executes the job and the
  result comes back green, not when the YAML parses or the steps reproduce
  locally. This stays unticked until someone has seen the actual run.

  The suite that found 6 of the 8 product bugs this session has never run
  unattended. Calling it done without evidence would be the same error as the
  engine registry labelling working engines `501`.

---

## Open decision — not work

- [ ] **Step 5 — Desktop shell: thin vs native**
  Currently the thin pywebview window, chosen because it is cheap and
  reversible. A Tauri/Electron shell is a different product: new toolchain,
  packaging pipeline, roughly triple the build surface. It deserves its own
  plan rather than a footnote here.

---

## Done

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