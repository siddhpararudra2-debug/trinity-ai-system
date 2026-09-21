#include "trinity/storage/Database.hpp"

#include <filesystem>
#include <stdexcept>

#include <sqlite3.h>

#include "trinity/storage/Migrations.hpp"

namespace trinity::storage {
namespace fs = std::filesystem;

const char* kSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS jobs (
    job_id      TEXT PRIMARY KEY,
    engine      TEXT NOT NULL,
    operation   TEXT NOT NULL,
    status      TEXT NOT NULL,
    progress    REAL NOT NULL DEFAULT 0.0,
    request     TEXT NOT NULL,
    result      TEXT,
    error       TEXT,
    created_at  TEXT NOT NULL,
    updated_at  TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS workflows (
    workflow_id TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    status      TEXT NOT NULL,
    definition  TEXT NOT NULL,
    created_at  TEXT NOT NULL,
    updated_at  TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS workflow_nodes (
    node_id     TEXT PRIMARY KEY,
    workflow_id TEXT NOT NULL REFERENCES workflows(workflow_id) ON DELETE CASCADE,
    engine      TEXT NOT NULL,
    operation   TEXT NOT NULL,
    parameters  TEXT NOT NULL,
    status      TEXT NOT NULL,
    created_at  TEXT NOT NULL,
    updated_at  TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS artifacts (
    artifact_id TEXT PRIMARY KEY,
    job_id      TEXT REFERENCES jobs(job_id),
    type        TEXT NOT NULL,
    path        TEXT NOT NULL,
    size_bytes  INTEGER NOT NULL,
    checksum    TEXT NOT NULL,
    created_at  TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS validations (
    validation_id TEXT PRIMARY KEY,
    job_id        TEXT REFERENCES jobs(job_id),
    engine        TEXT NOT NULL,
    status        TEXT NOT NULL,
    checks        TEXT NOT NULL,
    created_at    TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS cache_entries (
    cache_key TEXT PRIMARY KEY,
    engine TEXT NOT NULL,
    operation TEXT NOT NULL,
    response TEXT NOT NULL,
    created_at TEXT NOT NULL,
    expires_at TEXT
);

CREATE TABLE IF NOT EXISTS projects (
    project_id TEXT PRIMARY KEY,
    name       TEXT NOT NULL,
    created_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_artifacts_job_id ON artifacts(job_id);
CREATE INDEX IF NOT EXISTS idx_validations_job_id ON validations(job_id);
CREATE INDEX IF NOT EXISTS idx_workflow_nodes_workflow_id ON workflow_nodes(workflow_id);
)SQL";

Database::Database(std::string dbPath) : dbPath_(std::move(dbPath)) {}

Database::~Database() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::open() {
    if (db_ != nullptr) {
        return;
    }
    sqlite3* handle = nullptr;
    if (sqlite3_open(dbPath_.c_str(), &handle) != SQLITE_OK) {
        const std::string message =
            handle != nullptr ? sqlite3_errmsg(handle) : "out of memory";
        sqlite3_close(handle);
        throw std::runtime_error("Failed to open database '" + dbPath_ + "': " + message);
    }
    db_ = handle;
    exec("PRAGMA foreign_keys = ON;");
    exec("PRAGMA journal_mode = WAL;");
}

void Database::init() {
    std::error_code ec;
    fs::create_directories(fs::path(dbPath_).parent_path(), ec);
    ec.clear();
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        open();
        char* error = nullptr;
        if (sqlite3_exec(db_, kSchemaSql, nullptr, nullptr, &error) != SQLITE_OK) {
            const std::string message = error != nullptr ? error : "unknown error";
            sqlite3_free(error);
            throw std::runtime_error("Failed to initialize schema: " + message);
        }
    }
    // Additive versioned migrations (in place, idempotent).
    runMigrations(*this);
}

void Database::exec(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    open();
    char* error = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error != nullptr ? error : "unknown error";
        sqlite3_free(error);
        throw std::runtime_error("SQLite exec failed: " + message);
    }
}

void Database::bindAll(sqlite3_stmt* stmt, const std::vector<SqlValue>& params) {
    for (size_t i = 0; i < params.size(); ++i) {
        const int idx = static_cast<int>(i + 1);
        const SqlValue& v = params[i];
        int rc = SQLITE_OK;
        if (std::holds_alternative<std::nullptr_t>(v)) {
            rc = sqlite3_bind_null(stmt, idx);
        } else if (std::holds_alternative<std::int64_t>(v)) {
            rc = sqlite3_bind_int64(stmt, idx, std::get<std::int64_t>(v));
        } else if (std::holds_alternative<double>(v)) {
            rc = sqlite3_bind_double(stmt, idx, std::get<double>(v));
        } else {
            const std::string& text = std::get<std::string>(v);
            rc = sqlite3_bind_text(stmt, idx, text.c_str(), -1, SQLITE_TRANSIENT);
        }
        if (rc != SQLITE_OK) {
            throw std::runtime_error(std::string("SQLite bind failed: ") +
                                     sqlite3_errmsg(db_));
        }
    }
}

void Database::execute(const std::string& sql, const std::vector<SqlValue>& params) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    open();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("SQLite prepare failed: ") +
                                 sqlite3_errmsg(db_));
    }
    try {
        bindAll(stmt, params);
        const int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            throw std::runtime_error(std::string("SQLite step failed: ") +
                                     sqlite3_errmsg(db_));
        }
    } catch (...) {
        sqlite3_finalize(stmt);
        throw;
    }
    sqlite3_finalize(stmt);
}

