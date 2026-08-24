# Trinity AI

A unified AI Engineering and Research Operating System with 8 specialized engines: math, quantum, maker (CAD & PCB), literature, vision, code, and general AI — all accessible through a single terminal-style chat interface.

## Run & Operate

- `pnpm --filter @workspace/api-server run dev` — run the FastAPI backend (port 8080); script runs `bash start.sh` from the artifact directory
- `pnpm --filter @workspace/trinity run dev` — run the React frontend (port auto-assigned)
- `pnpm run typecheck:libs` — typecheck all shared libraries
- `pnpm --filter @workspace/api-spec run codegen` — regenerate React Query hooks and Zod schemas from the OpenAPI spec

## Stack

- **Frontend:** React + Vite, Tailwind CSS v4, TanStack React Query, wouter, KaTeX (LaTeX rendering), Recharts (quantum histograms)
- **Backend:** Python 3.11 + FastAPI + Uvicorn, SQLite via aiosqlite + SQLAlchemy async ORM
- **AI Engines:** SymPy (math), NumPy Bloch sphere (quantum), Fusion 360 script gen (CAD), KiCad 7 S-expression gen (PCB), arXiv Atom API (literature)
- **API Contract:** OpenAPI spec at `lib/api-spec/openapi.yaml`; codegen via Orval into `lib/api-client-react` and `lib/api-zod`
- **Routing:** Keyword/regex orchestrator in `app/unified_router/orchestrator.py`; optional LLM routing if `TRINITY_OPENAI_API_KEY` is set

## Where things live

- `artifacts/api-server/app/` — Python FastAPI application
  - `main.py` — app factory, CORS, router mounts
  - `config.py` — Pydantic settings (`TRINITY_` env prefix to avoid PostgreSQL DATABASE_URL override)
  - `database.py` — async SQLAlchemy engine + SQLite init
  - `models.py` — Conversation and Message ORM models
  - `engines/` — SymPy, NumPy quantum, CAD, PCB, arXiv, vision engines
  - `unified_router/orchestrator.py` — regex-based routing to all 8 engines
  - `routes/` — health, chat, conversations, engines routers
- `artifacts/trinity/src/` — React frontend
  - `index.css` — full HSL color palette (dark terminal theme)
  - `pages/ChatPage.tsx` — main chat interface
  - `components/` — sidebar, message rendering, engine widgets
- `lib/api-spec/openapi.yaml` — single source of truth for all API contracts
- `lib/api-client-react/src/generated/` — generated React Query hooks
- `lib/api-zod/src/generated/` — generated Zod validation schemas

## Architecture decisions

- **SQLite not PostgreSQL:** Trinity uses SQLite (`sqlite+aiosqlite:///./trinity.db`) for zero-config self-contained storage. The `TRINITY_` env prefix on Pydantic settings prevents the workspace's `DATABASE_URL` (PostgreSQL) from being picked up.
- **Python FastAPI backend:** Spec required FastAPI. Python files live in `artifacts/api-server/app/`; the Node.js scaffold files (`src/`, `build.mjs`) are inert legacy.
- **Zod v3 compatibility:** Orval generates `z.looseObject` (Zod v4 API); the generated `lib/api-zod/src/generated/api.ts` has been patched to use `z.record(z.string(), z.unknown())` for Zod v3 compatibility. Re-running codegen will require re-applying this patch.
- **Keyword routing:** Engine selection uses regex matching in the orchestrator; no LLM call needed. Optional LLM routing activates when `TRINITY_OPENAI_API_KEY` is set.
- **NumPy quantum simulation:** Qiskit was too heavy; NumPy implements Bloch sphere coordinates and 5 circuit templates (bell, hadamard, grover, QFT, X-gate).

## Product

Trinity routes user messages to the correct specialist engine automatically:
- **Math Engine** — SymPy: solve, integrate, differentiate, simplify, factor; renders LaTeX in UI
- **Quantum Lab** — NumPy Bloch sphere + circuit templates; UI shows histogram of measurement counts
- **Maker CAD** — Generates Fusion 360 Python API scripts; UI provides download button
- **Maker PCB** — Generates KiCad 7 S-expression schematics/layouts; UI provides download button
- **Literature RAG** — Real arXiv Atom API search; UI renders paper cards with links
- **Vision Engine** — Image upload endpoint with optional pix2tex handwritten LaTeX OCR and free Tesseract printed-text fallback
- **General AI** — Fallback for unrecognized queries
- **Firmware Engine** — Free target-specific MCU and flight-controller project generation with validation and downloadable bundles
- **Collab Engine** — Single-process WebSocket rooms at `/api/ws`; use Redis/pub-sub for multi-worker production deployments
- **Workflow Planner** — Transparent deterministic multi-engine planning at `/api/workflows/plan`
- **Fusion Worker** — Signed desktop worker protocol for real STEP/STL/F3D exports

