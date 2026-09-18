// Trinity — path resolution for the desktop data layout.
//
//   Data dir:        %LOCALAPPDATA%/Trinity          (env override TRINITY_DATA_DIR)
//   Projects dir:    Documents/Trinity Projects      (user-configurable workspace)
//   Logs dir:        <data>/logs
//   DB:              <data>/trinity.db
//   Artifacts:       <data>/artifacts
//
// No user-specific path is ever hardcoded: everything resolves from the
// environment (Windows shell API not required for V1 because LOCALAPPDATA and
// USERPROFILE are always present on Windows and the variables keep the code
// testable on any platform).
#pragma once

#include <optional>
#include <string>

namespace trinity::core {

struct Paths {
    // Root of all mutable Trinity data. Default: %LOCALAPPDATA%/Trinity,
    // overridable with TRINITY_DATA_DIR (used by tests and portable installs).
    static std::string data_dir();

    // Default workspace root for user projects: <Documents>/Trinity Projects.
    // Overridable with TRINITY_PROJECTS_DIR; users may change it in Settings.
    static std::string projects_root();

    static std::string database_file();     // <data>/trinity.db
    static std::string artifacts_dir();     // <data>/artifacts
    static std::string logs_dir();          // <data>/logs
    static std::string main_log_file();     // <data>/logs/trinity.log
    static std::string temp_dir();          // <data>/tmp
    static std::string settings_file();     // <data>/settings.json

    // Create the standard directory layout. Idempotent; returns false when a
    // directory could not be created.
    static bool ensure_layout();

    // Environment access seam (test injection).
    static std::optional<std::string> env(const std::string& name);
};

}  // namespace trinity::core
