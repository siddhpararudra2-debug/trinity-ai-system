#pragma once

// PlatformIO integration: genuine `pio run` builds producing real
// firmware.hex / firmware.bin artifacts. The path mirrors the
// controlled-execution law of Builder.hpp: PlatformIO is located by
// name (TRINITY_TOOLCHAIN_DIR or PATH), invoked as an explicit
// executable + argv, project data reaches it only as files the engine
// wrote itself. Unknown MCU models have no PlatformIO target — the
// caller falls back to the named cross-toolchain and reports
// CAPABILITY_UNAVAILABLE when nothing is available.

#include <string>

#include "Builder.hpp"
#include "FirmwareProject.hpp"

namespace trinity::firmware {

struct PlatformIoSpec {
    std::string env;        // [env:<env>] section name
    std::string platform;   // e.g. "espressif32"
    std::string board;      // e.g. "esp32dev"
    std::string framework;  // e.g. "arduino"
};

/// Supported-MCU → PlatformIO target mapping. Returns false for MCU
/// models that have no known PlatformIO board (never invents one).
bool platformIoSpecFor(const std::string& mcuModel, PlatformIoSpec& out);

/// Deterministic platformio.ini text for a project (same project →
/// same bytes, mirrors CodeGen's determinism law).
std::string platformIoConfigText(const FirmwareProject& project,
                                 const PlatformIoSpec& spec);

/// Run `pio run -d <workDir>` in a scratch workspace: writes
/// platformio.ini and every project source under src/, then executes
/// PlatformIO through FirmwareBuilder (explicit argv, captured out).
/// Artifacts are the real files PlatformIO emitted under
/// .pio/build/*/ (firmware.hex / firmware.bin / firmware.elf / .uf2).
BuildResult runPlatformIoBuild(const FirmwareProject& project,
                               const std::string& workDir,
                               const std::string& pioExecutable);

/// Name used for PlatformIO resolution ("platformio", falling back to
/// the shorter "pio" alias that the installer also provides).
constexpr const char* kPlatformIoPrimaryName = "platformio";
constexpr const char* kPlatformIoAliasName = "pio";

}  // namespace trinity::firmware
