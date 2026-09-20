#include "trinity/core/Config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace trinity::core {

namespace {

std::string getenvOr(const char* name, const std::string& fallback) {
#ifdef _WIN32
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr || value[0] == '\0') {
        if (value != nullptr) {
            free(value);
        }
        return fallback;
    }
    std::string out(value);
    free(value);
    return out;
#else
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return std::string(value);
#endif
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

int getenvIntOr(const char* name, int fallback) {
    const std::string raw = getenvOr(name, "");
    if (raw.empty()) {
        return fallback;
    }
    try {
        return std::stoi(raw);
    } catch (...) {
        return fallback;
    }
}

}  // namespace

std::string modelApiKeyFromEnv() {
    return getenvOr("TRINITY_MODEL_API_KEY", "");
}

std::string defaultUserStorageRoot() {
#ifdef _WIN32
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) &&
        raw != nullptr) {
        char narrow[MAX_PATH * 4] = {0};
        WideCharToMultiByte(CP_UTF8, 0, raw, -1, narrow, sizeof(narrow) - 1, nullptr,
                            nullptr);
        CoTaskMemFree(raw);
        std::string base(narrow);
        if (!base.empty()) {
            return base + "\\Trinity";
        }
    }
    const std::string localAppData = getenvOr("LOCALAPPDATA", "");
    if (!localAppData.empty()) {
        return localAppData + "\\Trinity";
    }
#endif
    // Zero-setup fallback (also the Python backend's repo-root default).
    return "./data";
}

std::string defaultDatabasePath(const std::string& storageRoot) {
    const bool windowsSep = storageRoot.find('\\') != std::string::npos;
    return storageRoot + (windowsSep ? "\\trinity.db" : "/trinity.db");
}

std::string defaultLogDir(const std::string& storageRoot) {
    const bool windowsSep = storageRoot.find('\\') != std::string::npos;
    return storageRoot + (windowsSep ? "\\logs" : "/logs");
}

Settings loadSettings() {
    Settings settings;
    const std::string fallbackRoot = defaultUserStorageRoot();
    settings.storageRoot = getenvOr("TRINITY_STORAGE_ROOT", fallbackRoot);
    settings.dataDir = settings.storageRoot;
    settings.repoRoot = ".";

    const std::string& root = settings.storageRoot;
    settings.dbPath = getenvOr("TRINITY_DB_PATH", defaultDatabasePath(root));
    settings.projectsDir = root + (root.find('\\') != std::string::npos ? "\\" : "/") +
                           "projects";
    const std::string sep = root.find('\\') != std::string::npos ? "\\" : "/";
    settings.artifactsDir = root + sep + "artifacts";
    settings.jobsDir = root + sep + "jobs";
    settings.cadDir = root + sep + "cad";
    settings.exportsDir = root + sep + "exports";
    settings.tempDir = root + sep + "temp";
    settings.cacheDir = root + sep + "cache";
    settings.embeddingsDir = root + sep + "embeddings";
    settings.evaluationDir = root + sep + "evaluation";
    settings.logDir = getenvOr("TRINITY_LOG_DIR", defaultLogDir(root));

    std::string mode = lowerCopy(getenvOr("TRINITY_MODE", ""));
    if (mode.empty()) {
#ifdef NDEBUG
        mode = "release";
#else
        mode = "development";
#endif
    }
    settings.buildMode = (mode == "release" || mode == "prod" || mode == "production")
                             ? "release"
                             : "development";

    settings.model.providerId = getenvOr("TRINITY_MODEL_PROVIDER", "null");
    settings.model.endpoint = getenvOr("TRINITY_MODEL_ENDPOINT", "");
    settings.model.modelName = getenvOr("TRINITY_MODEL_NAME", "");
    settings.model.timeoutMs = getenvIntOr("TRINITY_MODEL_TIMEOUT_MS", 30000);
    return settings;
}

std::vector<std::string> Settings::allStorageDirs() const {
    return {projectsDir,  artifactsDir, jobsDir,       cadDir,      exportsDir,
            tempDir,      cacheDir,     embeddingsDir, evaluationDir, logDir};
}

std::string Settings::logFilePath() const {
    const bool windowsSep = logDir.find('\\') != std::string::npos;
    return logDir + (windowsSep ? "\\trinity.log" : "/trinity.log");
}

Json ModelSettings::toJson() const {
    // NOTE: no api_key member exists by design — see modelApiKeyFromEnv().
    return Json{{"provider", providerId},
                {"endpoint", endpoint},
                {"model", modelName},
                {"timeout_ms", timeoutMs}};
}

Json Settings::toJson() const {
    return Json{{"app_name", appName},
                {"app_version", appVersion},
                {"build_mode", buildMode},
                {"storage_root", storageRoot},
                {"data_dir", dataDir},
                {"db_path", dbPath},
                {"artifacts_dir", artifactsDir},
                {"log_dir", logDir},
                {"log_file", logFilePath()},
                {"model", model.toJson()}};
}

}  // namespace trinity::core
