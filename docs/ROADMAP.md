# Trinity AI — Roadmap

Status: everything planned through Stage 9 has shipped — M1–M9
(Stages 1–6), M10–M13 release hardening (Stage 7), M14 CAD depth
(Stage 8), M15 agentic workflow depth (Stage 9). The complete,
hash-verified record lives in [MILESTONES.md](MILESTONES.md); the
working rules are [PROCESS.md](PROCESS.md). This file no longer plans
milestones that have already happened — it keeps the honest list of
what is *not* done.

## Current known limits (deliberate, stated by the product)

1. **Windows-only packaging.** The portable zip, the installer, and the
   auto-update flow are Windows; there is no macOS/Linux bundle. The
   product says so wherever a Windows-only path is hit.
2. **Self-signed certificates.** M12's signing harness signs release
   artifacts before hashing, but the certificate is self-signed by the
   settled policy — trusted (EV/OV) signing starts when distribution
   goes public, not before. Users are told what SmartScreen will say.
3. **One model seam, CI never attached a model.** `TRINITY_MODEL_ENDPOINT`
   speaks the OpenAI `chat/completions` wire (README M15.5 documents
   attaching Ollama / LM Studio / llama.cpp-server / vLLM). CI verifies
   the no-model truth and the wire format with scripted fakes; a live
   model is a machine-owner decision, not a CI dependency.
4. **`v1.0.0` is deliberately untagged.** `v0.9.0` is the
   pipeline-proven test tag. Version bumps are a release-owner
   decision; re-tagging old shas from later stages is not done and the
   `v0.9.0` workflow runs stay as the published proof of the pipeline.

## Stage 10 (open): working the deferred list

Started because "recorded, not promised" was read as permission to keep
recorded forever — it is not. Working order is verification capacity,
not convenience: M16 first (a Linux machine exists only in CI, so CI
must be the builder), then the data seam, engine depth last, and the
native daemon port only after the machinery it needs ships. Each keeps
the M-rules: one concern per commit, verified before advertised.

- **M16 — Linux portable bundle** (done): same PyInstaller spec
  (now cross-platform), same smoke contract with a `--platform linux`
  layout (api/ + web/ + trinity.sh, no native/ — native-backed engines
  must refuse honestly *inside the bundle*, which the smoke now
  proves), kernel-collection gate on `libTK*.so*`/`OCP*.so*` count, and
  `.github/workflows/linux-package.yml` builds + smokes on the runner.
  M16.1 is green (Linux Package #6 @ `79955ee`) and M16.2 is green
  (Release #14 @ `ab67607`, tag `v1.0.0-m16proof`): the parallel
  `linux-package` job built + smoked the Linux zip, the Windows job
  downloaded it and added it to the manifest, upload, and download-back
  verification, while manifest semantics stay Windows-scoped for the
  updater (latest.yml still references only the win64 asset — M13's
  gates stay intact). The proof release is a prerelease;
  `/releases/latest` still resolves to `v0.9.0`.
- **M17 — data schema/migration seam** (in work): `DATA_SCHEMA.json`
  marker at `TRINITY_STORAGE_ROOT` recording `schema_version`, applied
  at startup before the database is touched. Legacy unmarked storage is
  version 0 and migration 1 is the idempotent v1 schema init; future
  versions refuse and say so rather than guessing, corrupt markers
  refuse the same way, and the migration chain is explicit and tested.
- **M18 — engine depth beyond CAD** (queued): smallest
  verify-before-advertise increments the registry rules allow.
- **Native daemon on POSIX** (queued last): WinHTTP is the blocker —
  the transport abstraction gets a POSIX backend before any bundle
  claims native engines on Linux.

## Deferred (recorded on purpose, not promised)

- macOS/Linux packaging — **M16 done for Linux (see Stage 10); macOS
  still deferred**: the Linux bundle builds, smokes, and ships through
  the release flow; signing and swap-on-boot machinery stay
  Windows-shaped, and the honest Linux bundle ships without native
  engines and says so.
- New engine types beyond CAD depth — same verify-before-advertise
  rule that took phantom capabilities out of the registry applies.
- Schema/migration of existing user data stores.
- Native daemon support for non-Windows toolchains, once CI can verify
  it rather than assume it.

---

## What the deferred list is NOT

It is not a claim that these exist. Nothing in the deferred list is
advertised by the product: the catalog, the panels, and the docs state
the truth they do support (capability chips, verbatim refusals, machine
labels). If any line of this file starts shipping, it re-enters the
tracking-file conventions of `opencodeworks.md` — one concern per
commit, worklog ticked in the same commit, verified before advertised.
