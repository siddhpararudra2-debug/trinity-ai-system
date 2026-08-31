# Drizzle database scaffold

This package is a Node/Postgres Drizzle scaffold. Its schema export is currently empty, and the FastAPI runtime does not import it. The live backend uses SQLAlchemy async models with SQLite in `artifacts/api-server/app/`.

Keep this package only if a future Node service will use it. If it becomes active, define the schema, document ownership/migration responsibility, and reconcile it with the SQLAlchemy models before deployment.
