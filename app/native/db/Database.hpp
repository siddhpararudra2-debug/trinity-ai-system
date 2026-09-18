// Trinity — RAII SQLite wrapper over the vendored amalgamation.
// Policy carried over from the V1 Python backend:
//   - WAL journal mode, foreign keys ON
//   - busy timeout so concurrent job workers queue instead of failing
//   - one Database instance per thread or externally synchronised
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "../core/Error.hpp"
#include "../core/Json.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace trinity::db {

class Statement;

class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;

    // Opens (creating if needed) the database file with WAL + FK + busy_timeout.
    // Returns a non-throwing error for unusable paths.
    static Database open(const std::string& file_path, core::Error* out_error = nullptr);

    bool is_open() const { return handle_ != nullptr; }

    // Execute raw SQL (no results). Multiple statements allowed.
    core::Status execute(const std::string& sql);

    // Run a prepared statement with bound parameters; returns affected rows.
    core::Result<std::int64_t> run(const std::string& sql,
                                   const std::vector<core::Json>& params = {});

    // Query: returns all rows as arrays of JSON values (column order).
    core::Result<std::vector<std::vector<core::Json>>> query(
        const std::string& sql, const std::vector<core::Json>& params = {});

    // Query first row as object {column: value}; nullopt when no rows.
    core::Result<std::optional<core::JsonObject>> query_one(
        const std::string& sql, const std::vector<core::Json>& params = {});

    std::int64_t last_insert_rowid() const;

private:
    friend class Statement;
    sqlite3* handle_ = nullptr;
};

// Versioned, transactional migrations. The schema_migrations table records
// applied version numbers; PRAGMA user_version mirrors the max applied version.
class Migrations {
public:
    struct Step {
        int version;
        const char* name;
        const char* sql;  // multiple statements allowed; runs in a transaction
    };

    // Applies every step with version > current, in ascending order.
    static core::Status apply(Database& db, const std::vector<Step>& steps);
    static core::Result<int> current_version(Database& db);
};

}  // namespace trinity::db
