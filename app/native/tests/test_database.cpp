#include <doctest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "trinity/storage/Database.hpp"

namespace {

void removeQuietly(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

}  // namespace

TEST_CASE("database initializes the five V1 tables") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "trinity-test-db";
    removeQuietly(dir);
    {
        trinity::storage::Database db((dir / "trinity.db").string());
        db.init();

        const auto tables = db.query(
            "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name;");
        std::vector<std::string> names;
        for (const auto& row : tables) {
            names.push_back(row[0]);
        }
        for (const char* expected : {"jobs", "artifacts", "validations", "cache_entries",
                                     "projects"}) {
            CHECK(std::find(names.begin(), names.end(), expected) != names.end());
        }
    }
    removeQuietly(dir);
}

TEST_CASE("database round-trips a job row") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "trinity-test-db2";
    removeQuietly(dir);
    {
        trinity::storage::Database db((dir / "trinity.db").string());
        db.init();
        db.exec("INSERT INTO jobs (job_id, engine, operation, status, progress, request, "
                "created_at, updated_at) VALUES ('j1', 'math', 'solve', 'queued', 0.0, "
                "'{}', 't', 't');");
        const auto rows = db.query("SELECT job_id, status FROM jobs;");
        REQUIRE(rows.size() == 1);
        CHECK(rows[0][0] == "j1");
        CHECK(rows[0][1] == "queued");
    }
    removeQuietly(dir);
}
