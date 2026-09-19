#pragma once

// Application path resolution: executable directory, storage root,
// database path. All paths derive from Settings so there is exactly
// one place that decides where state lives.

#include <string>

#include "Config.hpp"

namespace trinity::core {

std::string executableDir();
std::string currentDir();

// Ensure every directory in settings.allStorageDirs() exists.
// Idempotent: missing directories are created, existing ones kept.
void ensureStorageLayout(const Settings& settings);

}  // namespace trinity::core
