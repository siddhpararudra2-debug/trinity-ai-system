#include "trinity/core/Config.hpp"

#include <cstdlib>

namespace trinity::core {

namespace {

std::string getenvOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return std::string(value);
}

}  // namespace

Settings loadSettings() {
    Settings settings;
    // Zero-setup default: state lives in ./data next to the working
    // directory, exactly like the Python backend's repo-root default.
    settings.storageRoot = getenvOr("TRINITY_STORAGE_ROOT", "./data");
    settings.repoRoot = ".";

    const std::string& root = settings.storageRoot;
    settings.dbPath = getenvOr("TRINITY_DB_PATH", root + "/trinity.db");
    settings.projectsDir = root + "/projects";
    settings.artifactsDir = root + "/artifacts";
    settings.jobsDir = root + "/jobs";
    settings.cadDir = root + "/cad";
    settings.exportsDir = root + "/exports";
    settings.tempDir = root + "/temp";
    settings.cacheDir = root + "/cache";
    settings.embeddingsDir = root + "/embeddings";
    settings.evaluationDir = root + "/evaluation";
    return settings;
}

std::vector<std::string> Settings::allStorageDirs() const {
    return {projectsDir,  artifactsDir, jobsDir,       cadDir,      exportsDir,
            tempDir,      cacheDir,     embeddingsDir, evaluationDir};
}

}  // namespace trinity::core
