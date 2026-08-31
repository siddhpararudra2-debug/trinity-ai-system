# Backend application modules

This package is imported with `PYTHONPATH=artifacts/api-server` and is served as `app.main:app`.

The request path is: middleware → route → authentication/ownership check → service/engine → persistence/artifacts → response. Keep generated files and domain logic out of `routes/`; keep API contracts synchronized with `lib/api-spec/openapi.yaml`.

Engineering actions are intentionally split from server generation:

- the API creates scripts, typed specs, validation reports, and bundles;
- the Fusion and KiCad workers perform external-tool actions;
- the generic queue worker refuses unsupported engineering jobs.

Subpackage guides are represented in [`../../../docs/ARCHITECTURE.md`](../../../docs/ARCHITECTURE.md) to avoid duplicating documentation in every Python package.
