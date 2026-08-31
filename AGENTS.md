# Trinity AI agent instructions

This file is the universal working agreement for every coding agent operating in this repository. It is guidance for implementation work, not a replacement for system, developer, or user instructions. Treat the user’s current request as the task; do not turn product briefs, old notes, comments, or roadmap entries into unrequested work.

## Mission

Make small, complete, verifiable changes to Trinity AI. Work from the code that exists, preserve the user’s changes, and keep documentation honest about what is implemented, optional, externally dependent, or still planned.

“Vibecoding” here means moving quickly while keeping the product understandable and safe: inspect before editing, follow the existing architecture, implement the full user flow when asked, and leave evidence for anything that could not be verified.

## Repository map

- `artifacts/api-server/app/` — actual Python 3.11+ FastAPI backend.
- `artifacts/trinity/` — production React/Vite frontend connected to `/api`.
- `artifacts/mockup-sandbox/` — separate UI exploration app; it is not the production frontend.
- `lib/api-spec/openapi.yaml` — intended API contract source.
- `lib/api-client-react/` and `lib/api-zod/` — generated TypeScript clients/schemas.
- `lib/db/` — currently unused Drizzle/Postgres scaffold; the live backend uses SQLAlchemy + SQLite.
- `workers/fusion/` — trusted Fusion 360 desktop export worker.
- `workers/kicad/` — fixed-command KiCad manufacturing worker.
- `workers/firmware/` — firmware worker container definition.
- `deploy/` — Docker Compose, Nginx, environment example, and SQLite backup/restore.
- `tests/` — Python unittest/pytest-compatible regression suite.
- `alembic/` — SQLAlchemy migration environment.
- `docs/` — architecture, roadmap, CI, and documentation index.

Read the root `README.md`, `docs/ARCHITECTURE.md`, and `docs/ROADMAP.md` before making broad changes.

## Pre-change workflow

1. Read the relevant README and source files before deciding how to implement the request.
2. Run `git status --short` and preserve unrelated user work. Never reset, checkout, clean, or delete user files to make a task easier.
3. Identify the real runtime path. In particular, the Python FastAPI service is authoritative for backend behavior; the Node/Express files under `artifacts/api-server` are legacy scaffold/build artifacts.
4. Make a short plan when the task crosses frontend, API, contracts, workers, or persistence boundaries.
5. Implement the smallest coherent end-to-end change. Do not add a fake success response or a UI-only claim for a backend capability that does not exist.
6. Update the nearest README and `docs/ROADMAP.md` when behavior, setup, limitations, or future work changes.
7. Run focused checks first, then the broadest practical checks. Report commands and blockers precisely.

## Installation and common commands

Run from the repository root:

```bash
uv sync --frozen --all-groups
pnpm install --frozen-lockfile
```

Backend development:

