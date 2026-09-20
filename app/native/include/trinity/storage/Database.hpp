#pragma once

// SQLite persistence layer. No ORM; large files are never stored in
// the database, only their metadata. Mirrors src/db/database.py:
// WAL journal mode, foreign keys on. Core tables: jobs, workflows,
// workflow_nodes, artifacts (plus legacy validations/cache_entries/
// projects kept for compatibility).

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "../core/Json.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace trinity::storage {

extern const char* kSchemaSql;

/// Bound parameter value for prepared statements.
using SqlValue =
    std::variant<std::nullptr_t, std::string, std::int64_t, double>;

inline SqlValue sqlNull() { return SqlValue(nullptr); }

class Database {
public:
    explicit Database(std::string dbPath);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Create parent directories and apply the schema. Idempotent.
    void init();

    // Execute a statement with no result rows (DDL/DML).
    void exec(const std::string& sql);
    // Prepared-statement execution with bound parameters (? placeholders).
    void execute(const std::string& sql, const std::vector<SqlValue>& params = {});

    using Row = std::vector<std::string>;
    // Query rows; every column is returned as text (empty for NULL).
    std::vector<Row> query(const std::string& sql);
    std::vector<Row> queryParams(const std::string& sql,
                                 const std::vector<SqlValue>& params);

    // Transaction control.
    void begin();
    void commit();
    void rollback();

    std::string lastError() const;

    const std::string& path() const noexcept { return dbPath_; }

private:
    void open();  // requires mutex_ held (open() runs PRAGMAs via exec())
    void bindAll(sqlite3_stmt* stmt, const std::vector<SqlValue>& params);

    std::string dbPath_;
    sqlite3* db_ = nullptr;
    mutable std::recursive_mutex mutex_;
};

/// RAII transaction: begins on construction, rolls back unless committed.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();

private:
    Database* db_ = nullptr;
    bool done_ = false;
};

/// RAII wrapper for a single prepared statement.
class Statement {
public:
    Statement(Database& db, sqlite3* handle, sqlite3_stmt* stmt);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    void bind(int index, const SqlValue& value);
    /// Step once; true while a row is available.
    bool step();
    int columnCount() const;
    std::string columnText(int col) const;

private:
    Database* db_ = nullptr;
    sqlite3* handle_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

}  // namespace trinity::storage
