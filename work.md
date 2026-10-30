# work.md — Trinity AI: Remaining Tasks

## Status: Stage 6.5 (documentation) done — Stage 7 (Release Hardening + CAD Depth) queued

### Done earlier (Stage 4, pushed to origin/main)
- M8 daemon + NativeDaemon bridge, C++ SSE streaming, Python agent loop/workflow execution, DagRunPanel
- 380/380 native doctests green, 344 passed / 24 skipped Python, ruff clean

### Done: Firmware PlatformIO (commit 1 of Stage 5 — `760c317`)
1. [x] Fix `test_firmware_platformio.cpp` — two `_WIN32` TEST_CASEs call `FakeToolchainDir` with the old two-arg `(suffix, script)` signature → single-arg ctor (fake exe copied to toolchain dir)
2. [x] Fake ToolLink: `tests/fake_platformio.cpp` → CMake target `fake_platformio` copies to `platformio.exe` in a scratch toolchain dir, env var `TRINITY_FAKE_PIO_FAIL` switches the failure test (batch-file/cmd.exe quoting is broken — `.cmd` were reverted to `.exe`-only in `findToolchain` on purpose)
3. [x] Remove debug `std::cerr << "build result: ..."` line
4. [x] `pendingArtifacts` pair types verified by the kind-to-extension test (firmware_hex / firmware_binary / firmware_image)
5. [x] `runPlatformIoBuild` reviewed: unknown-MCU early return, source path refusal (non-relative/escaping refused), artifact glob over `.pio/build/*/firmware.*`, artifact ordering sorted deterministically
6. [x] 385/385 native doctests green; 344 passed / 24 skipped Python; ruff clean
7. [x] Committed `760c317 firmware: genuine PlatformIO builds with real .hex/.bin artifacts` (worklog bullet ticked separately once PCB lands since the board file opencodeworks.md Stage 5 bullet stays one line until the full stage closes)

### PCB engine depth (commit 2 of Stage 5)

### Stage 5 remaining (one commit per engine)
8. [x] PCB — `TrackSegment` in the IR (round-trips JSON), trace DRC (`trace.width` / `trace.clearance` / `trace.in_bounds` / `trace.keepout` / `trace.net_known` in Validators.cpp, public `trackSegmentDistanceMm` testing seam), `add_track` op, and RS-274X Gerber export (`GerberExport.cpp/hpp`, Mo=2/Nd=6, D01 draws + D03 flashes, Edge.Cuts file, self-verifying) → commit `7b3408f`, 394/394 native green
9. [x] Simulation — `method: "rk4"` option (exact for constant-force dynamics, verified against the closed-form trajectory while semi-implicit Euler deviates >1e-4) + multi-body `spring_chain` (bodies from `objects[]`, Hooke RK4/Euler coupled integration, energy summary KE+PE+total, energy-conservation validated at 1%) → commit `541934d`, 400/400 native green
10. [x] Vision — `extract_contours` (Canny → `cv::findContours`, per-contour area/centroid/bbox, min-area filter) + `match_features` (ORB detectAndCompute + Hamming knn + Lowe ratio test, honest zero when no keypoints, no OpenCV build refuses truthfully); doctests generate shapes/textured PNGs in-test → commit `f4d1e5f`, 404/404 native green
11. [x] Robotics — verification only: DLS IK + joint-limit clamping/`withinLimits` already implemented (`Kinematics.cpp:225+`); `test_robotics_ik` already covers "iterates stay clamped to joint limits" — no changes needed
12. [x] Python bridge — `native_bridge.py` NATIVE_ENGINES + OP_MAP gained `pcb/add_track`, `image_contours`→`extract_contours`, `image_match_features`→`match_features`, `simulate_spring_chain`; bidirectional map test green; pytest 344 passed / 24 skipped; ruff clean
13. [x] README + `opencodeworks.md` Stage 5 entry ticked with each engine commit

### Stage 6: M9 — Packaging & Standalone Distribution (done)
14. [x] Qt standalone bundle: `windeployqt` + OpenCV DLLs (`packaging/bundle_native.ps1`, hermetic PATH-scrubbed `--selftest`) → commit `e122ba0`; native engine also wired into the portable dist
15. [x] Python backend bundling: PyInstaller onedir FastAPI bundle serving the Next.js static export (`packaging/trinity-api.spec`, `TRINITY_UI_DIR` mount, frozen smoke green) → commits `8edfc04`/`3ac8f21`/`829ef6e`; portable zip assembled + smoke-verified
16. [x] Windows installer: Inno Setup 6 (`packaging/installer.iss`, per-user `%LOCALAPPDATA%\Programs\TrinityAI`, Start Menu shortcut; `build_installer.ps1` silent-installs into a scratch dir and runs the same bundle smoke against the *installed* copy — `SMOKE OK` before the setup exe counts) → commit `d458477`
17. [x] CI: `.github/workflows/release.yml` on `v*` tags — release-vcpkg build with doctests + `--selftest`, frontend static export, PyInstaller, windeployqt, portable zip, installer, SHA-256 manifest, GitHub Release upload → commit `48ff1cf`

