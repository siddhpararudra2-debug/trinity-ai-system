# Generated Zod contracts

This package exports Zod schemas and inferred types generated from the OpenAPI contract. The generated files under `src/generated/` are consumed by the API client and other TypeScript packages.

Run codegen from [`../api-spec/`](../api-spec/README.md). Keep the Zod major version and the post-generation compatibility patch synchronized with `package.json` and `scripts/patch-zod-codegen.mjs`.
