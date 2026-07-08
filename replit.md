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
- **Vision Engine** — Stub (pix2tex when installed)
- **General AI** — Fallback for unrecognized queries

## Gotchas

- After running `pnpm --filter @workspace/api-spec run codegen`, manually patch `lib/api-zod/src/generated/api.ts`: replace all `zod.looseObject({...})` with `zod.record(zod.string(), zod.unknown())` to keep Zod v3 compatibility.
- The api-server `start.sh` skips `pip install` — Python packages must be installed via the workspace package manager (they land in `.pythonlibs/`).
- Do not set `DATABASE_URL` for the Trinity backend; use `TRINITY_DATABASE_URL` instead if you need to override the SQLite path.

## User preferences

_Populate as you build — explicit user instructions worth remembering across sessions._

## Pointers

- See the `pnpm-workspace` skill for workspace structure, TypeScript setup, and package details
