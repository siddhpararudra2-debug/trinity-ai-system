// Trinity — database layer tests.
#include <filesystem>
#include <string>

#include <doctest/doctest.h>

#include "../db/Schema.hpp"

using namespace trinity;

namespace {
std::string fresh_db_path() {
    static int counter = 0;
    const std::string path = std::filesystem::temp_directory_path().string() +
                             "/trinity_test_db_" + std::to_string(++counter) + ".db";
    std::filesystem::remove(path);
    return path;
}
}  // namespace

TEST_CASE("migrations create full schema and are idempotent") {
    const std::string path = fresh_db_path();
    {
        auto opened = db::open_and_migrate(path);
        REQUIRE(opened.is_ok());
        db::Database db = std::move(opened.value());

        auto version = db::Migrations::current_version(db);
        REQUIRE(version.is_ok());
        CHECK(version.value() >= 1);

        // All core tables exist.
        for (const char* table : {"projects", "jobs", "artifacts", "validations",
                                  "workflows", "logs", "engine_meta", "settings",
                                  "cache_entries", "schema_migrations"}) {
            auto row = db.query_one(std::string("SELECT count(*) as n FROM ") + table + ";");
            REQUIRE(row.is_ok());
        }
    }
    {
        // Re-open: idempotent, no error, version preserved.
        auto reopened = db::open_and_migrate(path);
        REQUIRE(reopened.is_ok());
        auto version = db::Migrations::current_version(reopened.value());
        REQUIRE(version.is_ok());
        CHECK(version.value() >= 1);
    }
    std::filesystem::remove(path);
}

TEST_CASE("crud round trip on projects and settings") {
    auto opened = db::open_and_migrate(fresh_db_path());
    REQUIRE(opened.is_ok());
    db::Database db = std::move(opened.value());

    REQUIRE(db.run("INSERT INTO projects (project_id, name, description, workspace, created_at, "
                   "updated_at) VALUES ('p1', 'Drone', 'desc', '/tmp/w', '2026-01-01', "
                   "'2026-01-01');")
                .is_ok());
    auto row = db.query_one("SELECT name FROM projects WHERE project_id = 'p1';");
    REQUIRE(row.is_ok());
    REQUIRE(row.value().has_value());
    CHECK(row.value()->at("name").as_string() == "Drone");

    REQUIRE(db.run("INSERT INTO settings (key, value, updated_at) VALUES ('k', 'v', 'now');")
                .is_ok());
    auto setting = db.query_one("SELECT value FROM settings WHERE key = 'k';");
    CHECK(setting.value()->at("value").as_string() == "v");
}

TEST_CASE("parameter binding handles nulls numbers and text") {
    auto opened = db::open_and_migrate(fresh_db_path());
    REQUIRE(opened.is_ok());
    db::Database db = std::move(opened.value());

    REQUIRE(db.run("INSERT INTO jobs (job_id, engine, operation, status, request, created_at, "
                   "updated_at) VALUES (?, ?, ?, ?, ?, ?, ?);",
                   {core::Json("j1"), core::Json("cad"), core::Json("generate"),
                    core::Json("QUEUED"), core::Json(nullptr), core::Json("t"), core::Json("t")})
                .is_ok());

    auto row = db.query_one("SELECT engine, operation, request FROM jobs WHERE job_id = ?;",
                            {core::Json("j1")});
    REQUIRE(row.is_ok());
    CHECK(row.value()->at("engine").as_string() == "cad");
    CHECK(row.value()->at("request").is_null());
}
