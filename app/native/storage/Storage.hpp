// Trinity — storage abstraction (brief §/storage, §Local filesystem).
//
// Two concerns live here:
//
//   IStorage / LocalFileStorage
//       Every read and write in the application goes through a storage rooted
//       at one directory. Relative paths are validated against that root, so a
//       traversal attempt ("../../etc/passwd", an engine emitting "../..", a
//       plugin manifest pointing outside its folder) fails with a classified
//       error instead of touching the filesystem.
//
//   StorageLayout
//       The resolved Trinity data layout. It is computed from an injected
//       IEnvironment (platform/Platform.hpp), never from hardcoded user paths,
//       so the same code serves a normal install, a portable install and a
//       hermetic test run.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "../core/Error.hpp"
#include "../core/Json.hpp"
#include "../platform/Platform.hpp"

namespace trinity::storage {

// A rooted view of a filesystem directory. Implementations must reject any
// path that escapes the root.
class IStorage {
public:
    virtual ~IStorage() = default;

    virtual std::string root() const = 0;

    // Absolute path for a (validated) relative path. Returns "" when the
    // relative path escapes the root.
    virtual std::string absolute(const std::string& relative) const = 0;

    virtual bool exists(const std::string& relative) const = 0;

    virtual bool ensure_directory(const std::string& relative, std::error_code& ec) = 0;

    virtual std::optional<std::string> read_text(const std::string& relative) const = 0;

    // Atomic write (temp file + rename); the parent directory is created.
    virtual bool write_text_atomic(const std::string& relative, std::string_view content,
                                   std::error_code& ec) = 0;

    // Copies an external file into the root. Returns false when the source is
    // missing/unreadable or the destination escapes the root.
    virtual bool copy_in(const std::string& source_file, const std::string& relative_destination,
                         std::error_code& ec) = 0;

    virtual bool remove_all(const std::string& relative, std::error_code& ec) = 0;

    virtual std::vector<std::string> list_files(const std::string& relative_directory) const = 0;
    virtual std::vector<std::string> list_directories(const std::string& relative_directory) const = 0;

    virtual std::uintmax_t size(const std::string& relative) const = 0;
};

// Real filesystem implementation. The root is created on first use.
class LocalFileStorage : public IStorage {
public:
    explicit LocalFileStorage(std::string root);

    std::string root() const override { return root_; }
    std::string absolute(const std::string& relative) const override;
    bool exists(const std::string& relative) const override;
    bool ensure_directory(const std::string& relative, std::error_code& ec) override;
    std::optional<std::string> read_text(const std::string& relative) const override;
    bool write_text_atomic(const std::string& relative, std::string_view content,
                           std::error_code& ec) override;
    bool copy_in(const std::string& source_file, const std::string& relative_destination,
                 std::error_code& ec) override;
    bool remove_all(const std::string& relative, std::error_code& ec) override;
    std::vector<std::string> list_files(const std::string& relative_directory) const override;
    std::vector<std::string> list_directories(const std::string& relative_directory) const override;
    std::uintmax_t size(const std::string& relative) const override;

private:
    // Resolves `relative` under the root; nullopt on traversal/syntax failure.
    std::optional<std::filesystem::path> resolve(const std::string& relative) const;

    std::string root_;
};

// The resolved Trinity data layout (brief §Local filesystem).
//
//   <data>/
//     trinity.db          database_file
//     artifacts/          artifacts_dir
//     logs/trinity.log    log_file
//     tmp/                temp_dir
//     plugins/            plugins_dir
//   <documents>/Trinity Projects   projects_dir
struct StorageLayout {
    std::string data_dir;
    std::string projects_dir;
    std::string artifacts_dir;
    std::string logs_dir;
    std::string temp_dir;
    std::string plugins_dir;
    std::string database_file;
    std::string log_file;
    // Bundled Python engine host script; "" when it is not present.
    std::string engine_host_script;

    // Resolves the layout from an environment. `executable_dir` (optional)
    // locates the bundled engine host next to the binary.
    static StorageLayout resolve(const platform::IEnvironment& environment,
                                 const std::string& executable_dir = std::string());

    // Same, but with explicit overrides (used by ApplicationConfig).
    static StorageLayout resolve_with_overrides(const platform::IEnvironment& environment,
                                                const std::string& data_dir_override,
                                                const std::string& projects_dir_override,
                                                const std::string& executable_dir = std::string());

    // Creates every directory in the layout; idempotent.
    bool ensure_directories(std::error_code& ec) const;

    core::Json to_json() const;
};

}  // namespace trinity::storage
