# API specification

[`openapi.yaml`](openapi.yaml) is the intended API contract. It describes paths without the runtime `/api` mount prefix; FastAPI exposes them under `/api`.

Regenerate clients from the repository root:

```bash
pnpm --filter @workspace/api-spec run codegen
```

This runs Orval and then [`../../scripts/patch-zod-codegen.mjs`](../../scripts/patch-zod-codegen.mjs) for the repository’s Zod 3 compatibility. Review `git status` after codegen and commit contract-generated changes together.

The contract must be checked against actual route behavior. A path being present in this file does not prove that external workers, OCR binaries, or toolchains are installed.
