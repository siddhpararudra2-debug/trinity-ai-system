# work.md — Trinity AI: Remaining Tasks

## Status: Stage 5 (Engine Feature Depth) — IN PROGRESS

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

### Stage 6: M9 — Packaging & Standalone Distribution (in progress)
14. [x] Qt standalone bundle: `windeployqt` + OpenCV DLLs (`packaging/bundle_native.ps1`, hermetic PATH-scrubbed `--selftest`) → commit `e122ba0`; native engine also wired into the portable dist
15. [x] Python backend bundling: PyInstaller onedir FastAPI bundle serving the Next.js static export (`packaging/trinity-api.spec`, `TRINITY_UI_DIR` mount, frozen smoke green) → commits `8edfc04`/`3ac8f21`/`829ef6e`; portable zip assembled + smoke-verified
16. [x] Windows installer: Inno Setup 6 (`packaging/installer.iss`, per-user `%LOCALAPPDATA%\Programs\TrinityAI`, Start Menu shortcut; `build_installer.ps1` silent-installs into a scratch dir and runs the same bundle smoke against the *installed* copy — `SMOKE OK` before the setup exe counts) → commit `d458477`
17. [x] CI: `.github/workflows/release.yml` on `v*` tags — release-vcpkg build with doctests + `--selftest`, frontend static export, PyInstaller, windeployqt, portable zip, installer, SHA-256 manifest, GitHub Release upload → commit `48ff1cf`
