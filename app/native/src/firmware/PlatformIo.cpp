#include "trinity/firmware/PlatformIo.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>

#include "trinity/core/Logger.hpp"

namespace trinity::firmware {
namespace fs = std::filesystem;

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

bool fileExists(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec;
}

}  // namespace

bool platformIoSpecFor(const std::string& mcuModel, PlatformIoSpec& out) {
    // Facts about real PlatformIO registry boards — no invented targets.
    static const std::map<std::string, PlatformIoSpec> kSpecs{
        {"ESP32", PlatformIoSpec{"esp32", "espressif32", "esp32dev", "arduino"}},
        {"STM32F401RE",
         PlatformIoSpec{"stm32f401re", "ststm32", "nucleo_f401re", "arduino"}},
        {"RP2040",
         PlatformIoSpec{"rp2040", "raspberrypi", "pico", "arduino"}},
    };
    const auto it = kSpecs.find(mcuModel);
    if (it == kSpecs.end()) {
        return false;
    }
    out = it->second;
    return true;
}

std::string platformIoConfigText(const FirmwareProject& project,
                                 const PlatformIoSpec& spec) {
    std::string out;
    out += "[env:" + spec.env + "]\n";
    out += "platform = " + spec.platform + "\n";
    out += "board = " + spec.board + "\n";
    out += "framework = " + spec.framework + "\n";
    if (!project.build.optLevel.empty()) {
        // The optimisation level selected in the UI flows through as a
        // compiler flag, like it would in any hand-written ini.
        out += "build_flags = " + project.build.optLevel + "\n";
    }
    out += "monitor_speed = 115200\n";
    return out;
}

BuildResult runPlatformIoBuild(const FirmwareProject& project,
                               const std::string& workDir,
                               const std::string& pioExecutable) {
    BuildResult out;
    if (pioExecutable.empty()) {
        out.error = "No PlatformIO executable provided";
        return out;
    }

    PlatformIoSpec spec;
    if (!platformIoSpecFor(project.mcu.model, spec)) {
        out.error =
            "No PlatformIO board mapping for MCU '" + project.mcu.model + "'";
        return out;
    }

    // Scratch workspace: platformio.ini + every generated source under
    // src/. Relative source paths are preserved; absolute or
    // parent-escaping paths are refused (never an escape hatch).
    const fs::path root(workDir);
    std::error_code ec;
    fs::create_directories(root / "src", ec);
    if (ec) {
        out.error = "Cannot create PlatformIO workspace: " + ec.message();
        return out;
    }

    std::ofstream ini(root / "platformio.ini", std::ios::binary | std::ios::trunc);
    if (!ini) {
        out.error = "Cannot write platformio.ini";
        return out;
    }
    ini << platformIoConfigText(project, spec);
    ini.close();
    if (!ini) {
        out.error = "Failed while writing platformio.ini";
        return out;
    }

    for (const auto& src : project.sources) {
        const fs::path relative = fs::path(src.path).lexically_normal();
        if (relative.is_absolute() ||
            (relative.begin() != relative.end() && *relative.begin() == "..")) {
            out.error = "Refusing non-relative firmware source path: " + src.path;
            return out;
        }
        const fs::path srcPath = root / "src" / relative;
        std::error_code mkdirEc;
        if (srcPath.has_parent_path()) {
            fs::create_directories(srcPath.parent_path(), mkdirEc);
            if (mkdirEc) {
                out.error =
                    "Cannot create source directory: " + mkdirEc.message();
                return out;
            }
        }
        std::ofstream file(srcPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            out.error = "Cannot write source " + src.path;
            return out;
        }
        file << src.content;
        file.close();
        if (!file) {
            out.error = "Failed while writing " + src.path;
            return out;
        }
    }

    FirmwareBuilder builder(workDir);
    out = builder.build(pioExecutable, {"run", "-d", workDir.empty() ? "." : workDir}, {});

    // Collect the real binaries PlatformIO produced (firmware.hex,
    // firmware.bin, firmware.elf, .uf2 — per platform).
    out.artifacts.clear();
    const fs::path buildDir = root / ".pio" / "build";
    if (fs::exists(buildDir, ec) && !ec) {
        for (const fs::path& envDir : fs::directory_iterator(buildDir, ec)) {
            std::error_code existsEc;
            if (!fs::exists(envDir, existsEc) || existsEc) continue;
            for (const fs::path& file : fs::directory_iterator(envDir, ec)) {
                const std::string stem = file.stem().string();
                const std::string ext = lower(file.extension().string());
                if (stem != "firmware" && stem.rfind("firmware", 0) != 0) continue;
                if (ext != ".hex" && ext != ".bin" && ext != ".elf" &&
                    ext != ".uf2") {
                    continue;
                }
                if (fileExists(file)) {
                    out.artifacts.push_back(file.string());
                }
            }
        }
    }
    // Directory iteration order is not meaningful; sort so artifact
    // kinds map deterministically (and doctests stay stable).
    std::sort(out.artifacts.begin(), out.artifacts.end());
    if (out.success && out.artifacts.empty()) {
        out.success = false;
        out.error =
            "PlatformIO exited successfully but emitted no firmware.hex/"
            "firmware.bin artifacts";
    }
    core::Logger::instance().info(
        "engines", "platformio firmware build",
        core::Json{{"mcu", project.mcu.model},
                   {"executed", out.executed},
                   {"success", out.success},
                   {"artifacts", static_cast<long long>(out.artifacts.size())}});
    return out;
}

}  // namespace trinity::firmware
