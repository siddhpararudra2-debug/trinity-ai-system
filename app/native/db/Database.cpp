#include "Database.hpp"

#include <sqlite3.h>

#include "../core/Logging.hpp"
#include "../core/Time.hpp"

namespace trinity::db {
namespace {

core::Error sqlite_error(sqlite3* handle, const std::string& context) {
    const char* message = sqlite3_errmsg(handle);
    return core::Error(core::ErrorCode::DatabaseError, context + ": " + (message ? message : "?"));
}

// Bind a JSON parameter to a sqlite statement at 1-based index.
// Mapping: null->NULL, bool/int/double->numeric, string->text.
core::Status bind_params(sqlite3_stmt* stmt, const std::vector<core::Json>& params) {
    for (std::size_t i = 0; i < params.size(); ++i) {
        const core::Json& value = params[i];
        const int index = static_cast<int>(i) + 1;
        if (value.is_null()) {
            if (sqlite3_bind_null(stmt, index) != SQLITE_OK) {
                return core::Status::fail(
                    core::Error(core::ErrorCode::DatabaseError, "failed to bind null"));
            }
        } else if (value.is_bool()) {
            if (sqlite3_bind_int(stmt, index, value.as_bool() ? 1 : 0) != SQLITE_OK) {
                return core::Status::fail(
                    core::Error(core::ErrorCode::DatabaseError, "failed to bind bool"));
            }
        } else if (value.is_number()) {
            const double number = value.as_double();
            if (number == static_cast<double>(static_cast<std::int64_t>(number))) {
                sqlite3_bind_int64(stmt, index, static_cast<sqlite3_int64>(number));
            } else {
                sqlite3_bind_double(stmt, index, number);
            }
        } else {
            const std::string& text = value.as_string();
            if (sqlite3_bind_text(stmt, index, text.c_str(), static_cast<int>(text.size()),
                                  SQLITE_TRANSIENT) != SQLITE_OK) {
                return core::Status::fail(
                    core::Error(core::ErrorCode::DatabaseError, "failed to bind text"));
            }
        }
    }
    return core::Status::ok();
}

core::Json column_value(sqlite3_stmt* stmt, int column) {
    switch (sqlite3_column_type(stmt, column)) {
        case SQLITE_INTEGER:
            return core::Json(static_cast<std::int64_t>(sqlite3_column_int64(stmt, column)));
        case SQLITE_FLOAT:
            return core::Json(static_cast<double>(sqlite3_column_double(stmt, column)));
        case SQLITE_TEXT: {
            const auto* text =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
            return core::Json(std::string(text ? text : ""));
        }
        case SQLITE_BLOB: {
            const void* blob = sqlite3_column_blob(stmt, column);
            const int size = sqlite3_column_bytes(stmt, column);
            return core::Json(std::string(static_cast<const char*>(blob),
                                          static_cast<std::size_t>(size)));
        }
        default:
            return core::Json(nullptr);
    }
}

std::string column_name(sqlite3_stmt* stmt, int column) {
    const char* name = sqlite3_column_name(stmt, column);
    return std::string(name ? name : "");
}

class ScopedStmt {
public:
    ScopedStmt(sqlite3* handle, const std::string& sql, core::Error* error)
        : stmt_(nullptr) {
        if (sqlite3_prepare_v2(handle, sql.c_str(), static_cast<int>(sql.size()), &stmt_,
                               nullptr) != SQLITE_OK) {
            if (error) *error = sqlite_error(handle, "prepare failed: " + sql);
            stmt_ = nullptr;
        }
    }
    ~ScopedStmt() {
        if (stmt_ != nullptr) sqlite3_finalize(stmt_);
    }
    sqlite3_stmt* get() const { return stmt_; }
    explicit operator bool() const { return stmt_ != nullptr; }

private:
    sqlite3_stmt* stmt_;
};

}  // namespace

Database::~Database() {
    if (handle_ != nullptr) {
        sqlite3_close_v2(handle_);
        handle_ = nullptr;
    }
}

Database::Database(Database&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) sqlite3_close_v2(handle_);
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

Database Database::open(const std::string& file_path, core::Error* out_error) {
    Database db;
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(file_path.c_str(), &db.handle_, flags, nullptr) != SQLITE_OK) {
        if (out_error) {
            *out_error = core::Error(core::ErrorCode::DatabaseError,
                                     "cannot open database at " + file_path);
        }
        if (db.handle_ != nullptr) {
            sqlite3_close_v2(db.handle_);
            db.handle_ = nullptr;
        }
        return db;
    }
    sqlite3_busy_timeout(db.handle_, 5000);
    if (sqlite3_exec(db.handle_, "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;",
                     nullptr, nullptr, nullptr) != SQLITE_OK) {
        if (out_error) *out_error = sqlite_error(db.handle_, "pragma setup failed");
    }
    return db;
}

core::Status Database::execute(const std::string& sql) {
    if (handle_ == nullptr) {
        return core::Status::fail(
            core::Error(core::ErrorCode::DatabaseError, "database is not open"));
    }
    char* error_message = nullptr;
    const int rc = sqlite3_exec(handle_, sql.c_str(), nullptr, nullptr, &error_message);
    if (rc != SQLITE_OK) {
        const std::string message = error_message != nullptr ? error_message : "exec failed";
        sqlite3_free(error_message);
        return core::Status::fail(
            core::Error(core::ErrorCode::DatabaseError, message));
    }
    return core::Status::ok();
}

