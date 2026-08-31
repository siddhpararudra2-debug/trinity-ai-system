# `artifacts/`

This directory contains runnable application artifacts.

- [`api-server/`](api-server/README.md) is the Python/FastAPI service and its container files.
- [`trinity/`](trinity/README.md) is the API-connected React/Vite production web app.
- [`mockup-sandbox/`](mockup-sandbox/README.md) is a separate UI exploration app and is not the production frontend.

The name “artifacts” is historical. It does not mean generated user files; runtime generated files belong under the configured `TRINITY_ARTIFACT_DIR`.
