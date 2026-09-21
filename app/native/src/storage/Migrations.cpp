#include "trinity/storage/Migrations.hpp"

#include "trinity/storage/Database.hpp"

namespace trinity::storage {

int currentSchemaVersion(Database& db) {
    const auto rows = db.query("PRAGMA user_version;");
    if (rows.empty() || rows[0].empty()) {
        return 0;
    }
    try {
        return std::stoi(rows[0][0]);
    } catch (...) {
        return 0;
    }
}

bool tableHasColumn(Database& db, const std::string& table, const std::string& column) {
    const auto rows = db.queryParams("SELECT name FROM pragma_table_info(?);", {table});
    for (const auto& row : rows) {
        if (!row.empty() && row[0] == column) {
            return true;
        }
    }
    return false;
}

namespace {

void addColumnIfMissing(Database& db, const std::string& table, const std::string& column,
                        const std::string& ddl) {
    if (!tableHasColumn(db, table, column)) {
        db.exec("ALTER TABLE " + table + " ADD COLUMN " + ddl + ";");
    }
}

void migrateToV2(Database& db) {
    Transaction tx(db);
    // jobs: full lifecycle fields (§1/§3). All nullable/defaulted so old rows load.
    addColumnIfMissing(db, "jobs", "request_id", "request_id TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db, "jobs", "workflow_id", "workflow_id TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db, "jobs", "started_at", "started_at TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db, "jobs", "completed_at", "completed_at TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db, "jobs", "artifacts", "artifacts TEXT NOT NULL DEFAULT '[]'");
    addColumnIfMissing(db, "jobs", "metadata", "metadata TEXT NOT NULL DEFAULT '{}'");
    addColumnIfMissing(db, "jobs", "timeout_ms", "timeout_ms INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db, "jobs", "retry_count", "retry_count INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db, "jobs", "max_retries", "max_retries INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db, "jobs", "retry_delay_ms", "retry_delay_ms INTEGER NOT NULL DEFAULT 0");
    // Backfill: input alias lives in `request`; keep both in sync at read time.
    // workflows: description/metadata/timestamps (§4).
    addColumnIfMissing(db, "workflows", "description", "description TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db, "workflows", "metadata", "metadata TEXT NOT NULL DEFAULT '{}'");
    addColumnIfMissing(db, "workflows", "started_at", "started_at TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db, "workflows", "completed_at", "completed_at TEXT NOT NULL DEFAULT ''");
    // workflow_nodes: result/error/retry/input_from/allow_failure (§4/§9/§10).
    addColumnIfMissing(db, "workflow_nodes", "result", "result TEXT NOT NULL DEFAULT 'null'");
    addColumnIfMissing(db, "workflow_nodes", "error", "error TEXT NOT NULL DEFAULT 'null'");
    addColumnIfMissing(db, "workflow_nodes", "retry_count", "retry_count INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db, "workflow_nodes", "max_retries", "max_retries INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db, "workflow_nodes", "retry_delay_ms",
                       "retry_delay_ms INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db, "workflow_nodes", "input_from", "input_from TEXT NOT NULL DEFAULT '{}'");
    addColumnIfMissing(db, "workflow_nodes", "allow_failure",
                       "allow_failure INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db, "workflow_nodes", "dependencies",
                       "dependencies TEXT NOT NULL DEFAULT '[]'");
    db.exec("PRAGMA user_version = 2;");
    tx.commit();
}

}  // namespace

void runMigrations(Database& db) {
    const int version = currentSchemaVersion(db);
    if (version < 2) {
        migrateToV2(db);
    }
}

}  // namespace trinity::storage
