# Trinity AI architecture

This document explains the code that is currently in the repository. It is organized around runtime responsibility rather than the original product brief.

## Runtime components

### FastAPI service — `artifacts/api-server/app`

`app/main.py` creates the FastAPI application, initializes the database during lifespan startup, installs CORS/rate-limit/request-ID middleware, enforces the optional API-key boundary, and mounts every router below `/api`.

The service uses two kinds of persistence:

- SQLAlchemy + SQLite for users, conversations, messages, durable jobs, and workflow runs;
- `ArtifactStore` filesystem storage for design and firmware job JSON, manifests, and generated files, with optional S3-compatible uploads.

`app/auth.py` handles password hashing, signed bearer tokens, optional authentication enforcement, and the bootstrap admin flow. `app/security.py` handles the separate `X-Trinity-Api-Key`/Bearer infrastructure boundary. Production should enable both layers.

### Request and response path

`routes/chat.py` is the primary user path. It verifies conversation ownership, stores the user message, loads up to 20 prior messages, calls `TrinityOrchestrator.route`, stores the structured response, and returns the saved assistant message.

`routes/conversations.py` provides owner-scoped list/create/delete/history operations. `routes/vision.py` is a multipart path that verifies the conversation, MIME type, and 10 MB limit before passing bytes to `VisionEngine` and persisting both sides of the exchange.

### Routing — `app/unified_router`

`orchestrator.py` contains weighted regex rules. A manual engine override bypasses detection; otherwise multi-domain requests are first converted to a deterministic plan. Single-domain requests dispatch to an engine and return `{content, engine, data}`. Exceptions are formatted into an error response plus help text.

There is no implemented general LLM router in this module. `TRINITY_OPENAI_API_KEY` is not used to activate routing. LLM integration exists under `app/firmware/llm.py` and is limited to optional firmware generation.

### Specialist engines — `app/engines`

- `math_engine.py`: safe parser plus SymPy operations;
- `quantum_engine.py`: built-in templates and probabilistic count shaping;
- `maker_cad.py`: four Fusion 360 script generators;
- `maker_pcb.py`: KiCad S-expression text generator;
- `literature_engine.py`: arXiv Atom client/parser plus fallback cards;
- `vision_engine.py`: Pillow validation, pix2tex attempt, Tesseract fallback.

The direct-engine registry in `routes/engines.py` is also consumed by the sidebar. Its human descriptions contain some future-facing text (for example, vector search and ReAct loop). The implementation descriptions in this document are authoritative until the registry is corrected.

### Design pipeline — `app/designs`

`parsers.py` converts free-text CAD/PCB requests into typed Pydantic specs and clarification questions. `models.py` defines job, artifact, and validation contracts. `validators.py` performs static CAD, Python-script, PCB-text, and optional KiCad CLI checks. `jobs.py` generates files, combines reports, persists job metadata, and creates bundles. `artifacts.py` gives every stored file an ID, digest, MIME type, and protected download path.

CAD is server-side script generation. The server deliberately does not execute `adsk` code. STEP/STL/F3D output is completed by `workers/fusion/TrinityFusionWorker.py` after a signed claim.

PCB generation is server-side text/bundle generation. Full ERC/DRC/Gerber/drill/BOM/GLB output is completed by `workers/kicad/kicad_worker.py` when a real KiCad installation is available.

### Firmware pipeline — `app/firmware`

`registry.py` is the allowlist of concrete boards/frameworks plus generic guarded profiles. `knowledge.py` extracts hardware identifiers and checks known MCU metadata/pin ranges. `generator.py` produces deterministic starter project files. `universal.py` selects paradigm prompts and optionally parses structured LLM output without executing it. `validator.py` performs syntax and compatibility checks. `analysis.py` scans security patterns, reports dependencies, checks pins, and estimates resources. `builds.py` runs only registered allowlisted commands when explicitly enabled. `jobs.py` coordinates all of this, applies an in-process per-user rate limit, saves files, and creates a ZIP.

The estimates are heuristics based on source size and feature counts. They are not compiler/map-file measurements.

### Jobs and workflows

`job_queue.py` implements an SQLite queue with an atomic claim predicate, leases, retries, backoff, cancellation, and owner IDs. `job_worker.py` handles approved workflow bookkeeping and generic/no-op tasks. It intentionally refuses `cad`, `pcb`, `fusion`, `kicad`, `firmware`, and `ocr` kinds unless a corresponding trusted integration is installed.

`workflows.py` creates ordered plans. `routes/workflows.py` persists a plan, requires approval by default, enqueues it, and exposes status/approve/cancel endpoints. An approved workflow currently transitions to `awaiting_external_worker`; it does not execute each plan step or persist step outputs.

### Collaboration

`collab.py` keeps rooms in memory and broadcasts bounded JSON/text frames. If `TRINITY_REDIS_URL` is configured and the Redis package is installed, events are mirrored via pub/sub for multi-process fan-out. There is no durable notebook document model and the current production frontend does not open a WebSocket.

## Frontend — `artifacts/trinity`

`App.tsx` mounts the single `/` route and React Query provider. `ChatPage.tsx` composes the sidebar, thread, input, auth panel, and operations panel. `MessageInput.tsx` sends chat messages, chooses an engine override, and uploads supported images. `ChatThread.tsx` loads history. `MessageBubble.tsx` selects the renderer from the response data shape.

`components/messages/` contains the engine-specific views. `FirmwareMessage.tsx` is the richest renderer: multi-file selection, syntax highlighting, copy, resource bars, validation/security findings, and protected downloads. `components/ui/` is a reusable shadcn/Radix-style component library; it is not domain logic.

`lib/auth.ts` stores the bearer token in browser local storage. `main.tsx` registers that token with the generated API client. This is convenient for the current prototype but should be revisited for a hardened production browser security model.

`artifacts/mockup-sandbox` is a separate Vite UI sandbox with a copied component library. It has no demonstrated connection to the FastAPI application and should not be confused with the real Trinity web app.

## Contracts and generated code — `lib`

`lib/api-spec/openapi.yaml` is the intended contract source. Orval generates React Query hooks in `lib/api-client-react/src/generated/` and Zod schemas in `lib/api-zod/src/generated/`. `custom-fetch.ts` adds bearer-token injection, base URL configuration, response-type handling, and structured API errors.

`lib/db` contains a Drizzle/Postgres scaffold with an empty schema export. It is not imported by the Python API runtime, which uses SQLite through SQLAlchemy. Keep this package only if a future Node service will use it; otherwise remove or explicitly archive it.

## Database and migrations

The runtime calls `Base.metadata.create_all` during startup. Alembic also contains an initial migration for the SQLAlchemy models and can use `TRINITY_DATABASE_URL`. The project should choose one production schema strategy—migration-only is preferable—and add a clean migration test before release.

## External workers

- Fusion worker: operator enters a CAD job ID; the add-in claims a short-lived token, downloads and hashes the allowlisted script, executes it with restricted imports, exports requested formats, uploads artifacts, and completes the job.
- KiCad worker: accepts only job-local `.kicad_sch` and `.kicad_pcb` inputs and fixed subcommands for ERC/DRC and manufacturing outputs. It writes `kicad-worker-result.json` and can synchronize outputs to the API.
- Firmware container: currently provides a Python 3.11-slim build sandbox definition. The generated firmware service itself still requires target toolchains and explicit build enablement.
