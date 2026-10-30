# Trinity AI — Process Record

How this project was built: the working rules, the decisions that were
settled on purpose, the lessons that cost a debugging session, and the
verification chain every change goes through. For *what* was built, see
[MILESTONES.md](MILESTONES.md).

---

## The working rules

These are enforced by the repo's own docs (`opencodeworks.md`) and by
habit — they are why the test counts only ever climb:

1. **One concern per commit.** A commit is one milestone step, one engine,
   one fix. Not one session.
2. **The worklog line is ticked in the same commit** as the change it
   describes — never on its own. `work.md` (task tracker) and
   `opencodeworks.md` (work log) are shared, versioned, and ticked as work
   lands. This is also how the next person (human or agent) knows what is
   actually done.
3. **Never fake capabilities.** If an engine, artifact, or feature is not
   implemented, the system answers with a structured, truthful refusal
   (`capability_unavailable`, `status="scaffolded"`) — never a plausible
   lie. This rule shaped the engine registry, the catalog, the launcher's
   health check, and every bundle below. Phantom capabilities
   (`pcb/inspect`, `vision/ocr`, `vision/document_parse`,
   `vision/geometry_extract`) were *removed* rather than implemented when
   the roadmap's verification-first mindset caught them.
4. **Everything is verified against reality, in the direction that matters.**
   Not "the YAML parses" but "the tests *executed* in CI" (a skip goes
   green too). Not "the bundle should work" but "the bundled exe passed
   `--selftest` with no Qt on PATH". Not "CI will pass" but "the workflow
   ran on a real tag and the artifacts attached".
5. **A wrong premise gets corrected, not implemented.** When M7's plan said
   "restore Python engines from git history", the history check
   (`git log -S`) proved no such Python classes ever existed. The response
   was a plan correction with evidence, not resurrecting something that
   never was.

---

## Settled decisions — recorded so they stop being re-opened

| Decision | What | Why |
|---|---|---|
| Thin desktop shell | Keep the pywebview window (`desktop.py`); no native titlebar rewrite | `start.ps1` / `run.bat` already cover the need; a native titlebar would triple the build surface for no functional gain |
| Bundle UI = static export served by the API | `TRINITY_STATIC_EXPORT=1` → Next static export; FastAPI serves it at `/` | One Python process, no Node and no second port in the shipped bundle; the UI's same-origin `/api` base just works |
| Static export is opt-in, not the default | `next.config.ts` switches on the env var only | `next start` (the dev flow) cannot serve an exported build; both modes stay functional |
| Ship portable zip **and** installer | `TrinityAI-<v>-win64.zip` + `TrinityAI-<v>-win64-setup.exe` from the same staged directory | Installer for normal users, zip for portable / no-install; identical contents by construction |
| Inno Setup, per-user, no admin | Installs to `%LOCALAPPDATA%\Programs\TrinityAI` | Lowest friction; no UAC prompt |
| No code signing is enforced-by-default (M12 harness shipped) | Binaries ship unsigned until a certificate secret is configured; when `TRINITY_SIGN_PFX` (+ `..._PASSWORD`, `..._TARGET=base64`) exists in the repo secrets, `Trinity.exe` and `setup.exe` are signed before hashing/compression | Harness (`packaging/sign_files.ps1`) works with a PFX or a zero-config self-signed code-signing cert (dev distribution); committing unsigned while the cert decision is pending is honest, and SmartScreen friction is a known, documented cost |
| Bridge over duplication | Engines live in C++; Python routes to the native engine, never reimplements | One implementation, one place to verify; enshrined by M7's plan correction |
| LLM transport tests stay skipped | No API keys on dev/CI machines | Documented refusal — the transports have offline contract tests via fixture transports instead |

### M12 signing policy (settled)

- **Order of operations:** sign → smoke → compress → hash → publish. The
  SHA-256 manifest always describes the bytes a user downloads.
- **Renewal rule:** a signing cert inside its final **60 days** fails the
  build (`sign_files.ps1` asserts `NotAfter`); renew first, then release.
  Direction chosen instead of "ignore expiry" because a dead cert is worse
  than no cert.
- **Cert type (settled 2026-10-29):** self-signed for the current stage -
  zero cost, zero CI secret plumbing, keeps the harness and order-of-
  operations correct. Upgrade path when distribution goes public: Azure
  Trusted Signing (first choice) or an EV/OV cert; only the credential
  source changes, the pipeline stays as built.

### M13 auto-update policy (settled)

