// Trinity — database schema migrations.
// Mirrors the V1 Python schema (jobs, artifacts, validations, cache_entries,
// projects) and adds desktop tables (settings, workflows, logs, engine_meta)
// with real version tracking.
#pragma once

#include <vector>

#include "Database.hpp"

namespace trinity::db {

const std::vector<Migrations::Step>& schema_steps();

// Convenience: open a database file and apply all schema migrations.
core::Result<Database> open_and_migrate(const std::string& file_path);

}  // namespace trinity::db
