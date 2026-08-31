# Trinity API server

The actual backend is the Python application under `app/`. The Node/Express files and `package.json` in this directory are legacy scaffold/build artifacts; [`start.sh`](start.sh) and [`Dockerfile`](Dockerfile) run FastAPI with Uvicorn.

## Run locally

From the repository root after `uv sync --frozen --all-groups`:

```bash
export PYTHONPATH=artifacts/api-server
uv run python -m uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

The service mounts routes below `/api`, initializes SQLite tables on startup, and exposes `/api/docs`, `/api/redoc`, and `/api/openapi.json` unless docs are disabled.

## Module map

- `app/main.py` — application factory, middleware, router registration, lifespan.
- `app/routes/` — HTTP/WebSocket endpoints.
- `app/engines/` — math, quantum, CAD, PCB, literature, and vision engines.
- `app/firmware/` — target registry, generation, validation, analysis, optional LLM, builds, and jobs.
- `app/designs/` — typed CAD/PCB jobs, validators, artifacts, and storage.
- `app/unified_router/` — weighted keyword routing and deterministic workflow detection.
- `app/job_queue.py`, `app/job_worker.py` — durable queue primitives and the deliberately limited worker.
- `app/auth.py`, `app/security.py` — bearer authentication and API-key boundary.
- `app/database.py`, `app/models.py` — SQLAlchemy async database setup and models.

See [`../../docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md) for behavior and boundaries. Do not treat the registry descriptions in `routes/engines.py` as proof that vector search, ReAct orchestration, or a general code engine exists.

## Configuration

All application settings use the `TRINITY_` prefix. The complete example is [`../../deploy/.env.example`](../../deploy/.env.example). Production must use strong API/auth secrets, owner-scoped bearer tokens, and explicit opt-ins for external tools.
