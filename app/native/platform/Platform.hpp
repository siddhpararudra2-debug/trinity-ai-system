// Trinity — platform abstraction (brief §/platform).
//
// Everything that differs between operating systems funnels through here so
// the rest of the core stays portable and testable:
//   - IEnvironment is the single seam for environment variables, so tests can
//     point the whole data layout at a temp directory without touching the
//     real process environment;
//   - PlatformInfo is diagnostics (OS, architecture, CPU count, resolved dirs)
//     surfaced in Settings → General and in crash/log headers;
//   - install_terminate_handler() gives the desktop application a last-chance
//     structured log line instead of a silent abort (brief §Crash handling).
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "../core/Json.hpp"

namespace trinity::platform {

struct PlatformInfo {
    std::string os;                // "windows" | "linux" | "macos" | "unknown"
    std::string architecture;      // "x64" | "arm64" | "x86" | "unknown"
    int pointer_bits = 0;          // 32 | 64
    int cpu_count = 0;
    std::string config_dir;        // resolved Trinity data directory
    std::string documents_dir;     // resolved Documents directory
    std::string executable_directory;  // directory holding the running binary

    core::Json to_json() const;
};

// Environment access seam. Implementations must be safe to call concurrently.
class IEnvironment {
public:
    virtual ~IEnvironment() = default;
    virtual std::optional<std::string> get(const std::string& name) const = 0;
    // Home/profile directory; empty when it cannot be determined.
    virtual std::string home_directory() const = 0;
};

// Reads the real process environment. Never throws.
class SystemEnvironment : public IEnvironment {
public:
    std::optional<std::string> get(const std::string& name) const override;
    std::string home_directory() const override;
};

// Directory containing the running executable, or the current working
// directory when it cannot be determined. Used to locate the bundled Python
// engine host without hardcoding install paths.
std::string executable_directory();

// std::thread::hardware_concurrency(), floored at 1.
std::size_t hardware_concurrency();

// Installs a std::terminate handler that logs a structured critical record
// (including the in-flight exception when one exists) and then aborts.
// Idempotent; call once during application startup.
void install_terminate_handler();

PlatformInfo describe_platform(const IEnvironment& environment);

}  // namespace trinity::platform
