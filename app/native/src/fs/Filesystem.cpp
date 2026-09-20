#include "trinity/fs/Filesystem.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace trinity::fs {
namespace filesystem = std::filesystem;

namespace {

std::string toIsoUtc(std::filesystem::file_time_type tp) {
    try {
        const auto sctp =
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                tp - std::filesystem::file_time_type::clock::now() +
                std::chrono::system_clock::now());
        const std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &tt);
#else
        gmtime_r(&tt, &tm);
#endif
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d+00:00",
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                      tm.tm_sec);
        return std::string(buffer);
    } catch (...) {
        return "";
    }
}

}  // namespace

core::Json FileMetadata::toJson() const {
    return core::Json{{"exists", exists},
                      {"is_file", isFile},
                      {"is_dir", isDir},
                      {"size_bytes", sizeBytes},
                      {"last_modified", lastModified}};
}

core::Json DirEntry::toJson() const {
    return core::Json{{"name", name}, {"path", path}, {"is_dir", isDir}};
}

bool Filesystem::exists(const std::string& path) const noexcept {
    std::error_code ec;
    return filesystem::exists(path, ec) && !ec;
}

bool Filesystem::isFile(const std::string& path) const noexcept {
    std::error_code ec;
    return filesystem::is_regular_file(path, ec) && !ec;
}

bool Filesystem::isDirectory(const std::string& path) const noexcept {
    std::error_code ec;
    return filesystem::is_directory(path, ec) && !ec;
}

core::Status Filesystem::createDirs(const std::string& path) {
    std::error_code ec;
    filesystem::create_directories(path, ec);
    if (ec) {
        return core::Status::fail(core::makeError(core::ErrorCode::TrinityError,
                                                  "Cannot create directory: " + ec.message(),
                                                  "filesystem",
                                                  {{"path", path}}));
    }
    return core::okStatus();
}

core::Status Filesystem::writeFile(const std::string& path, const std::string& content) {
    return writeBinary(path, content.data(), content.size());
}

core::Status Filesystem::writeBinary(const std::string& path, const void* data,
                                     size_t size) {
    std::error_code ec;
    filesystem::create_directories(filesystem::path(path).parent_path(), ec);
    ec.clear();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return core::Status::fail(core::makeError(core::ErrorCode::TrinityError,
                                                  "Cannot open file for writing",
                                                  "filesystem", {{"path", path}}));
    }
    if (size > 0) {
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!out) {
            return core::Status::fail(core::makeError(core::ErrorCode::TrinityError,
                                                      "Failed while writing file",
                                                      "filesystem", {{"path", path}}));
        }
    }
    return core::okStatus();
}

core::Result<std::string> Filesystem::readFile(const std::string& path) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return core::Result<std::string>::fail(core::makeError(
            core::ErrorCode::ArtifactNotFoundError, "Cannot open file for reading",
            "filesystem", {{"path", path}}));
    }
    std::ostringstream stream;
    stream << in.rdbuf();
    return core::Result<std::string>::ok(stream.str());
}

core::Status Filesystem::remove(const std::string& path) {
    std::error_code ec;
    filesystem::remove(path, ec);
    if (ec) {
        return core::Status::fail(core::makeError(core::ErrorCode::TrinityError,
                                                  "Cannot remove file: " + ec.message(),
                                                  "filesystem", {{"path", path}}));
    }
    return core::okStatus();
}

core::Status Filesystem::removeAll(const std::string& path) {
    std::error_code ec;
    filesystem::remove_all(path, ec);
    if (ec) {
        return core::Status::fail(core::makeError(core::ErrorCode::TrinityError,
                                                  "Cannot remove path: " + ec.message(),
                                                  "filesystem", {{"path", path}}));
    }
    return core::okStatus();
}

core::Status Filesystem::copyFile(const std::string& from, const std::string& to) {
    std::error_code ec;
    filesystem::create_directories(filesystem::path(to).parent_path(), ec);
    ec.clear();
    filesystem::copy_file(from, to, filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return core::Status::fail(core::makeError(core::ErrorCode::TrinityError,
                                                  "Cannot copy file: " + ec.message(),
                                                  "filesystem",
                                                  {{"from", from}, {"to", to}}));
    }
    return core::okStatus();
}

FileMetadata Filesystem::metadata(const std::string& path) const noexcept {
    FileMetadata meta;
    try {
        std::error_code ec;
        meta.exists = filesystem::exists(path, ec) && !ec;
        if (!meta.exists) {
            return meta;
        }
        meta.isFile = filesystem::is_regular_file(path, ec) && !ec;
        meta.isDir = filesystem::is_directory(path, ec) && !ec;
        if (meta.isFile) {
            meta.sizeBytes =
                static_cast<std::uint64_t>(filesystem::file_size(path, ec));
        }
        meta.lastModified = toIsoUtc(filesystem::last_write_time(path, ec));
    } catch (...) {
    }
    return meta;
}

core::Result<std::vector<DirEntry>> Filesystem::listDir(const std::string& path) const {
    std::error_code ec;
    if (!filesystem::is_directory(path, ec) || ec) {
        return core::Result<std::vector<DirEntry>>::fail(core::makeError(
            core::ErrorCode::TrinityError, "Not a directory", "filesystem",
            {{"path", path}}));
    }
    std::vector<DirEntry> entries;
    for (filesystem::directory_iterator it(path, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        DirEntry entry;
        entry.path = it->path().string();
        entry.name = it->path().filename().string();
        entry.isDir = it->is_directory(ec) && !ec;
        entries.push_back(std::move(entry));
    }
    if (ec) {
        return core::Result<std::vector<DirEntry>>::fail(core::makeError(
            core::ErrorCode::TrinityError, "Failed to list directory: " + ec.message(),
            "filesystem", {{"path", path}}));
    }
    return core::Result<std::vector<DirEntry>>::ok(std::move(entries));
}

core::Result<std::string> Filesystem::safeJoin(const std::string& root,
                                               const std::string& untrusted) const {
    try {
        filesystem::path rel(untrusted);
        if (rel.is_absolute()) {
            return core::Result<std::string>::fail(core::makeError(
                core::ErrorCode::RequestValidationError, "Absolute paths are not allowed",
                "filesystem", {{"path", untrusted}}));
        }
        filesystem::path joined = filesystem::path(root) / rel;
        filesystem::path normal = joined.lexically_normal();
        filesystem::path base = filesystem::path(root).lexically_normal();
        // Ensure normalized join stays within base.
        auto baseIt = base.begin();
        auto normIt = normal.begin();
        for (; baseIt != base.end(); ++baseIt, ++normIt) {
            if (normIt == normal.end() || *baseIt != *normIt) {
                return core::Result<std::string>::fail(core::makeError(
                    core::ErrorCode::RequestValidationError,
                    "Path escapes storage root", "filesystem",
                    {{"path", untrusted}}));
            }
        }
        return core::Result<std::string>::ok(normal.string());
    } catch (const std::exception& exc) {
        return core::Result<std::string>::fail(
            core::makeError(core::ErrorCode::RequestValidationError,
                            std::string("Invalid path: ") + exc.what(), "filesystem",
                            {{"path", untrusted}}));
    }
}

}  // namespace trinity::fs
