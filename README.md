# Trinity AI

Trinity AI is a full-stack engineering workspace that gives users one terminal-style chat interface for mathematics, quantum examples, CAD, PCB generation, literature search, image OCR, firmware project generation, collaboration, and explicit multi-engine workflow planning.

This README is an implementation guide, not a product promise. It separates behavior verified in the repository from capabilities that still depend on external tools or need implementation.

**Documentation status:** 2026-08-31 · **License:** Private proprietary — Ram and Rudra
**Runtime shape:** React/Vite frontend + Python/FastAPI API + SQLite persistence + optional Redis and trusted workers

## Current status at a glance

| Area | Current state | Evidence and boundary |
| --- | --- | --- |
| Web chat | Implemented | `artifacts/trinity/src/pages/ChatPage.tsx` sends chat requests and displays persisted messages. |
| Conversations | Implemented | SQLAlchemy models and owner-scoped conversation routes. |
| Automatic routing | Implemented | Regex/keyword scoring in `artifacts/api-server/app/unified_router/orchestrator.py`. |
| Math | Implemented | Safe SymPy parsing for solve, integrate, differentiate, simplify, expand, factor, and evaluation. |
| Quantum | Implemented as templates | Five built-in circuit templates with simulated histogram noise; this is not a general quantum circuit simulator. |
| CAD | Implemented as script generation | Generates and statically validates Fusion 360 Python; real STEP/STL/F3D export needs the desktop worker. |
| PCB | Implemented as generated text/bundle | Generates typed schematic/PCB text and a ZIP; KiCad CLI validation and manufacturing exports are external opt-ins. |
| Literature | Implemented with caveat | Calls arXiv when reachable; falls back to representative cards in code, so fallback output must not be treated as retrieved papers. |
| Vision/OCR | Implemented with optional backends | Upload validation and persistence work; pix2tex and Tesseract must be installed for OCR. |
| Firmware | Implemented as guarded generation | Registered target profiles, deterministic starter projects, validation, security scan, estimates, and ZIP artifacts exist; target builds are disabled by default. |
| Collaboration | Backend implemented | Authenticated WebSocket rooms with in-memory state and optional Redis fan-out; the current web UI has no collaborative editor. |
| Durable jobs | Implemented as queue primitives | SQLite queue supports leases, retries, cancellation, and ownership. The bundled worker intentionally refuses unsupported engineering actions. |
| Workflow execution | Plan and approval implemented | Plans are persisted and approval-gated; the worker transitions approved workflows to external-worker handoff rather than executing all steps. |
| General AI/code engine | Not implemented as described | Unrecognized chat returns the help message. The only LLM adapter in the repository is for optional firmware generation. |

## What is in the product now

### User experience

The main page is a single React screen. It provides:

- account registration and login;
- a sidebar of the signed-in user’s conversations;
- automatic routing or a manual engine override;
- chat message history loaded from the API;
- engine-specific renderers for math, quantum, maker, literature, vision, firmware, and workflow responses;
- image upload for OCR from an existing conversation;
- an operations panel that polls health and durable jobs every five seconds;
- copy, expand, file selection, and protected download controls for firmware output.

The UI is intentionally terminal-like: dark Tailwind styling, monospace labels, KaTeX for mathematical output, and Recharts for quantum histograms.

### Engines and their real implementation

1. **Math** — `app/engines/math_engine.py` uses a restricted character set and a whitelist of SymPy functions/symbols before dispatching to SymPy. It returns plain text, LaTeX, and short operation steps.
2. **Quantum Lab** — `app/engines/quantum_engine.py` recognizes Bell, Hadamard, Grover, QFT, and Pauli-X requests. It returns an ASCII circuit, a template state label, Bloch coordinates, counts, and a simplified state vector. It does not parse arbitrary gates or calculate a full state vector.
3. **Maker CAD** — `app/engines/maker_cad.py` extracts dimensions and generates Fusion 360 scripts for an L-bracket, housing, spur gear, or shaft. `app/designs/` adds typed specs, static checks, job metadata, and immutable artifacts.
4. **Maker PCB** — `app/engines/maker_pcb.py` generates KiCad S-expression text. `app/designs/` adds typed board specs, static reference/net checks, a design manifest, and a project ZIP. The generated engine instructions mention KiCad 7 while the external worker is written for KiCad 9; standardize this before promising version-specific compatibility.
5. **Literature** — `app/engines/literature_engine.py` calls the arXiv Atom API, parses paper cards, and summarizes the returned list. On any network/client failure it creates representative fallback cards using an arXiv search URL. There is no vector database, NetworkX graph, or LLM summarizer in the current implementation.
6. **Vision** — `app/engines/vision_engine.py` verifies image bytes with Pillow, then tries pix2tex for LaTeX or Tesseract for printed text. The route accepts PNG, JPEG, BMP, TIFF, and WebP up to 10 MB. PDF and HEIC are not accepted by the current route.
7. **Firmware** — `app/firmware/` resolves requests only to explicit target profiles or guarded generic paradigm profiles. It can generate deterministic multi-file projects, optionally call a structured-output LLM, validate syntax/pins/security patterns/dependencies, estimate resources, and create a ZIP. Build execution is opt-in and toolchain-dependent; flight-controller output always needs independent review and SITL/bench testing.
8. **Collaboration** — `app/collab.py` manages bounded WebSocket rooms, rejects unauthenticated connections, limits frames to 12,000 characters, and optionally mirrors events through Redis. No persistent notebook/document model is present.

