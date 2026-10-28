# Trinity AI — Roadmap: Stage 7

Status: M1–M9 (Stages 1–6) shipped — see [MILESTONES.md](MILESTONES.md).
Stage 7 combines **release hardening** with **engine & CAD depth**, in the
order below. Each milestone is a self-contained, verifiable unit following
the rules in [PROCESS.md](PROCESS.md): one concern per commit, worklog
ticked in the same commit, and no capability shipped until it is actually
verified working.

Deliberately deferred (not in Stage 7): macOS/Linux packaging, new engine
types beyond CAD depth, schema/migration of existing user data.

---

## M10 — Tag `v1.0.0` and prove the release pipeline

**Goal:** the release workflow (`.github/workflows/release.yml`) has never
run on a real tag. Its first real run is the gate.

**Steps:**
1. Tag `v1.0.0` on `main` and push the tag.
2. Watch the workflow end to end: native release-vcpkg build with
   doctests + `--selftest`, frontend static export, PyInstaller,
   windeployqt, portable zip, Inno Setup installer (with its installed-
   build smoke), SHA-256 manifest, artifacts attached to the GitHub
   Release.
3. Download the artifacts from the Release page and run
   `packaging/smoke_bundle.py` against the downloaded zip — verifying the
   artifact *as published*, not as freshly staged.
4. Fix whatever the first real run trips over; iterate until the release
   is live. Tag spots may be burned (`v1.0.1`, `v1.0.2`) if the workflow
   itself needs fixing — that is fine and gets recorded.

**Verified when:** the GitHub Release page shows the zip, the setup exe,
and `SHA256SUMS.txt`, and the downloaded zip passes the bundle smoke.

## M11 — STEP kernel inside the bundle

**Goal:** the packaged catalog truthfully reports `step` as unavailable
today — the OpenCascade `[cad]` extra is not carried into the PyInstaller
bundle. Carry it in, and make the pre-release smoke prove `step`
genuinely works.

**Steps:**
1. Install the `cad` extra (OpenCascade/NLohmann dependency via
   `pip install ".[cad]"`) on the packaging machine / CI runner.
2. Extend `packaging/trinity-api.spec` so the OpenCascade package (and
   any native DLLs it loads) reaches `_internal/`.
3. Extend the bundle smoke to execute a real STEP export through the
   bundled API — an artifact the user can open in a CAD tool.
4. The catalog flips to reporting STEP as available **only when the
   bundled build actually answers** — verified, not configured.

**Verified when:** the portable-dist smoke imports a STEP file it
exported itself, and the catalog marks `step` available with real
artifacts.

## M12 — Code signing

**Goal:** reduce SmartScreen / UAC friction on `setup.exe` and
`Trinity.exe`.

**Steps:**
1. Decide certificate type (self-signed for test distribution vs. an
   EV/OV code-signing cert for public distribution) and the renewal
   policy up front — signing with a cert that dies in 12 months is
   worse than not signing.
2. Sign `Trinity.exe`, every shipped DLL where feasible, and
   `setup.exe` as the last step of packaging (signing before
   compression keeps hashes stable for the manifest).
3. `SHA256SUMS.txt` generation moves *after* signing so the manifest
   describes the shipped bytes.
4. Record in the README what users should expect from SmartScreen with
   each cert type.

**Verified when:** `Get-AuthenticodeSignature` on the shipped setup exe
returns a valid signature, and the release manifest still matches the
published files.

## M13 — Auto-update

**Goal:** an installed copy can check for a new release and upgrade
itself, without silent force-upgrades.

**Steps:**
1. Ship a `latest.yml`-style manifest with each GitHub Release (version,
   asset URLs, SHA-256 per asset).
2. The bundled API exposes a version + update-availability endpoint
   backed by that manifest.
3. The UI surfaces "update available" with a user-initiated install
   (download with SHA-256 verification → run the new setup exe → app
   restarts). No background force-restart.
4. Uninstall/reinstall data-preservation is verified: `%LOCALAPPDATA%`
   data survives an update in both portable and installed layouts.

**Verified when:** an installed v1.0.0 discovers a v1.0.1 manifest,
verifies its hash, upgrades, and keeps its storage intact.

## M14 — More CAD depth

**Goal:** extend the verified CAD engine with catalog-driven operations,
in the same style as M3/M4: each shipped with native doctests pinning
geometric truth before it is advertised.

**Candidate operations (each becomes its own commit):**
- **Nesting / plate packing** — fit part outlines on a stock rectangle,
  with rotation and spacing rules, verified by pairwise overlap checks.
- **Drawings / assembly export** — multi-sheet DXF or PDF projections
  from solids (front/top/iso views), verified by geometry counts.
- **Parameter families** — generate a table of variants from one
  parameter model, each family member a full artifact set with its own
  checksum.

Only operations that close the loop (artifact → re-parse → verified
geometry) get advertised. The catalog's honesty rule from M4 applies:
anything that cannot verify its output stays a truthful refusal.

---

## Milestone order and why

1. **M10 first, always** — every later milestone releases through the
   pipeline; proving the pipeline works on a real tag de-risks
   everything else. It also burns in the release cadence.
2. **M11 before M12/M13** — the STEP kernel changes what the bundle
   *contains*; signing and update-manifests should describe the final
   bundle shape, not be re-published after every bundling change.
3. **M13 after M12** — an updater downloading signed, hash-verified
   artifacts is the trustworthy form; the reverse order would ship an
   updater for unsigned artifacts.
4. **M14 runs concurrent to any of the above** — CAD ops are orthogonal
   to packaging and can land between release milestones.

Each milestone gets its own `work.md` section as its commits start
landing, following the tracking-file conventions in `opencodeworks.md`.
