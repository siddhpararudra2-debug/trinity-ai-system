#include "trinity/core/Paths.hpp"

#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace trinity::core {
namespace fs = std::filesystem;

std::string executableDir() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return currentDir();
    }
    return fs::path(buffer).parent_path().string();
#else
    return currentDir();
#endif
}

std::string currentDir() {
    std::error_code ec;
    const std::string dir = fs::current_path(ec).string();
    return ec ? std::string(".") : dir;
}

void ensureStorageLayout(const Settings& settings) {
    std::error_code ec;
    for (const std::string& dir : settings.allStorageDirs()) {
        fs::create_directories(dir, ec);
        ec.clear();
    }
}

}  // namespace trinity::core
