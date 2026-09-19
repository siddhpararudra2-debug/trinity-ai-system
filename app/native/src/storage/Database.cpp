#include "trinity/storage/Database.hpp"

#include <filesystem>
#include <stdexcept>

#include <sqlite3.h>

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    open();
    char* error = nullptr;
    if (sqlite3_exec(db_, kSchemaSql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error != nullptr ? error : "unknown error";
        sqlite3_free(error);
        throw std::runtime_error("Failed to initialize schema: " + message);
    }
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

std::vector<Database::Row> Database::query(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    open();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("SQLite prepare failed: ") +
                                 sqlite3_errmsg(db_));
    }
    std::vector<Row> rows;
    int step = 0;
    while ((step = sqlite3_step(stmt)) == SQLITE_ROW) {
        Row row;
        const int columns = sqlite3_column_count(stmt);
        for (int i = 0; i < columns; ++i) {
            const unsigned char* text = sqlite3_column_text(stmt, i);
            row.emplace_back(text != nullptr ? reinterpret_cast<const char*>(text) : "");
        }
        rows.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    if (step != SQLITE_DONE) {
        throw std::runtime_error(std::string("SQLite step failed: ") +
                                 sqlite3_errmsg(db_));
    }
    return rows;
}

}  // namespace trinity::storage
