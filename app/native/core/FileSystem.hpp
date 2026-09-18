// Trinity — safe filesystem helpers.
//
// Security posture (brief §Security):
//  - every user-supplied or engine-supplied path is normalised and checked
//    against its allowed root before use (no traversal escapes),
//  - temporary files are created inside Trinity's own temp dir with
//    unpredictable names,
//  - no path is ever constructed from raw user text without validation.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trinity::core {

class FileSystem {
public:
    // Lexically normalises a path (forward slashes, collapse ".", "..").
    static std::filesystem::path normalise(const std::string& raw);

    // True when `candidate` (already normalised) is inside `root` or equals it.
    static bool is_within(const std::filesystem::path& root,
                          const std::filesystem::path& candidate);

    // Validate a path against an allowed root; returns the normalised path or
    // nullopt on traversal/syntax failure.
    static std::optional<std::filesystem::path> validate_under(const std::string& root,
                                                               const std::string& candidate);

    // ensure_directory / remove_all with error_code out-params (no throws).
    static bool ensure_directory(const std::string& path, std::error_code& ec);
    static bool ensure_parent_directories(const std::string& file_path, std::error_code& ec);
    static bool remove_all(const std::string& path, std::error_code& ec);

    // Read a small file fully; nullopt on failure.
    static std::optional<std::string> read_file(const std::string& path);
    // Write a file atomically (write to .tmp sibling, then rename).
    static bool write_file_atomic(const std::string& path, std::string_view content,
                                  std::error_code& ec);

    // Create a unique directory under `base` with a `prefix`; returns "" on
    // failure. Caller owns removal.
    static std::string make_temp_directory(const std::string& base, const std::string& prefix);

    // Human-readable size of a file; 0 when missing.
    static std::uintmax_t file_size(const std::string& path);

    // List immediate children (files only) of a directory, sorted by name.
    static std::vector<std::string> list_files(const std::string& directory);
};

}  // namespace trinity::core
