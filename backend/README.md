# Trinity Backend (V1)

LLM-independent engineering execution core, per `PRD v1.0`:
`API -> Structured Request -> Engine Registry -> Engine -> Validation -> Artifact`.

Implemented so far:
- **Core** — FastAPI app, SQLite (no ORM), job system, artifact manager, structured logging, typed error hierarchy
- **Math engine** — SymPy-backed `solve` / `evaluate`, each result re-verified against the original expression
- **CAD engine** — parametric quadcopter-frame generator (pure-Python mesh + binary STL export), with `generate/validate/export/preview` shaped so a CadQuery/FreeCAD/Onshape adapter can drop in later without touching the API layer

Not yet built: PCB (KiCad), firmware (PlatformIO), vision, research engine, workflow DAG engine, Onshape adapter, model-provider abstraction.

## Run it

```bash
cd backend
pip install -r requirements.txt
python -m uvicorn app.main:app --reload --port 8000
```

## Try it

```bash
curl -X POST localhost:8000/api/math/solve \
  -H 'Content-Type: application/json' \
  -d '{"expression": "x**2 - 4 = 0"}'

curl -X POST localhost:8000/api/cad/generate \
  -H 'Content-Type: application/json' \
  -d '{"type":"quadcopter_frame","parameters":{"overall_size":50},"outputs":["stl","json"]}'
```

## Tests

```bash
pip install pytest httpx
pytest tests/ -v
```

## API surface

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/health` | Liveness check |
| GET | `/api/engines` | List registered engines + capabilities |
| POST | `/api/execute` | Generic `{engine, operation, parameters}` entrypoint |
| GET | `/api/jobs` | Recent jobs |
| GET | `/api/jobs/{job_id}` | Job status/result |
| GET | `/api/artifacts/{artifact_id}` | Download a generated file |
| POST | `/api/math/solve` | Solve/evaluate an expression |
| POST | `/api/cad/generate` | Generate a parametric CAD part |

Every engine call returns the same envelope: `{success, engine, operation, result, artifacts, validation, errors, job_id}`.
Engine-time failures (bad params, failed geometry checks) come back as a **tracked failed job** — HTTP 200,
`success: false`, `errors` populated — while genuine lookup failures (unknown engine/job/artifact) return real
404s. Check `success`, not just the status code.
