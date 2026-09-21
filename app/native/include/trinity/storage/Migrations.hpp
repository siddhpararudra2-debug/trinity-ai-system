#pragma once

// Additive versioned SQLite migrations (§3).
// Existing databases are upgraded in place; fresh databases get the
// baseline schema plus all deltas with an identical end state.
// Never requires a fresh database on dev builds.

#include <string>

namespace trinity::storage {

class Database;

/// Current migration version applied by runMigrations().
inline constexpr int kLatestSchemaVersion = 2;

/// Apply any pending migrations. Idempotent and safe to call twice.
void runMigrations(Database& db);
int currentSchemaVersion(Database& db);
bool tableHasColumn(Database& db, const std::string& table, const std::string& column);

}  // namespace trinity::storage
