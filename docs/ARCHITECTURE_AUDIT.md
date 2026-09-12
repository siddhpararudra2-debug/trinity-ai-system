# Architecture audit

The repository contained a Next.js corporate site plus a compact FastAPI V1 backend. The backend already had SQLite job/artifact metadata, an engine registry, SymPy math, and a deterministic native STL generator. These components were preserved.

Gaps found: request handlers executed CPU work synchronously, CAD lacked GLB, STEP capability reporting was only informal, no cache/workflow primitives existed, only two engines were discoverable, and the frontend did not expose Trinity.

This build adds thread-offloaded API execution, deterministic SQLite cache for pure operations, a standards-compliant native GLB exporter, optional CAD adapter boundaries, truthful engine scaffolds, a deterministic requirement router, DAG ordering primitives, regression coverage, and a Trinity workspace UI. Real STEP remains correctly unavailable without an OpenCascade kernel.