- **Checks never download.** `GET /api/update/check` answers availability
  (+ version/size) or an honest reason - offline, missing `latest.yml`,
  already up to date - and performs zero file transfers beyond the two
  documents it must read.
- **Apply is explicit and idempotent-safe.** `POST /api/update/apply`
  refuses without `{"confirm": true}`; then: download once → sha256 must
  match `latest.yml` → extracted tree must actually be a Trinity bundle
  (`native/api/web`) → staged under
  `%LOCALAPPDATA%\TrinityAI\data\update-pending` with a `swap.json`
  marker. Anything failing refuses with a reason and leaves nothing
  half-installed.
- **The swap happens at next boot, not in-process.** Windows cannot
  replace a running exe, so `Trinity.bat` runs `swap_pending.ps1`
  before the API boots: previous component dirs are kept as `.prev`
  backups, the staged tree takes over, the marker is cleared, user data
  (`TRINITY_STORAGE_ROOT`, outside the swap set) is never touched.
- **`latest.yml` schema is project-defined** (`version / url / sha256 /
  size`, pointing at the public download URL) and the release
  verify-as-published gate downloads it back and proves every field
  against the actually published zip - a stale or wrong manifest fails
  the release instead of poisoning clients.

---

## Hard-won lessons (the debugging notes worth keeping)

Lessons learned on this exact codebase, in the exact situation they
broke. Kept because the next session will hit them again:

### Windows / PowerShell 5.1
- **Scripts must be pure ASCII.** PS 5.1 parses ANSI, not UTF-8 — an em
  dash inside a string is a parser error that shows up as "missing
  terminator" at a *later* line. Prefer `-f` formatting over interpolating
  into thrown strings.
- **PS 5.1 has no ternary operator.** `x ? a : b` is a parse error; use
  `if/else`.