## Firmware Engine

The Firmware Engine is intentionally target-specific rather than claiming universal support. Registered profiles currently cover ESP32 DevKitC and ESP32-S3 DevKitC/ESP-IDF, Arduino Uno and Nano/AVR, STM32F103 Blue Pill, STM32F411 Black Pill, and STM32F746 Nucleo/HAL, Raspberry Pi Pico/RP2040 SDK, nRF52840/Zephyr, Pixhawk FMUv5 and FMUv6C/PX4, Cube Orange/ArduPilot, generic Betaflight STM32 F4 and H7, SpeedyBee F405/Betaflight, generic INAV STM32 F7, and Matek F722/INAV.

- `GET /api/firmware/targets` — list the registered boards and toolchains.
- `POST /api/firmware/jobs` — generate a deterministic firmware project from an explicit target, feature list, pin map, and peripheral list.
- `GET /api/firmware/jobs/{job_id}` — retrieve the persisted job, validation report, assumptions, and artifact URLs.
- `POST /api/vision/ocr` — upload a PNG/JPEG/BMP/TIFF/WebP image for OCR or LaTeX extraction.
- `GET /api/collab/sessions/{session_id}` and WebSocket `/api/ws?session_id=...` — join a bounded real-time collaboration room.
- `POST /api/workflows/plan` — decompose a multi-engine objective into a transparent deterministic sequence.
- `/api/fusion/jobs/{job_id}/claim|artifacts|complete` — signed trusted Fusion worker protocol for real exports.
- Firmware generation uses free/open-source toolchains and does not guess unsupported boards or pins.
- Set `TRINITY_ENABLE_FIRMWARE_BUILDS=1` only in a trusted worker with the required target toolchain installed; otherwise build validation is explicitly skipped and recorded.
- Flight-controller output is an extension/module scaffold and must be tested in SITL and on a safe bench before hardware or flight use.


## Design jobs and artifact delivery

- `POST /api/designs/cad` creates a typed CAD job, persists the Fusion script, and returns validation metadata and an artifact download URL.
- `POST /api/designs/pcb` creates a typed PCB job, persists the KiCad schematic, PCB layout, design specification, and project ZIP bundle.
- `GET /api/design-jobs/{job_id}` returns the persisted job status and validation report.
- `GET /api/artifacts/{artifact_id}` downloads an immutable generated artifact.
- Generated files are stored below `TRINITY_ARTIFACT_DIR` (default `./trinity_artifacts`).
- Set `TRINITY_ENABLE_KICAD_CLI=1` only in a trusted worker with a pinned `kicad-cli` installation to run ERC/DRC checks.
- Fusion STEP/STL/F3D export still requires a connected Fusion desktop worker; set `TRINITY_FUSION_WORKER_SECRET` to a random secret of at least 32 characters to enable the signed claim/upload/complete protocol. The server safely returns a validated Fusion script instead of trying to execute `adsk` code on Linux.
- Vision image uploads use `POST /api/vision/ocr`; pix2tex is optional for handwritten LaTeX and Tesseract is optional for printed-text OCR.
- Collaboration is single-process by default; use a shared pub/sub adapter for multiple API workers.

## Gotchas

- After running `pnpm --filter @workspace/api-spec run codegen`, manually patch `lib/api-zod/src/generated/api.ts`: replace all `zod.looseObject({...})` with `zod.record(zod.string(), zod.unknown())` to keep Zod v3 compatibility.
- The api-server `start.sh` skips `pip install` — Python packages must be installed via the workspace package manager (they land in `.pythonlibs/`).
- Do not set `DATABASE_URL` for the Trinity backend; use `TRINITY_DATABASE_URL` instead if you need to override the SQLite path.

## User preferences

_Populate as you build — explicit user instructions worth remembering across sessions._

## Pointers

- See the `pnpm-workspace` skill for workspace structure, TypeScript setup, and package details
