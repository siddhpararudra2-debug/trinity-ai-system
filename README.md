# Trinity AI

LLM-independent engineering OS: models reason and emit structured tool-calls, deterministic engines execute, independent checks validate.

`API -> structured request -> job manager -> engine registry -> engine -> validation -> artifact manager -> SQLite lineage`

## Layout

```
trinity-ai-system/
├── config/model_config.yaml, prompt_templates.yaml, logging_config.yaml
├── src/llm/base.py, openai_client.py, claude_client.py
├── src/agents/researcher.py, coder.py
├── src/prompt_engineering/few_shot.py, chain.py
├── src/utils/token_counter.py, rate_limiter.py, vector_store.py
├── src/engines/, src/api/, src/core/, src/jobs/, src/artifacts/, src/db/, src/models/
├── frontend/ (Next.js 16 / React 19)
├── data/cache, embeddings, evaluation/
├── notebooks/prompt_experimentation.ipynb
└── tests/
```

## Backend

```bash
python -m pip install -e .
uvicorn src.main:app --reload
```

## Frontend

```bash
npm --prefix frontend install
npm --prefix frontend run dev
```

Frontend calls `http://localhost:8000/api` via `NEXT_PUBLIC_TRINITY_API_URL`.

## API

- `GET /api/health`, `GET /api/engines`
- `POST /api/execute`, `POST /api/math/solve`, `POST /api/cad/generate`, `POST /api/requirements/execute`
- `GET /api/jobs`, `GET /api/jobs/{job_id}`, `GET /api/artifacts/{artifact_id}`

Example:

```json
POST /api/cad/generate
{"type": "quadcopter_frame", "parameters": {"overall_size": 50}, "outputs": ["stl", "glb", "json"]}
```

Every engine call returns `{success, engine, operation, result, artifacts, validation, errors, job_id}`. See `docs/ARCHITECTURE.md`.

## Native Windows app

`app/native/` hosts the native C++20 / CMake / Qt 6 / MSVC foundation
(`Trinity.exe`). See `app/native/README.md` for architecture, build
requirements, and launch instructions.
