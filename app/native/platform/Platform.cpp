#include "Platform.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

#include "../core/Error.hpp"
#include "../core/Logging.hpp"
#include "../core/Paths.hpp"

namespace trinity::platform {
namespace {

core::ComponentLog log_("platform");

std::string join(const std::string& base, const std::string& child) {
    if (base.empty()) return child;
    const char last = base.back();
    if (last == '/' || last == '\\') return base + child;
    return base + "/" + child;
}

std::string detect_architecture() {
#if defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    return "x64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

}  // namespace

std::optional<std::string> SystemEnvironment::get(const std::string& name) const {
    // Delegates to the same hardened lookup the path layer already uses
    // (getenv_s on Windows, getenv elsewhere).
    return core::Paths::env(name);
}

std::string SystemEnvironment::home_directory() const {
    for (const char* candidate : {"USERPROFILE", "HOME"}) {
        if (auto value = get(candidate); value && !value->empty()) return *value;
    }
    return std::string();
}

std::string executable_directory() {
#if defined(_WIN32)
    char buffer[MAX_PATH * 4];
    const DWORD length = ::GetModuleFileNameA(nullptr, buffer, sizeof(buffer));
    if (length > 0 && length < sizeof(buffer)) {
        std::string path(buffer, length);
        const std::size_t cut = path.find_last_of("/\\");
        if (cut != std::string::npos) return path.substr(0, cut);
        return path;
    }
#elif defined(__linux__)
    char buffer[PATH_MAX + 1];
    const ssize_t length = ::readlink("/proc/self/exe", buffer, PATH_MAX);
    if (length > 0) {
        std::string path(buffer, static_cast<std::size_t>(length));
        const std::size_t cut = path.find_last_of('/');
        if (cut != std::string::npos) return path.substr(0, cut);
        return path;
    }
#endif
    std::error_code ec;
    const std::string cwd = std::filesystem::current_path(ec).string();
    return ec ? std::string(".") : cwd;
}

std::size_t hardware_concurrency() {
    const unsigned int detected = std::thread::hardware_concurrency();
    return detected == 0 ? 1 : static_cast<std::size_t>(detected);
}

void install_terminate_handler() {
    static bool installed = false;
    if (installed) return;
    installed = true;

    std::set_terminate([] {
        // Best effort only: a terminating process may be mid-allocator, so
        // every step here is guarded and failures are ignored.
        try {
            core::Json context = core::Json::object();
            context["fatal"] = true;
            if (std::exception_ptr in_flight = std::current_exception()) {
                try {
                    std::rethrow_exception(in_flight);
                } catch (const core::TrinityException& exception) {
                    context["error"] = exception.error().to_json();
                    log_.critical("terminate called with an unhandled trinity error", context);
                } catch (const std::exception& exception) {
                    context["what"] = std::string(exception.what());
                    log_.critical("terminate called with an unhandled exception", context);
                } catch (...) {
                    log_.critical("terminate called with a non-standard exception", context);
                }
            } else {
                log_.critical("terminate called with no in-flight exception", context);
            }
        } catch (...) {
        }
        std::abort();
    });
}

core::Json PlatformInfo::to_json() const {
    core::Json out = core::Json::object();
    out["os"] = os;
    out["architecture"] = architecture;
    out["pointer_bits"] = pointer_bits;
    out["cpu_count"] = cpu_count;
    out["config_dir"] = config_dir;
    out["documents_dir"] = documents_dir;
    out["executable_directory"] = executable_directory;
    return out;
}

PlatformInfo describe_platform(const IEnvironment& environment) {
    PlatformInfo info;
#if defined(_WIN32)
    info.os = "windows";
#elif defined(__APPLE__)
    info.os = "macos";
#elif defined(__linux__)
    info.os = "linux";
#else
    info.os = "unknown";
#endif
    info.architecture = detect_architecture();
    info.pointer_bits = static_cast<int>(sizeof(void*) * 8);
    info.cpu_count = static_cast<int>(hardware_concurrency());
    info.executable_directory = executable_directory();

    // Config directory mirrors the layout rules in core/Paths.cpp, but reads
    // them through the injected environment so tests stay hermetic.
    if (auto override_dir = environment.get("TRINITY_DATA_DIR");
        override_dir && !override_dir->empty()) {
        info.config_dir = *override_dir;
    } else if (auto local = environment.get("LOCALAPPDATA"); local && !local->empty()) {
        info.config_dir = join(*local, "Trinity");
    } else if (auto xdg = environment.get("XDG_DATA_HOME"); xdg && !xdg->empty()) {
        info.config_dir = join(*xdg, "Trinity");
    } else if (const std::string home = environment.home_directory(); !home.empty()) {
        info.config_dir = join(home, ".local/share/Trinity");
    }

    if (auto documents = environment.get("TRINITY_DOCUMENTS");
        documents && !documents->empty()) {
        info.documents_dir = *documents;
    } else if (const std::string home = environment.home_directory(); !home.empty()) {
        info.documents_dir = join(home, "Documents");
    }

    return info;
}

}  // namespace trinity::platform
