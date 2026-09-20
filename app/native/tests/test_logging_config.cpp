#include <doctest.h>

#include <cstdlib>
#include <filesystem>

#include "trinity/core/Config.hpp"
#include "trinity/core/Error.hpp"
#include "trinity/core/Logger.hpp"

#ifdef _WIN32
#define TRINITY_SETENV(name, value) _putenv_s(name, value)
#else
#define TRINITY_SETENV(name, value) setenv(name, value, 1)
#endif

TEST_CASE("configuration exposes app identity and windows-safe paths") {
    const std::string root =
        (std::filesystem::temp_directory_path() / "trinity-test-logcfg").string();
    TRINITY_SETENV("TRINITY_STORAGE_ROOT", root.c_str());
    TRINITY_SETENV("TRINITY_DB_PATH", (root + "/custom.db").c_str());
    TRINITY_SETENV("TRINITY_MODE", "release");

    const trinity::core::Settings settings = trinity::core::loadSettings();
    CHECK(settings.appName == "Trinity");
    CHECK_FALSE(settings.appVersion.empty());
    CHECK(settings.buildMode == "release");
    CHECK(settings.isRelease());
    CHECK(settings.dataDir == settings.storageRoot);
    CHECK(settings.dbPath == root + "/custom.db");
    CHECK_FALSE(settings.logDir.empty());
    CHECK(settings.logFilePath().find("trinity.log") != std::string::npos);
    CHECK_FALSE(settings.toJson()["app_name"].get<std::string>().empty());

    TRINITY_SETENV("TRINITY_STORAGE_ROOT", "");
    TRINITY_SETENV("TRINITY_DB_PATH", "");
    TRINITY_SETENV("TRINITY_MODE", "");
}

TEST_CASE("logger writes to file and exposes recent entries") {
    const std::string root =
        (std::filesystem::temp_directory_path() / "trinity-test-logging").string();
    std::filesystem::remove_all(root);
    const std::string logFile = root + "/logs/trinity.log";

    auto& logger = trinity::core::Logger::instance();
    const std::string previous = logger.logFilePath();
    logger.setLogFile(logFile);
    logger.clearBuffer();
    logger.info("test", "hello-file", trinity::core::Json{{"n", 1}});
    logger.warning("test", "warn-entry");
    logger.error("test", "error-entry");
    logger.debug("test", "debug-filtered-or-not");

    const auto recent = logger.recent(10);
    CHECK(recent.size() >= 3);
    bool sawHello = false;
    for (const auto& record : recent) {
        if (record.value("message", "") == "hello-file") {
            sawHello = true;
            CHECK(record.value("level", "") == "INFO");
        }
    }
    CHECK(sawHello);
    CHECK(std::filesystem::exists(logFile));
    CHECK(logger.logFilePath() == logFile);

    logger.setLogFile(previous);
    logger.clearBuffer();
    std::filesystem::remove_all(root);
}

TEST_CASE("error handling carries source and timestamp") {
    const auto info = trinity::core::makeError(
        trinity::core::ErrorCode::JobNotFoundError, "no job", "jobs");
    CHECK(info.source == "jobs");
    CHECK_FALSE(info.timestamp.empty());
    const auto json = info.toJson();
    CHECK(json["code"] == "job_not_found");

    trinity::core::JobNotFoundError err("gone", {}, "storage");
    CHECK(err.source() == "storage");
    CHECK_FALSE(err.timestamp().empty());
    CHECK(err.info().code == trinity::core::ErrorCode::JobNotFoundError);
}
