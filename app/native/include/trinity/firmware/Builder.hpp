#pragma once

// Controlled firmware build abstraction. Resolves the configured
// cross-toolchain (never a shell string), executes it as an explicit
// executable + argv array, and captures stdout/stderr, exit code and
// produced artifacts. No system()/popen()/cmd/raw-input execution
// anywhere: user data only ever reaches the compiler as file paths
// written by the generator. Missing toolchains report
// CAPABILITY_UNAVAILABLE truthfully — success is never claimed.

#include <string>
#include <vector>

#include "../core/Json.hpp"

namespace trinity::firmware {

struct BuildResult {
    bool executed = false;  // false when no toolchain was available
    bool success = false;
    int exitCode = -1;
    std::string executable;  // resolved compiler path (or name probed)
    std::vector<std::string> argv;  // controlled argument list
    std::string stdoutText;
    std::string stderrText;
    std::vector<std::string> artifacts;  // produced file paths
    std::string error;  // human reason when !executed or !success
};

/// Locate a toolchain executable: TRINITY_TOOLCHAIN_DIR override first,
/// then PATH search. Returns "" when unavailable (never throws).
std::string findToolchain(const std::string& name);

class FirmwareBuilder {
public:
    explicit FirmwareBuilder(std::string workDir) : workDir_(std::move(workDir)) {}

    /// Compile sources (written by the generator) with the given
    /// toolchain executable + argv. All paths stay under workDir_.
    BuildResult build(const std::string& toolchain, const std::vector<std::string>& argv,
                      const std::vector<std::string>& expectedArtifacts);

private:
    std::string workDir_;
};

}  // namespace trinity::firmware
