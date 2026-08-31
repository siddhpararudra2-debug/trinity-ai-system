# Shared libraries

`lib/` contains contract and client packages shared by the frontend workspace.

- [`api-spec/`](api-spec/README.md) — OpenAPI source and Orval codegen.
- [`api-client-react/`](api-client-react/README.md) — generated React Query hooks and fetch wrapper.
- [`api-zod/`](api-zod/README.md) — generated Zod schemas/types.
- [`db/`](db/README.md) — Drizzle/Postgres scaffold, currently unused by the FastAPI runtime.

When an API changes, update `api-spec/openapi.yaml`, run codegen, review generated diffs, then run the TypeScript typecheck. Do not hand-edit generated files except through the documented codegen patch.
