#include "Schema.hpp"

#include "../core/Time.hpp"

namespace trinity::db {
namespace {

// Version 1 — full desktop schema. Column names and semantics mirror the V1
// Python backend (backend/app/db/database.py) wherever the tables overlap.
const Migrations::Step kSteps[] = {
    {1, "base_schema",
     R"SQL(
CREATE TABLE projects (
    project_id   TEXT PRIMARY KEY,
    name         TEXT NOT NULL,
    description  TEXT NOT NULL DEFAULT '',
    workspace    TEXT NOT NULL DEFAULT '',
    archived     INTEGER NOT NULL DEFAULT 0,
    created_at   TEXT NOT NULL,
    updated_at   TEXT NOT NULL
);

CREATE TABLE jobs (
    job_id       TEXT PRIMARY KEY,
    project_id   TEXT REFERENCES projects(project_id),
    engine       TEXT NOT NULL,
    operation    TEXT NOT NULL,
    status       TEXT NOT NULL,
    progress     REAL NOT NULL DEFAULT 0.0,
    request      TEXT NOT NULL,
    result       TEXT,
    error        TEXT,
    logs         TEXT NOT NULL DEFAULT '[]',
    duration_ms  INTEGER,
    created_at   TEXT NOT NULL,
    updated_at   TEXT NOT NULL
);

CREATE TABLE artifacts (
    artifact_id       TEXT PRIMARY KEY,
    project_id        TEXT REFERENCES projects(project_id),
    job_id            TEXT REFERENCES jobs(job_id),
    type              TEXT NOT NULL,
    filename          TEXT NOT NULL,
    path              TEXT NOT NULL,
    size_bytes        INTEGER NOT NULL,
    hash_sha256       TEXT NOT NULL,
    engine            TEXT NOT NULL DEFAULT '',
    engine_version    TEXT NOT NULL DEFAULT '',
    validation_state  TEXT NOT NULL DEFAULT 'GENERATED',
    created_at        TEXT NOT NULL
);

CREATE TABLE validations (
    validation_id  TEXT PRIMARY KEY,
    artifact_id    TEXT REFERENCES artifacts(artifact_id),
    job_id         TEXT REFERENCES jobs(job_id),
    engine         TEXT NOT NULL,
    status         TEXT NOT NULL,
    checks         TEXT NOT NULL,
    created_at     TEXT NOT NULL
);

CREATE TABLE workflows (
    workflow_id   TEXT PRIMARY KEY,
    project_id    TEXT REFERENCES projects(project_id),
    name          TEXT NOT NULL,
    definition    TEXT NOT NULL,
    status        TEXT NOT NULL DEFAULT 'QUEUED',
    created_at    TEXT NOT NULL,
    updated_at    TEXT NOT NULL
);

CREATE TABLE logs (
    log_id      INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id  TEXT REFERENCES projects(project_id),
    job_id      TEXT REFERENCES jobs(job_id),
    level       TEXT NOT NULL,
    component   TEXT NOT NULL,
    message     TEXT NOT NULL,
    context     TEXT NOT NULL DEFAULT '{}',
    created_at  TEXT NOT NULL
);

CREATE TABLE engine_meta (
    engine_id     TEXT PRIMARY KEY,
    name          TEXT NOT NULL,
    version       TEXT NOT NULL,
    capabilities  TEXT NOT NULL,
    status        TEXT NOT NULL,
    updated_at    TEXT NOT NULL
);

CREATE TABLE settings (
    key         TEXT PRIMARY KEY,
    value       TEXT NOT NULL,
    updated_at  TEXT NOT NULL
);

CREATE TABLE cache_entries (
    cache_key   TEXT PRIMARY KEY,
    engine      TEXT NOT NULL,
    operation   TEXT NOT NULL,
    response    TEXT NOT NULL,
    created_at  TEXT NOT NULL,
    expires_at  TEXT
);

CREATE INDEX idx_jobs_project    ON jobs(project_id);
CREATE INDEX idx_jobs_status     ON jobs(status);
CREATE INDEX idx_artifacts_job   ON artifacts(job_id);
CREATE INDEX idx_artifacts_proj  ON artifacts(project_id);
CREATE INDEX idx_validations_job ON validations(job_id);
CREATE INDEX idx_logs_job        ON logs(job_id);
)SQL"},
};

}  // namespace

const std::vector<Migrations::Step>& schema_steps() {
    static const std::vector<Migrations::Step> steps(std::begin(kSteps), std::end(kSteps));
    return steps;
}

core::Result<Database> open_and_migrate(const std::string& file_path) {
    core::Error open_error;
    Database db = Database::open(file_path, &open_error);
    if (!db.is_open()) {
        return core::Result<Database>::fail(open_error.message().empty()
                                                ? core::Error(core::ErrorCode::DatabaseError,
                                                              "cannot open " + file_path)
                                                : open_error);
    }
    if (core::Status status = Migrations::apply(db, schema_steps()); !status.is_ok()) {
        return core::Result<Database>::fail(status.take_error());
    }
    return core::Result<Database>::ok(std::move(db));
}

}  // namespace trinity::db
