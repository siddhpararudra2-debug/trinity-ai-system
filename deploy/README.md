# Trinity deployment

This directory provides a reproducible production baseline for the FastAPI service, durable database-backed worker, Redis collaboration bus, and React terminal frontend. Copy `.env.example` to `.env`, replace every placeholder secret, and start the complete stack from the repository root:

```bash
docker compose -f deploy/docker-compose.yml up --build
```

The `frontend` service builds the React application in a multi-stage Node image and serves it from Nginx. Nginx provides SPA fallback routing, proxies `/api/*` to the FastAPI `api` service, and keeps websocket upgrade headers available for `/api/ws`. The API is also exposed on port `8000` for direct diagnostics; the browser-facing entry point is port `80`.

The API stores SQLite data under the `trinity-data` volume and generated files under `trinity-artifacts`. For a multi-instance deployment, set `TRINITY_ARTIFACT_BACKEND=s3` and provide an S3-compatible bucket plus credentials through the platform secret manager. The application uploads artifacts and returns expiring presigned object links while retaining a local compatibility cache for the existing worker protocol.

Redis is used for cross-instance collaboration broadcasts when `TRINITY_REDIS_URL` is set. If the Redis package or service is unavailable, the API remains usable in single-process mode and exposes the limitation through deployment logs and capability checks. The queue worker uses leases and retries; unsupported engineering work is deliberately refused until a trusted external worker is installed.

Set `TRINITY_AUTH_REQUIRED=1` in production. Account registration creates ordinary users. An administrator is created or promoted only when `TRINITY_BOOTSTRAP_ADMIN_EMAIL` and `TRINITY_BOOTSTRAP_ADMIN_PASSWORD` are supplied at first startup. Rotate the bootstrap password after login and do not commit `.env`.

The `/api/healthz` endpoint reports capability flags, `/api/readyz` is intended for load-balancer readiness checks, and `/api/metrics` emits Prometheus-compatible counters. Configure log collection for the `trinity.api` logger and retain request IDs when investigating failures.

The repository’s local verification commands are:

```bash
PYTHONPATH=artifacts/api-server python3 -m unittest discover -s tests -p 'test_*.py' -v
./node_modules/.bin/tsc --build tsconfig.json
PORT=5173 BASE_PATH=/ ./artifacts/trinity/node_modules/.bin/vite build --config artifacts/trinity/vite.config.ts
```

The repository cannot validate proprietary Fusion 360 execution, KiCad ERC/DRC/Gerber output, target-specific firmware compilation, OCR binaries, or physical hardware in the sandbox. Run the documented Fusion and KiCad workers on their real host systems, install the selected firmware toolchains, and perform SITL, bench, manufacturing, and hardware review before treating outputs as production or flight-ready.