core::Result<std::int64_t> Database::run(const std::string& sql,
                                         const std::vector<core::Json>& params) {
    if (handle_ == nullptr) {
        return core::Result<std::int64_t>::fail(
            core::Error(core::ErrorCode::DatabaseError, "database is not open"));
    }
    core::Error error;
    ScopedStmt stmt(handle_, sql, &error);
    if (!stmt) return core::Result<std::int64_t>::fail(error);

    if (core::Status bind_status = bind_params(stmt.get(), params); !bind_status.is_ok()) {
        return core::Result<std::int64_t>::fail(bind_status.take_error());
    }

    const int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        return core::Result<std::int64_t>::fail(sqlite_error(handle_, "step failed"));
    }
    return core::Result<std::int64_t>::ok(sqlite3_changes64(handle_));
}

core::Result<std::vector<std::vector<core::Json>>> Database::query(
    const std::string& sql, const std::vector<core::Json>& params) {
    if (handle_ == nullptr) {
        return core::Result<std::vector<std::vector<core::Json>>>::fail(
            core::Error(core::ErrorCode::DatabaseError, "database is not open"));
    }
    core::Error error;
    ScopedStmt stmt(handle_, sql, &error);
    if (!stmt) {
        return core::Result<std::vector<std::vector<core::Json>>>::fail(error);
    }
    if (core::Status bind_status = bind_params(stmt.get(), params); !bind_status.is_ok()) {
        return core::Result<std::vector<std::vector<core::Json>>>::fail(
            bind_status.take_error());
    }

    std::vector<std::vector<core::Json>> rows;
    const int columns = sqlite3_column_count(stmt.get());
    int rc = 0;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        std::vector<core::Json> row;
        row.reserve(static_cast<std::size_t>(columns));
        for (int c = 0; c < columns; ++c) row.push_back(column_value(stmt.get(), c));
        rows.push_back(std::move(row));
    }
    if (rc != SQLITE_DONE) {
        return core::Result<std::vector<std::vector<core::Json>>>::fail(
            sqlite_error(handle_, "query failed"));
    }
    return core::Result<std::vector<std::vector<core::Json>>>::ok(std::move(rows));
}

core::Result<std::optional<core::JsonObject>> Database::query_one(
    const std::string& sql, const std::vector<core::Json>& params) {
    auto rows = query(sql, params);
    if (rows.is_error()) {
        return core::Result<std::optional<core::JsonObject>>::fail(rows.take_error());
    }
    auto& values = rows.value();
    if (values.empty()) {
        return core::Result<std::optional<core::JsonObject>>::ok(std::nullopt);
    }
    // Re-query for column names via a light parse of the first row.
    core::Error error;
    ScopedStmt stmt(handle_, sql, &error);
    if (!stmt) {
        return core::Result<std::optional<core::JsonObject>>::fail(error);
    }
    core::JsonObject obj;
    const int columns = sqlite3_column_count(stmt.get());
    for (int c = 0; c < columns; ++c) {
        obj[column_name(stmt.get(), c)] = values[0][static_cast<std::size_t>(c)];
    }
    return core::Result<std::optional<core::JsonObject>>::ok(std::move(obj));
}

std::int64_t Database::last_insert_rowid() const {
    return handle_ != nullptr ? sqlite3_last_insert_rowid(handle_) : -1;
}

// ------------------------------------------------------------------ migrations

core::Result<int> Migrations::current_version(Database& db) {
    auto row = db.query_one("PRAGMA user_version;");
    if (row.is_error()) return core::Result<int>::fail(row.take_error());
    if (!row.value().has_value()) return core::Result<int>::ok(0);
    return core::Result<int>::ok(
        static_cast<int>(row.value()->at("user_version").as_int()));
}

core::Status Migrations::apply(Database& db, const std::vector<Step>& steps) {
    if (core::Status ensure = db.execute(
            "CREATE TABLE IF NOT EXISTS schema_migrations ("
            "version INTEGER PRIMARY KEY, name TEXT NOT NULL, applied_at TEXT NOT NULL);");
        !ensure.is_ok()) {
        return ensure;
    }

    auto version_result = current_version(db);
    if (version_result.is_error()) return core::Status::fail(version_result.take_error());
    int version = version_result.value();

    for (const Step& step : steps) {
        if (step.version <= version) continue;

        if (core::Status begin = db.execute("BEGIN IMMEDIATE;"); !begin.is_ok()) return begin;

        if (core::Status script = db.execute(step.sql); !script.is_ok()) {
            db.execute("ROLLBACK;");
            return core::Status::fail(core::Error(
                core::ErrorCode::DatabaseError,
                std::string("migration ") + std::to_string(step.version) + " (" + step.name +
                    ") failed: " + script.error().message()));
        }

        const std::string stamp_sql =
            "INSERT INTO schema_migrations (version, name, applied_at) VALUES (?, ?, ?);";
        if (auto insert = db.run(stamp_sql,
                                 {core::Json(step.version), core::Json(step.name),
                                  core::Json(core::iso_utc_now())});
            insert.is_error()) {
            db.execute("ROLLBACK;");
            return core::Status::fail(insert.take_error());
        }

        const std::string pragma = "PRAGMA user_version = " + std::to_string(step.version) + ";";
        if (core::Status set_version = db.execute(pragma); !set_version.is_ok()) {
            db.execute("ROLLBACK;");
            return set_version;
        }

        if (core::Status commit = db.execute("COMMIT;"); !commit.is_ok()) return commit;

        version = step.version;
    }
    return core::Status::ok();
}

}  // namespace trinity::db
