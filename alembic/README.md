# Alembic migrations

This directory contains the async Alembic environment and the initial migration for the SQLAlchemy models used by the FastAPI service.

The migration URL defaults to SQLite and can be overridden with `TRINITY_DATABASE_URL`:

```bash
uv run alembic upgrade head
uv run alembic check
```

The runtime currently also calls `Base.metadata.create_all` at startup. Choose a migration-only production lifecycle and add upgrade tests before relying on this directory for long-lived deployments.