The orchestrator is the routing layer around those engines. It scores regex matches, accepts a manual override, detects multi-domain requests with `build_workflow_plan`, and returns a transparent ordered plan. It does not provide a general-purpose autonomous ReAct loop.

## Request flow

```text
Browser
  -> Nginx (/api proxy and SPA fallback, production)
  -> FastAPI middleware (request ID, API key boundary, rate limiter)
  -> route handler
       -> auth and owner checks
       -> SQLite conversation/job persistence
       -> orchestrator or direct engine
       -> static validation and artifact storage where applicable
  -> JSON response
  -> React renderer / protected download
```

For a normal chat request, `POST /api/chat` creates or reuses an owned conversation, stores the user message, routes the query, stores the assistant message and structured `data`, and returns the saved assistant message. The browser then invalidates the conversation/message queries.

For a generated design or firmware project, the service writes job JSON and immutable files under the configured artifact directory. Every artifact records a SHA-256 digest and an owner-scoped download URL. S3-compatible storage is available only when `TRINITY_ARTIFACT_BACKEND=s3` and its credentials are configured.

## Repository map

```text
trinity-ai-system/
├─ artifacts/
│  ├─ api-server/       FastAPI application and runtime container
│  ├─ trinity/           Production React/Vite web application
│  └─ mockup-sandbox/    Separate UI mockup/reference app; not the API-connected product
├─ lib/
│  ├─ api-spec/          OpenAPI source contract and Orval config
│  ├─ api-client-react/  Generated React Query hooks plus safe fetch wrapper
│  ├─ api-zod/           Generated Zod schemas/types
│  └─ db/                Drizzle/Postgres scaffold; not used by the FastAPI runtime
├─ workers/
│  ├─ firmware/          Firmware sandbox container definition
│  ├─ fusion/            Fusion 360 desktop add-in and signed worker protocol
│  └─ kicad/             Fixed-command KiCad manufacturing worker
├─ deploy/               Docker Compose, Nginx, environment example, backup/restore
├─ tests/                Python unittest/pytest-compatible regression suite
├─ alembic/              SQLAlchemy database migration environment
├─ scripts/              TypeScript utility scripts and codegen patch
└─ docs/                 CI, architecture, roadmap, and documentation index
```

