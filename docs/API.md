# API

- `GET /api/health`, `GET /api/engines`
- `POST /api/execute`, `POST /api/math/solve`, `POST /api/cad/generate`
- `GET /api/jobs`, `GET /api/jobs/{job_id}`, `GET /api/artifacts/{artifact_id}`

`POST /api/cad/generate` accepts `{"parameters":{"overall_size":50},"outputs":["stl","glb","json"]}`.
