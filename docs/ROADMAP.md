# Trinity AI roadmap

This roadmap is derived from the supplied product brief, current source, tests, and deployment files. “Implemented” means code exists; it does not mean an external engineering artifact is production-ready.

## Priority 0 — make the current baseline reproducible

1. **Declare every test dependency.** `tests/test_api_contract.py` imports PyYAML, but it is absent from both Python dependency manifests. Add `PyYAML` and rerun the suite from a clean environment.
2. **Fix the local build matrix.** The checked-in pnpm overrides remove Windows Rollup optional packages, while the current workspace is Windows. Decide whether Windows builds are supported and install the matching native dependency or document Linux/CI-only frontend builds.
3. **Choose one database lifecycle.** Startup calls `create_all` while Alembic is also maintained. Use migrations for deployed databases and add a fresh-database plus upgrade-from-existing-database check.
4. **Keep contract and implementation synchronized.** Compare `openapi.yaml`, generated clients, route response models, and frontend usage in CI. Add a test that catches route descriptions claiming capabilities the implementation does not provide.

## Priority 1 — finish the currently advertised platform

### Workflow execution

Current code plans and approval-gates workflows, then the generic worker marks an approved run as awaiting external work. Implement a real step executor that:

- persists step state and dependencies;
- dispatches only to registered adapters;
- passes previous outputs into downstream steps;
- pauses for human approval at engineering safety boundaries;
- records artifacts, logs, validation, and failures per step;
- supports retry/cancel/resume without duplicating external actions.

### CAD and PCB integration

- Connect the Fusion worker end to end and validate script hash, model features, solid bodies, export files, versions, and operator approval in integration tests.
- Resolve the KiCad 7 versus KiCad 9 mismatch in generated instructions, worker docs, and supported file/version policy.
- Add a real KiCad worker test fixture and documented ERC/DRC exception policy.
- Add reviewable previews and manufacturing metadata before calling a bundle production-ready.

### Firmware completeness

- Add per-target toolchain images and compile tests instead of relying on heuristic estimates and skipped builds.
- Make the target registry the generated source for UI/API documentation.
- Add generated-project diffs, version restore, and explicit conversation-version persistence.
- Expand hardware knowledge with authoritative package pin maps, errata, voltage constraints, and board-specific aliases.
- Keep flight-controller output gated behind SITL, bench, and human approval evidence.

### Vision and literature quality

- Add PDF and HEIC/HEIF normalization only with a bounded, tested conversion dependency.
- Replace literature’s synthetic fallback cards with an explicit `unavailable` state that cannot look like retrieved research.
- If “RAG” remains a product term, implement retrieval provenance, caching, citations, and a real document/vector index. Do not describe arXiv HTTP search as vector search.

### Collaboration

- Add a frontend WebSocket client and a visible session workflow.
- Define a durable notebook/document model, permissions, reconnect behavior, conflict handling, and Redis presence semantics.
- Add browser tests for authentication, reconnect, unauthorized access, and multi-user updates.

## Priority 2 — product decisions that are not in the current code

### General AI/code engine

The product brief and registry imply a general AI/code fallback and a ReAct-style autonomous orchestrator. The current code has neither. Decide whether to:

- implement a separately configured general assistant with explicit provider, budget, provenance, and safety controls; or
- remove those descriptions and keep unknown input as a deterministic help response.

Do not use the firmware LLM adapter as proof that a general code engine exists.

### Quantum scope

The current engine is a template demonstrator. If the intended product is a research simulator, add a real circuit model, arbitrary gate parsing, complex state vectors, normalized sampling, reproducible seeds, and limits. Otherwise rename the feature to make the template scope clear.

### Artifact and tenancy hardening

- Move rate limits and cache state out of process for multi-instance deployments.
- Add retention, deletion, quota, and malware/content scanning policies for uploaded/generated files.
- Use object-store presigned URLs consistently instead of retaining local compatibility files indefinitely.
- Review browser token storage and add CSRF/session strategy if cookie auth is introduced.

## Priority 3 — operational maturity

- Add end-to-end browser tests against the Docker stack.
- Add worker heartbeat/capability registration and stale-worker detection.
- Add structured logs/traces around engine selection, job IDs, artifact hashes, and external worker IDs.
- Add performance/load tests for SQLite contention, Redis fan-out, large firmware bundles, and OCR uploads.
- Add maintenance docs for pinned KiCad/Fusion/toolchain versions and hardware profile updates.
- Add release notes and a clear compatibility matrix for Python, Node, pnpm, KiCad, Fusion, Redis, and target SDKs.