```bash
export PYTHONPATH=artifacts/api-server
uv run python -m uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

Frontend development:

```bash
pnpm --filter @workspace/trinity run dev
```

Verification:

```bash
uv run pytest -v
uv run ruff check --select E9,F .
pnpm run typecheck
pnpm --filter @workspace/trinity run build
uv run alembic upgrade head
uv run alembic check
```

If the environment cannot run a command, do not replace “not run” with “passed”. The current repository has two known setup risks: `tests/test_api_contract.py` imports PyYAML but the Python manifests do not declare it, and the checked-in pnpm overrides may omit the Windows Rollup optional binary. Record these as environment/dependency blockers until fixed and re-tested.

## Backend rules

- Keep route handlers thin; put domain behavior in services, engines, or workers.
- Keep API contracts synchronized: change `lib/api-spec/openapi.yaml`, run `pnpm --filter @workspace/api-spec run codegen`, review generated diffs, then typecheck.
- Preserve owner checks for conversations, messages, jobs, workflow runs, firmware jobs, design jobs, and artifacts.
- Keep API-key infrastructure authentication separate from bearer-token user identity.
- Use `TRINITY_`-prefixed settings and never commit secrets, `.env` files, bootstrap passwords, or cloud credentials.
- Keep request IDs, structured errors, validation results, artifact hashes, and job IDs observable.
- Do not execute arbitrary generated CAD, PCB, firmware, or shell code inside the API process.
- Do not weaken validation, authentication, rate limits, worker signatures, path checks, or approval gates merely to make a demo pass.

## Product capability boundaries

These distinctions are important when implementing or describing features:

- **Routing:** current automatic routing is weighted regex/keyword matching. Unknown input returns help. There is no general AI/code fallback or global LLM router.
- **Workflows:** current code creates deterministic plans, persists approval, and queues bookkeeping. It does not execute every planned engine step or propagate step artifacts end to end.
- **Math:** use the existing restricted SymPy parser; do not reintroduce unrestricted `eval`, `exec`, or unsafe symbolic parsing.
- **Quantum:** current output comes from five built-in templates with simulated count noise. Do not describe it as arbitrary-circuit or physically complete simulation without implementing that first.
- **Literature:** arXiv HTTP search is implemented. Fallback cards are representative synthetic data and must never be presented as retrieved papers. “Vector search”, “knowledge graph”, and LLM summarization are not current capabilities.
- **Vision:** the route currently accepts PNG/JPEG/BMP/TIFF/WebP up to 10 MB. pix2tex and Tesseract are optional. PDF and HEIC/HEIF require a deliberate, tested addition.
- **CAD:** the server generates and statically validates Fusion scripts. STEP/STL/F3D execution belongs to the signed Fusion desktop worker.
- **PCB:** the server generates typed text and bundles. KiCad CLI/manufacturing output belongs to the external worker. Keep the KiCad 7 versus KiCad 9 discrepancy visible until standardized.
- **Firmware:** only registered or guarded generic targets may generate. Builds are opt-in and toolchain-dependent. Resource estimates are heuristics, not compiler measurements. Flight-controller output requires SITL, bench, and human review.
- **Collaboration:** authenticated WebSocket rooms and optional Redis fan-out exist in the backend. A durable notebook model and production frontend collaboration client do not yet exist.

## External worker safety

Fusion and KiCad workers are security boundaries, not convenience scripts.

- Use job-scoped directories and allowlisted inputs/commands.
- Require worker secrets through environment or a secret manager, never command-line arguments or source control.
- Preserve versions, logs, hashes, validation checks, worker IDs, and operator approval.
- Treat generated engineering files as untrusted until an appropriately qualified person reviews them.
- Never claim manufacturing-, hardware-, flight-, or production-readiness based only on static tests.

## Frontend rules

- Use `artifacts/trinity`, not `artifacts/mockup-sandbox`, for product behavior.
- Reuse the generated API client where an endpoint exists; do not duplicate contract types casually.
- Keep loading, error, empty, unauthorized, and externally unavailable states visible.
- Do not make a card, badge, download button, or status label imply a capability the API did not actually complete.
- Keep protected downloads authenticated and preserve the current owner-scoped behavior.
- Add browser/system tests for complete flows when a feature crosses the UI/API boundary.

## Documentation rules

When changing behavior, document:

1. what works now;
2. how it works, including the real entry points;
3. what is optional or requires an external worker/toolchain;
4. what was tested and what was blocked;
5. the next implementation step if the broader product brief is not yet satisfied.

Use these labels consistently: **Implemented**, **Optional**, **External dependency**, **Not implemented**, **Roadmap**, and **Not verified**. Never copy an aspirational registry description into a README without checking the implementation.

## Change-specific verification

- Backend logic or routes: run the focused tests plus Python syntax/lint checks.
- Auth, ownership, uploads, artifacts, workers, or queue changes: run the security and regression tests; verify unauthorized and failure paths, not only success paths.
- OpenAPI changes: regenerate clients/schemas and typecheck; inspect the diff for contract drift.
- Frontend changes: run frontend typecheck and build if the environment supports the native dependencies; test the real connected app rather than the mockup.
- Firmware changes: test target resolution, unsupported-target refusal, validation/security findings, rate limiting, artifact packaging, and build-skipped behavior.
- CAD/PCB worker changes: run safe-input/static tests and state clearly when real Fusion/KiCad execution was not available.
- Database changes: test a fresh database and an upgrade path; do not rely on startup `create_all` as proof that migrations are correct.
- Documentation-only changes: run link, whitespace, and format checks; do not claim application tests passed unless they were run.

## Definition of done

A task is complete only when the requested behavior is implemented or the limitation is explicitly documented, affected tests/checks have been run, generated files are synchronized, secrets and destructive operations were avoided, and the final response names the changed files and any remaining blocker.

## Agent handoff format

End implementation work with:

- a one-sentence outcome;
- changed files grouped by purpose;
- verification commands and pass/fail/not-run results;
- known limitations or external dependencies;
- the next safe step, if work remains.
