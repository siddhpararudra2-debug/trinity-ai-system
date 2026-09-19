#pragma once

// SQLite persistence layer. No ORM; large files are never stored in
// the database, only their metadata. Mirrors src/db/database.py:
// WAL journal mode, foreign keys on, same five tables.

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "../core/Json.hpp"

struct sqlite3;

namespace trinity::storage {

extern const char* kSchemaSql;

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

    using Row = std::vector<std::string>;
    // Query rows; every column is returned as text (empty for NULL).
    std::vector<Row> query(const std::string& sql);

    const std::string& path() const noexcept { return dbPath_; }

private:
    void open();  // requires mutex_ held (open() runs PRAGMAs via exec())

    std::string dbPath_;
    sqlite3* db_ = nullptr;
    std::recursive_mutex mutex_;
};

}  // namespace trinity::storage
