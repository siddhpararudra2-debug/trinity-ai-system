#pragma once

// Application configuration. Single source of truth for paths and
// runtime settings. Mirrors src/core/config.py: everything resolves
// relative to a storage root so the system runs with zero setup.
// TRINITY_STORAGE_ROOT, TRINITY_DB_PATH, TRINITY_LOG_DIR and
// TRINITY_MODE environment variables override the defaults. When no
// override is set on Windows, state lives under %LOCALAPPDATA%/Trinity.

#include <string>
#include <vector>

#include "Json.hpp"

namespace trinity::core {

/// Model provider selection. The API key is deliberately NOT a member:
/// secrets come only from the TRINITY_MODEL_API_KEY environment variable,
/// read at configure() time, and are never stored, logged, or persisted.
struct ModelSettings {
    std::string providerId = "null";  // TRINITY_MODEL_PROVIDER
    std::string endpoint;             // TRINITY_MODEL_ENDPOINT
    std::string modelName;            // TRINITY_MODEL_NAME
    int timeoutMs = 30000;            // TRINITY_MODEL_TIMEOUT_MS

    /// Config handed to the provider factory. Never contains secrets.
    Json toJson() const;
};

/// Reads the API key from the environment. Returns empty when unset.
/// The key must never be written to logs, JSON, or the database.
std::string modelApiKeyFromEnv();

struct Settings {
    std::string appName = "Trinity";
    std::string appVersion = "0.1.0";
    std::string buildMode = "development";  // development | release

    std::string repoRoot;
    std::string storageRoot;
    std::string dataDir;  // == storageRoot (kept as explicit alias for task §5)
    std::string dbPath;

    std::string projectsDir;
    std::string artifactsDir;
    std::string jobsDir;
    std::string cadDir;
    std::string exportsDir;
    std::string tempDir;
    std::string cacheDir;
    std::string embeddingsDir;
    std::string evaluationDir;
    std::string logDir;

    std::string apiTitle = "Trinity AI — Engineering Operating System";
    std::string apiVersion = "1.0.0";

    ModelSettings model;

    bool isRelease() const noexcept { return buildMode == "release"; }

    std::vector<std::string> allStorageDirs() const;
    std::string logFilePath() const;
    Json toJson() const;
};

/// Default per-user storage root, e.g. %LOCALAPPDATA%/Trinity on Windows.
std::string defaultUserStorageRoot();
/// Default database path for a given storage root.
std::string defaultDatabasePath(const std::string& storageRoot);
/// Default log directory for a given storage root.
std::string defaultLogDir(const std::string& storageRoot);

Settings loadSettings();

}  // namespace trinity::core
