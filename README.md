# Trinity AI

**Trinity AI** is a unified AI Engineering and Research Operating System. It provides a single terminal-style chat interface to interact with specialized engines, bridging complex engineering, research, and design tools into one workspace.

Full product/architecture specifications for all 20 roadmap features live in [`docs/features/`](docs/features/README.md).

## Core Engines (01–08)

| # | Engine | Technology | Output |
|---|--------|------------|--------|
| 01 | **Math Engine** | SymPy | LaTeX-rendered equations |
| 02 | **Quantum Lab** | NumPy | Bloch sphere & histograms |
| 03 | **Maker CAD** | Fusion 360 API | Python scripts → STEP/STL |
| 04 | **Maker PCB** | KiCad 7 S-expressions | ZIP project bundles |
| 05 | **Literature RAG** | arXiv Atom API | Interactive paper cards |
| 06 | **Vision Engine** | pix2tex + Tesseract | Extracted math LaTeX & text |
| 07 | **Firmware Engine** | Target C/C++ | Compilable project bundles |
| 08 | **General AI / Code** | LLM (optional) | Assistance, code, engine handoffs |

## Platform (09–12)

| # | Capability | Technology |
|---|------------|------------|
| 09 | **Unified Router** | Keyword/regex + optional LLM fallback |
| 10 | **Workflow Planner & Executor** | Deterministic multi-engine plans |
| 11 | **Durable Jobs Queue** | SQLite-backed leases & retries |
| 12 | **Collaboration Engine** | WebSockets + optional Redis pub/sub |

## Roadmap Expansions (13–20)

| # | Feature | Notes |
|---|---------|-------|
| 13 | **In-Browser CAD/PCB Viz** | Three.js STL viewer + SVG schematic preview |
| 14 | **WebSerial / WebUSB Flashing** | Browser flash scaffold for ESP32/Pico/STM32 |
| 15 | **Automated BOM & Sourcing** | `/api/sourcing/bom/*` with distributor quotes |
| 16 | **MCP Server Integration** | `/api/mcp/tools` for external assistants |
| 17 | **Multi-Engine Pipeline** | `PipelineExecutor` chains workflow steps |
| 18 | **Whiteboard Diagram-to-PCB** | `/api/pipelines/whiteboard-to-pcb` |
| 19 | **Paper-to-Code** | `/api/pipelines/paper-to-code` |
| 20 | **Forkable Project Links** | `/api/projects/share` and `/fork` |

## Key API Routes

- `POST /api/chat` — unified orchestrator
- `POST /api/pipelines/paper-to-code` — equation extraction → SymPy blocks
- `POST /api/pipelines/whiteboard-to-pcb` — diagram image → PCB intent
- `POST /api/pipelines/execute` — run multi-engine pipeline
- `POST /api/sourcing/bom/components` — BOM normalization & sourcing
- `GET /api/mcp/tools` — MCP tool discovery
- `POST /api/projects/share` — create forkable project link

## Tech Stack

* **Frontend:** React + Vite, Tailwind CSS v4, KaTeX, Recharts, Three.js previews
* **Backend:** Python 3.11 + FastAPI + SQLite (aiosqlite)
* **Routing:** Deterministic orchestrator in `app/unified_router/orchestrator.py`

## Run

```bash
pnpm --filter @workspace/api-server run dev   # API on :8080
pnpm --filter @workspace/trinity run dev      # React UI
```

Set `TRINITY_OPENAI_API_KEY` to enable General AI LLM responses. See `replit.md` for full operational details.
