# Trinity AI

An LLM-independent engineering operating system: models may reason later, while deterministic tools execute and independent checks verify.

## Run

Backend: `cd backend && python -m pip install -e . && uvicorn app.main:app --reload`

Frontend: `npm install && npm run dev`

The flagship API request is `POST /api/cad/generate` with `{"parameters":{"overall_size":50},"outputs":["stl","glb","json"]}`. It generates a validated 50 mm quadcopter frame and persisted, checksummed artifacts. STEP is an explicit capability limitation until CadQuery/OpenCascade is configured.

See [architecture](docs/ARCHITECTURE.md), [CAD](docs/CAD.md), and [API](docs/API.md).