Folder-level guides are available in each major runtime/package directory. Start at [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for file-by-file responsibilities and [`docs/ROADMAP.md`](docs/ROADMAP.md) for verified gaps and the recommended delivery order.

## Quick start

### Requirements

- Python 3.11 or newer;
- Node.js 22 for parity with CI (Vite requires Node 20.19+);
- pnpm 10;
- Docker Desktop if using the complete deployment stack;
- `uv` for Python dependency synchronization.

### Local development

Install both dependency sets from the repository root:

```bash
uv sync --frozen --all-groups
pnpm install --frozen-lockfile
```

Start the API in one terminal:

```bash
export PYTHONPATH=artifacts/api-server
uv run python -m uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

Start the real frontend in another:

```bash
pnpm --filter @workspace/trinity run dev
```

The Vite dev server proxies `/api` to `http://localhost:8000` by default. The API documentation is available at `http://localhost:8000/api/docs` unless `TRINITY_DISABLE_DOCS=1` is set.

### Docker deployment baseline

The complete stack is defined in [`deploy/docker-compose.yml`](deploy/docker-compose.yml): frontend/Nginx, API, durable bookkeeping worker, and Redis.

```bash
cp deploy/.env.example deploy/.env
# Replace every placeholder secret in deploy/.env.
docker compose -f deploy/docker-compose.yml up --build
```

Open `http://localhost`. The API is also exposed at `http://localhost:8000` for diagnostics. Use [`deploy/README.md`](deploy/README.md) for authentication, storage, backups, worker setup, and production restrictions.

## API surface

The canonical contract is [`lib/api-spec/openapi.yaml`](lib/api-spec/openapi.yaml). All API paths are mounted below `/api` in FastAPI. The main groups are:

- health: `/healthz`, `/readyz`, `/metrics`;
- accounts: `/auth/register`, `/auth/login`, `/auth/me`, `/auth/users`;
- chat/history: `/chat`, `/conversations`, `/conversations/{id}/messages`;
- direct engines: `/engines`, `/engines/math`, `/engines/quantum`, `/engines/maker/cad`, `/engines/maker/pcb`, `/engines/literature`;
- designs/artifacts: `/designs/cad`, `/designs/pcb`, `/design-jobs/{job_id}`, `/artifacts/{artifact_id}`;
- firmware: `/firmware/targets`, `/firmware/jobs`, `/firmware/jobs/{job_id}`;
- vision: multipart `/vision/ocr`;
- collaboration: `/collab/sessions/{session_id}` and WebSocket `/ws`;
- workflows and jobs: `/workflows/*` and `/jobs/*`;
- external workers: `/fusion/jobs/*` and `/kicad/jobs/*`.

Regenerate client code after changing the OpenAPI contract:

```bash
pnpm --filter @workspace/api-spec run codegen
```

The codegen script also runs [`scripts/patch-zod-codegen.mjs`](scripts/patch-zod-codegen.mjs) because the generated output must remain compatible with the repository’s Zod 3 dependency.

## Security and safety boundaries

- Set `TRINITY_AUTH_REQUIRED=1`, a strong `TRINITY_AUTH_SECRET`, and a strong `TRINITY_API_KEY` in production.
- API key middleware is an infrastructure boundary; bearer tokens identify the owning user. Conversation, job, workflow, and artifact reads are owner-scoped.
- Never commit `.env`, worker secrets, API keys, bootstrap passwords, or cloud credentials.
- Generated CAD/Firmware/PCB content is untrusted output. Static validation is not proof of engineering correctness.
- Fusion execution is isolated behind a signed, claim-token-based desktop worker that validates the script hash and allowlisted imports.
- KiCad worker commands are fixed by the worker code; they are not taken from user input. Do not ship files when ERC/DRC has failed without qualified engineering approval.
- Flight-controller output is a scaffold/module starting point. Test in simulation and on a safe bench before any hardware or flight use.

## Verification status

The repository contains a regression suite and CI configuration in [`.github/workflows/ci.yml`](.github/workflows/ci.yml). The following checks were attempted during this documentation pass on 2026-08-31:

- **Passed:** TypeScript project build/typecheck with `tsc --build tsconfig.json`.
- **Not completed:** Python tests. The repository `.venv` launcher returned Windows `Access is denied`; the bundled runtime did not contain the project dependencies, and combining it with the `.venv` packages failed on the compiled `pydantic_core` extension.
- **Blocked in this sandbox:** Vite production build because the installed pnpm tree is missing `@rollup/rollup-win32-x64-msvc`. This is an environment/install issue, not a TypeScript error. Reinstall with the supported pnpm setup on the target machine and verify the native optional dependency.
- **Additional repository gap:** `tests/test_api_contract.py` imports PyYAML, but `pyproject.toml` and `artifacts/api-server/requirements.txt` do not declare `PyYAML`. Add it to the dependency manifests before relying on a clean Python CI run.

Run the full checks on a prepared development machine using [`docs/ci.md`](docs/ci.md). Treat external Fusion, KiCad, OCR, firmware toolchain, hardware, and flight validation as separate system-integration gates.

## Roadmap summary

The detailed, evidence-based roadmap is in [`docs/ROADMAP.md`](docs/ROADMAP.md). The highest-value next steps are:

1. make dependency and migration checks reproducible on a fresh machine;
2. reconcile the OpenAPI contract, generated clients, and current route behavior continuously;
3. implement real workflow step execution with explicit worker adapters and result propagation;
4. replace synthetic literature fallback cards with an explicit unavailable state and add true retrieval/RAG if required;
5. standardize KiCad version support and complete external manufacturing validation;
6. decide whether Trinity needs a real general AI/code engine and implement it separately from firmware LLM generation;
7. add the missing collaborative web client, PDF/HEIC vision path, firmware diffs/version restore, and end-to-end browser/system tests.

## Source documentation

The supplied `trinity-ai-documentation.docx` was used as a product brief. Its descriptions are retained where the code confirms them and marked as planned, optional, externally dependent, or not currently implemented where the repository differs.

This repository is private and is jointly owned by Ram and Rudra. See [`LICENSE`](LICENSE) for the private proprietary usage terms. It is not an open-source project.