### Stage 6.5: Project documentation (done)
18. [x] `docs/MILESTONES.md` — full history baseline→M9, hashes verified against git log → commit `fb35272`
19. [x] `docs/PROCESS.md` — working rules, settled decisions, build lessons, verification chain → commit `26edaa4`
20. [x] `docs/ROADMAP.md` — Stage 7: M10–M14 → commit `4f940c1`
21. [x] README documentation cross-links section

### Stage 7: Release Hardening + Engine/CAD Depth (queued — see docs/ROADMAP.md)
22. [x] M10 - tag `v0.9.0` (pipeline-test tag per agreed policy, not `v1.0.0`); release workflow green end-to-end (run 9, `a1479fe`): native build, doctests, bundle smoke, Inno Setup installer, SHA-256 manifest, publish, then **prove-as-published gate** — assets downloaded back from the public Release page, SHA256SUMS re-verified, full smoke run against the unzipped published bundle. Fixed along the way: Linux-portability of the daemon spawn test (runs 152-160), Inno Setup runner install (`76ef109`), windeployqt resolution (runs 1-5), pwsh annotation reporting, tag-delete-leaves-draft-release 404s
23. [x] M11 - OpenCascade `[cad]` extra carried into the PyInstaller bundle; bundle smoke executes a real STEP export. Spec hiddenimports (`549b0db`), kernel reason plumbing via `_import_cause` + collection-count assert (`023042a`), PyInstaller pinned `6.22.3` with build-env identity check + captured `pyi-build.log` (`92c7890`); runs 10-12 red with runner OCP `0 pyd, 0 TK dll` root-caused to silent native-command failures on the runner's newest preinstalled Python - cured by `actions/setup-python` pin 3.12 + strict deps-step gates + hard-fail identity check (`61a50b4`); **run 13 green as published** (kernel catalog ok + step export ok, volume_mm3=1847.5, opencascade 7.9.3.1)
24. [x] M12 — code signing for `Trinity.exe` + `setup.exe` (cert type + renewal policy decided up front); manifest generated after signing → `dcd4dbd`: `sign_files.ps1` (PFX or zero-config self-signed, 60-day expiry gate), signing wired into `package.ps1` + `build_installer.ps1` before hashing, honest "unsigned build" skip without the secret. Cert decision recorded (self-signed now; Azure Trusted Signing/EV when distribution goes public). Local proof: both artifacts signed (dev cert) + full local package run green; runner runs print the honest skip
25. [x] M13 — auto-update: `latest.yml` manifest per release, version/availability endpoint, user-initiated verified download + restart, data preserved. Core `updater.py` (check never downloads; apply = sha256 + layout gates → staged tree + `swap.json` marker; swap at next launcher boot because Windows cannot replace a running exe; storage root outside the swap set) → `75bf618`; `latest.yml` generation + verify-as-published manifest gate in `release.yml` → `12cfd04`; launcher-block repair → `66db7d5`; version stamp + swap script + update-route smoke ride in package.ps1; frontend banner with explicit consent + honest restart message → `deb4882`; 10 updater tests, full suite 342 passed / 40 skipped, ruff clean; **run 13 green: latest.yml matched the published zip (version/sha256/size/url)**
26. [ ] M14 — CAD depth, one commit per op: nesting/plate packing, drawings/assembly export, parameter families (catalog honesty rule applies)
    - [x] Nesting / plate packing — shelf packer (`src/engines/cad/nesting.py`): measured XY footprints from real builders, deterministic order (longest side, stable tie-break), optional half-turn rotation for taller-than-wide parts, spacing rule between placements, pairwise overlap + spacing proof carried in the result (violation raises, never a lying layout); oversized/bad parts listed `unplaced` with real reasons, `quantity` expansion, fill ratio; POST /api/cad/nest; 10 tests - spacing semantics corrected mid-proof: parts side by side share their y-range, so EITHER-axis separation satisfies the rule (suite 359 passed / 40 skipped)
    - [x] Parameter families — `build_family` (`src/engines/cad/families.py`): every member is a full variant (own params, real validator verdict + verbatim refusal reason, geometry stats, content checksum over triangle soup + spec); unknown family parameter → 422 with `supported_parameters`; bound-violating members reported invalid, never dropped → `ca61424` (suite 349 passed / 40 skipped)