std::vector<Database::Row> Database::query(const std::string& sql) {
    return queryParams(sql, {});
}

std::vector<Database::Row> Database::queryParams(const std::string& sql,
                                                 const std::vector<SqlValue>& params) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    open();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("SQLite prepare failed: ") +
                                 sqlite3_errmsg(db_));
    }
    std::vector<Row> rows;
    int step = 0;
    try {
        bindAll(stmt, params);
        while ((step = sqlite3_step(stmt)) == SQLITE_ROW) {
            Row row;
            const int columns = sqlite3_column_count(stmt);
            for (int i = 0; i < columns; ++i) {
                const unsigned char* text = sqlite3_column_text(stmt, i);
                row.emplace_back(text != nullptr ? reinterpret_cast<const char*>(text) : "");
            }
            rows.push_back(std::move(row));
        }
    } catch (...) {
        sqlite3_finalize(stmt);
        throw;
    }
    sqlite3_finalize(stmt);
    if (step != SQLITE_DONE) {
        throw std::runtime_error(std::string("SQLite step failed: ") +
                                 sqlite3_errmsg(db_));
    }
    return rows;
}

void Database::begin() {
    exec("BEGIN;");
}

void Database::commit() {
    exec("COMMIT;");
}

void Database::rollback() {
    try {
        exec("ROLLBACK;");
    } catch (...) {
    }
}

std::string Database::lastError() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_ == nullptr) {
        return "database not open";
    }
    const char* msg = sqlite3_errmsg(db_);
    return msg != nullptr ? std::string(msg) : "unknown error";
}

Transaction::Transaction(Database& db) : db_(&db) {
    db_->begin();
}

Transaction::~Transaction() {
    if (!done_ && db_ != nullptr) {
        db_->rollback();
    }
}

void Transaction::commit() {
    if (!done_ && db_ != nullptr) {
        db_->commit();
        done_ = true;
    }
}

Statement::Statement(Database& db, sqlite3* handle, sqlite3_stmt* stmt)
    : db_(&db), handle_(handle), stmt_(stmt) {}

Statement::~Statement() {
    if (stmt_ != nullptr) {
        sqlite3_finalize(stmt_);
    }
}

Statement::Statement(Statement&& other) noexcept
    : db_(other.db_), handle_(other.handle_), stmt_(other.stmt_) {
    other.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
        db_ = other.db_;
        handle_ = other.handle_;
        stmt_ = other.stmt_;
        other.stmt_ = nullptr;
    }
    return *this;
}

void Statement::bind(int index, const SqlValue& value) {
    int rc = SQLITE_OK;
    if (std::holds_alternative<std::nullptr_t>(value)) {
        rc = sqlite3_bind_null(stmt_, index);
    } else if (std::holds_alternative<std::int64_t>(value)) {
        rc = sqlite3_bind_int64(stmt_, index, std::get<std::int64_t>(value));
    } else if (std::holds_alternative<double>(value)) {
        rc = sqlite3_bind_double(stmt_, index, std::get<double>(value));
    } else {
        const std::string& text = std::get<std::string>(value);
        rc = sqlite3_bind_text(stmt_, index, text.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string("SQLite bind failed: ") +
                                 sqlite3_errmsg(handle_));
    }
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    throw std::runtime_error(std::string("SQLite step failed: ") +
                             sqlite3_errmsg(handle_));
}

int Statement::columnCount() const {
    return sqlite3_column_count(stmt_);
}

std::string Statement::columnText(int col) const {
    const unsigned char* text = sqlite3_column_text(stmt_, col);
    return text != nullptr ? reinterpret_cast<const char*>(text) : "";
}

}  // namespace trinity::storage
