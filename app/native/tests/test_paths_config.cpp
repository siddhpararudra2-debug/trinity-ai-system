#include <doctest.h>

#include <cstdlib>
#include <filesystem>

#include "trinity/core/Config.hpp"
#include "trinity/core/Paths.hpp"

#ifdef _WIN32
#define TRINITY_SETENV(name, value) _putenv_s(name, value)
#else
#define TRINITY_SETENV(name, value) setenv(name, value, 1)
#endif

TEST_CASE("settings honor environment overrides") {
    const std::string root =
        (std::filesystem::temp_directory_path() / "trinity-test-cfg").string();
    TRINITY_SETENV("TRINITY_STORAGE_ROOT", root.c_str());
    TRINITY_SETENV("TRINITY_DB_PATH", (root + "/custom.db").c_str());

    const trinity::core::Settings settings = trinity::core::loadSettings();
    CHECK(settings.storageRoot == root);
    CHECK(settings.dbPath == root + "/custom.db");
    CHECK(settings.artifactsDir == root + "/artifacts");
    CHECK(settings.allStorageDirs().size() == 9);

    TRINITY_SETENV("TRINITY_STORAGE_ROOT", "");
    TRINITY_SETENV("TRINITY_DB_PATH", "");
}

TEST_CASE("storage layout creation is idempotent") {
    const std::string root =
        (std::filesystem::temp_directory_path() / "trinity-test-layout").string();
    TRINITY_SETENV("TRINITY_STORAGE_ROOT", root.c_str());
    const trinity::core::Settings settings = trinity::core::loadSettings();
    CHECK_NOTHROW(trinity::core::ensureStorageLayout(settings));
    CHECK_NOTHROW(trinity::core::ensureStorageLayout(settings));
    CHECK(std::filesystem::is_directory(settings.artifactsDir));
    TRINITY_SETENV("TRINITY_STORAGE_ROOT", "");
    std::filesystem::remove_all(root);
}
