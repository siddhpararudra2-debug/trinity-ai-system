#pragma once

// SHA-256 file checksum (Windows CNG). Shared so engines/artifacts
// never reimplement hashing.

#include <string>

namespace trinity::artifacts {

std::string sha256File(const std::string& path);

}  // namespace trinity::artifacts
