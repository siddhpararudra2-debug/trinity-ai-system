#pragma once

// Application configuration. Single source of truth for paths and
// runtime settings. Mirrors src/core/config.py: everything resolves
// relative to a storage root so the system runs with zero setup.
// TRINITY_STORAGE_ROOT and TRINITY_DB_PATH environment variables
// override the defaults.

#include <string>
#include <vector>

namespace trinity::core {

struct Settings {
    std::string repoRoot;
    std::string storageRoot;
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

    std::string apiTitle = "Trinity AI — Engineering Operating System";
    std::string apiVersion = "1.0.0";

    std::vector<std::string> allStorageDirs() const;
};

Settings loadSettings();

}  // namespace trinity::core
