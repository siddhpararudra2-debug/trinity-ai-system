# Trinity AI — Architecture (consolidated)

`API -> structured request -> job manager -> engine registry -> engine -> validation -> artifact manager -> SQLite lineage`

- LLM-independent: models emit structured tool-calls only, never execute engines.
- Deterministic engines: `math` (SymPy), `cad` (native mesh + STL/GLB), scaffolds for pcb/firmware/vision/research/simulation/robotics return `capability_unavailable`.
- Every call returns `{success, engine, operation, result, artifacts, validation, errors, job_id}`. Tracked failures are HTTP 200 `success:false`; unknown engine/job/artifact are real 404s.
- Frontend lives in `frontend/` (Next.js). Python core lives in `src/`. Runtime state lives in `data/` (gitignored except `.gitkeep`).
- Native foundation lives in `app/native/` (C++20 / CMake / Qt 6 / MSVC, `Trinity.exe`): same pipeline stages as native bootstrap (config → logging → paths → SQLite → engine registry → null model provider → Qt window). Engine implementations and real model providers are later phases; see `app/native/README.md`.
