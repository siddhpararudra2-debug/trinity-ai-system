# React API client

This package exports generated React Query hooks plus `custom-fetch.ts`.

`custom-fetch.ts` supports base URL configuration, bearer-token injection, JSON/text/blob response handling, and structured `ApiError`/`ResponseParseError` objects. The production web app registers a token getter in `artifacts/trinity/src/main.tsx`.

The `src/generated/` directory is generated output. Regenerate it through [`../api-spec/`](../api-spec/README.md) rather than editing individual hooks.