- **`$ErrorActionPreference = "Stop"` + `2>&1` turns native stderr into
  terminating errors.** Tools that write *warnings* to stderr
  (windeployqt's missing-dxcompiler message is the recurring one) abort
  the script. The pattern: wrap native invocations in
  `$ErrorActionPreference = "Continue"` inside try/finally, and verify
  success **by artifact existence** (`Test-Path` the output), never by
  parsing the log.
- **`Get-ChildItem ... | Copy-Item` breaks for items outside CWD** —
  `Copy-Item` resolves FileInfo relative to the process CWD. Use
  `Copy-Item (Join-Path $dir "*.dll") $dest` instead of piping objects.
- **`Join-Path` takes exactly two arguments.** Nested path math breaks;
  build paths step by step, and `GetFullPath` once, absolutely, before
  any tool that Push-Locations changes the CWD underneath you.
- **Stale processes hold file locks during zipping.** A leftover
  `trinity-api.exe` from a failed run makes `Compress-Archive` fail with
  "file in use". `Stop-Process -Name trinity-api -Force` before archiving;
  `smoke_bundle.py` terminates its child explicitly in `finally`.

### Backend (Starlette / FastAPI / PyInstaller)
- **`StaticFiles(html=True)` returns a 404 `FileResponse` — it does not
  raise** — when a `404.html` exists in the directory (Next.js exports
  one). Any "catch the exception and retry" logic never fires. The
  `_StaticHtml` subclass checks `response.status_code == 404` and retries
  with a `.html` suffix, taking the retry only when it returns 200 — so a
  genuinely unknown path still answers the real 404 page.
- **Freeze the entry with a direct import.** `uvicorn.run("src.main:app")`
  relies on the import-string path resolving at runtime inside the frozen
  importer; `uvicorn.run(src.main.app)` with `import src.main` at module
  top is what PyInstaller sees.
- **PyInstaller data collection is path-contract, not magic.** The spec
  ships `config/` into `_internal/config/` so frozen `model_config`
  resolution (`REPO_ROOT / "config"`) keeps working unchanged — no
  repack, no symlinks.
- **Frozen startup is slow (~10–30 s first boot).** Poll health with
  generous iterations; a 10-second timeout is a false failure.
- **Import-time environment means reload-based tests.** `TRINITY_UI_DIR`
  is read at module import; the UI-serving tests `importlib.reload`
  (importing `src.main` *inside* the test) and restore a mount-free
  reload at teardown so neighbor tests keep their cached mount-free app.

### Native / C++ / CMake
- **`find_package(OpenCV QUIET)` does not fail on a miss.** A silent
  "not found" produced a degraded binary that Configure reported as
  green. `TRINITY_REQUIRE_OPENCV=ON` turns the miss into a Configure
  `FATAL_ERROR` — the package preset for release bundles sets it.
- **doctest `static_assert`s on disabled exceptions** — that is how the
  missing `/EHsc` (which VS injects silently, Ninja does not) surfaced as
  a compile error instead of a runtime UB.
- **nlohmann: `Json{"literal"}` is an array** (braced-init disambiguation).
  Strings need an explicit `std::string` constructor. Found because the
  system prompt shipped as a one-element JSON array.
- **Binary pipes, not text pipes**, for the daemon wire: a text-mode pipe
  delivers `"exit\r"`, and `std::getline` executes it as an op instead of
  shutting the child down.
- **Binary dir layouts vary.** vcpkg manifest mode installs into
  `<build>/vcpkg_installed/<triplet>/bin`, which is *not* necessarily
  sibling of the exe — bundle scripts should verify artifact existence
  and refuse loudly ("the packaged vision engine would be a lie").

### Frontend / Next.js 16
- **Read the bundled docs, not the training data.** The repo carries an
  AGENTS.md note that Next 16 may differ; `output: 'export'` was
  confirmed valid in `node_modules/next/dist/docs/.../static-exports.md`
  before any config was written.
- **Every page must be export-client compatible** — no `next/image`, no
  SSR-only features. All 12 routes were verified before enabling export.
- **`NEXT_PUBLIC_*` env vars are inlined at build time.** The packaged
  build sets `NEXT_PUBLIC_TRINITY_API_URL=/api` before `npm run build`;
  the same codebase builds with the default `http://localhost:8000/api`
  for dev.

### CI (GitHub Actions)
- **A job cannot opt out of its workflow's cancellation policy.** The
  first attempt to protect the long e2e suite set job-level
  `concurrency` with `cancel-in-progress: false` — the run still came
  back cancelled; workflow-level policy governs. The fix was splitting
  the file (e2e into its own workflow with `cancel-in-progress: false`
  at workflow level).
- **Assert the tests executed, not just that the job is green.** The
  `cad-kernel` job would have hidden a full skip behind a green exit; it
  now parses the log for executed tests and fails otherwise.
- **Cache keys need explicit invalidation.** The vcpkg cache took a `v2`
  prefix bump when its entry set was no longer trustworthy, and the
  unconditional fallback restore was removed — a fallback hit is exactly
  where an incompatible older entry sneaks in.

---

## The verification chain

Every landed change climbs this ladder; the release bundle climbs all of
it plus the last two rungs:

1. **Native doctests** — `ctest --preset ...` then the bare test binary
   (404 cases / 6,331 assertions at Stage 6 close). Doctests
   self-verify: they generate inputs, compute expected values, and pin
   numbers, so they are the ones that caught missing `/EHsc` and the
   registry's false claims.
2. **Python suite** — `python -m pytest` (348 passed / 24 skipped; the 24
   are the settled skip gates: no API keys, no native exe in that
   checkout). Ruff clean after.
3. **Doctest binary standalone** — asserts the suite passes *outside
   ctest*, catching environment differences.
4. **Native engine selftest** — `Trinity.exe --selftest` runs the whole
   bootstrap headlessly (no QApplication) and exercises every engine.
5. **Frozen bundle smoke** — `packaging/smoke_bundle.py` starts the
   packaged `trinity-api.exe` and asserts, in order: `/api/health`
   answers, `/` serves the static UI, `/chat` resolves through the
   `.html` retry, and a real engine execution answers through the bundled
   native bridge. Exit 0 only when every probe really answered.
6. **Hermetic native bundle check** — `bundle_native.ps1` runs the same
   `--selftest` with the system PATH scrubbed to an empty temp directory,
   so the bundled DLLs are the only option.
7. **Installed-build smoke** — `build_installer.ps1` silent-installs the
   Inno Setup exe into a scratch directory and runs the bundle smoke
   against the *installed* copy, not the staging directory.

The repo's own history is the proof the chain matters: the launcher
health bug (process up, engines dead), the `Verify-OpenCV` QUIET miss,
the `cad-kernel` skip-goes-green trap, and the phantom capabilities all
fell to this discipline — each was a plausible-looking pass until
somebody checked.

---

## The tracking files

- **`work.md`** — the task tracker. Numbered items, ticked in the commit
  that lands them. If it is unticked, it is not done.
- **`opencodeworks.md`** — the narrative work log. Milestone entries with
  the what-and-why, settled decisions, product bugs, and the baseline
  test numbers. Shared with human and agent contributors alike.
- **This directory** (`docs/`) — the durable documentation distilled from
  the above: [MILESTONES.md](MILESTONES.md), this file, and
  [ROADMAP.md](ROADMAP.md).
