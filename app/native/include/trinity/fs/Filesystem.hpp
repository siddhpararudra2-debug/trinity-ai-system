#pragma once

// Reusable filesystem service, independent from Qt UI code.
// Safe path handling, directory creation, existence checks,
// file read/write, metadata and directory operations.

#include <cstdint>
#include <string>
#include <vector>

#include "../core/Error.hpp"
#include "../core/Json.hpp"
#include "../core/Result.hpp"

namespace trinity::fs {

struct FileMetadata {
    bool exists = false;
    bool isFile = false;
    bool isDir = false;
    std::uint64_t sizeBytes = 0;
    std::string lastModified;  // UTC ISO-8601, empty when unknown

    core::Json toJson() const;
};

struct DirEntry {
    std::string name;
    std::string path;
    bool isDir = false;

    core::Json toJson() const;
};

class Filesystem {
public:
    Filesystem() = default;

    bool exists(const std::string& path) const noexcept;
    bool isFile(const std::string& path) const noexcept;
    bool isDirectory(const std::string& path) const noexcept;

    core::Status createDirs(const std::string& path);
    core::Status writeFile(const std::string& path, const std::string& content);
    core::Status writeBinary(const std::string& path, const void* data, size_t size);
    core::Result<std::string> readFile(const std::string& path) const;
    core::Status remove(const std::string& path);
    core::Status removeAll(const std::string& path);
    core::Status copyFile(const std::string& from, const std::string& to);

    FileMetadata metadata(const std::string& path) const noexcept;
    core::Result<std::vector<DirEntry>> listDir(const std::string& path) const;

    /// Join root + untrusted relative path, rejecting absolute paths and
    /// ".." traversal that escapes root. Returns canonical joined path.
    core::Result<std::string> safeJoin(const std::string& root,
                                       const std::string& untrusted) const;
};

}  // namespace trinity::fs
