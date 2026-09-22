#include "trinity/firmware/Builder.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <array>
#include <cstdlib>
#include <memory>
#include <thread>

#include <filesystem>

namespace trinity::firmware {

namespace fs = std::filesystem;

namespace {

std::string pathJoin(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    return (fs::path(dir) / name).string();
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

std::string searchPathFor(const std::string& name) {
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    const char* pathEnv = std::getenv("PATH");
    if (pathEnv == nullptr) return {};
    std::string path(pathEnv);
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(sep, start);
        if (end == std::string::npos) end = path.size();
        std::string dir = path.substr(start, end - start);
        if (!dir.empty()) {
            const std::string candidate = pathJoin(dir, name);
            if (fileExists(candidate)) return candidate;
#ifdef _WIN32
            const std::string exe = candidate + ".exe";
            if (fileExists(exe)) return exe;
#endif
        }
        start = end + 1;
    }
    return {};
}

#ifdef _WIN32

struct HandleCloser {
    void operator()(HANDLE h) const {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
};

using UniqueHandle = std::unique_ptr<void, HandleCloser>;

std::string readPipeAsync(HANDLE pipe) {
    std::string out;
    std::array<char, 4096> buffer{};
    DWORD read = 0;
    while (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                    nullptr) != FALSE &&
           read > 0) {
        out.append(buffer.data(), read);
    }
    return out;
}

std::string quoteArg(const std::string& arg) {
    // Microsoft C runtime argument quoting rules.
    if (arg.find_first_of(" \t\"") == std::string::npos) {
        return arg;
    }
    std::string out = "\"";
    size_t backslashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out += '"';
            backslashes = 0;
            continue;
        }
        out.append(backslashes, '\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, '\\');
    out += '"';
    return out;
}

#endif  // _WIN32

}  // namespace

std::string findToolchain(const std::string& name) {
    const char* dirEnv = std::getenv("TRINITY_TOOLCHAIN_DIR");
    if (dirEnv != nullptr && *dirEnv != '\0') {
        const std::string candidate = pathJoin(dirEnv, name);
        if (fileExists(candidate)) return candidate;
        const std::string exe = candidate + ".exe";
        if (fileExists(exe)) return exe;
    }
    return searchPathFor(name);
}

BuildResult FirmwareBuilder::build(const std::string& toolchain,
                                   const std::vector<std::string>& argv,
                                   const std::vector<std::string>& expectedArtifacts) {
    BuildResult result;
    result.executable = toolchain;
    result.argv = argv;

    if (toolchain.empty()) {
        result.error = "No firmware toolchain found (set TRINITY_TOOLCHAIN_DIR or PATH)";
        return result;
    }
    if (!fileExists(toolchain)) {
        result.error = "Toolchain executable not found: " + toolchain;
        return result;
    }

#ifndef _WIN32
    result.error =
        "Controlled process execution is not implemented on this platform yet "
        "(CAPABILITY_UNAVAILABLE): " +
        toolchain;
    return result;
#else
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stderrRead = nullptr;
    HANDLE stderrWrite = nullptr;
    if (CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0) == FALSE ||
        CreatePipe(&stderrRead, &stderrWrite, &sa, 0) == FALSE) {
        result.error = "Failed to create capture pipes";
        return result;
    }
    UniqueHandle outRead(stdoutRead);
    UniqueHandle outWrite(stdoutWrite);
    UniqueHandle errRead(stderrRead);
    UniqueHandle errWrite(stderrWrite);
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0);

    std::string commandLine = quoteArg(toolchain);
    for (const auto& arg : argv) {
        commandLine += " ";
        commandLine += quoteArg(arg);
    }
    std::vector<char> mutableCmd(commandLine.begin(), commandLine.end());
    mutableCmd.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdoutWrite;
    si.hStdError = stderrWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    const BOOL created =
        CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW, nullptr,
                       workDir_.empty() ? nullptr : workDir_.c_str(), &si, &pi);
    if (created == FALSE) {
        result.error = "CreateProcess failed for " + toolchain;
        return result;
    }
    UniqueHandle process(pi.hProcess);
    UniqueHandle thread(pi.hThread);
    outWrite.reset();
    errWrite.reset();

    // Drain both pipes on background threads so a full stderr buffer can
    // never deadlock the process while we are still reading stdout.
    std::string stdoutText;
    std::string stderrText;
    std::thread outThread([&] { stdoutText = readPipeAsync(stdoutRead); });
    std::thread errThread([&] { stderrText = readPipeAsync(stderrRead); });
    WaitForSingleObject(process.get(), INFINITE);
    outThread.join();
    errThread.join();
    result.stdoutText = std::move(stdoutText);
    result.stderrText = std::move(stderrText);

    DWORD exitCode = 1;
    GetExitCodeProcess(process.get(), &exitCode);
    result.exitCode = static_cast<int>(exitCode);
    result.executed = true;
    result.success = (exitCode == 0);
    if (!result.success) {
        result.error = "Compiler exited with code " + std::to_string(exitCode);
    }

    for (const auto& artifact : expectedArtifacts) {
        const std::string full = pathJoin(workDir_, artifact);
        if (fileExists(full)) {
            result.artifacts.push_back(full);
        }
    }
    return result;
#endif
}

}  // namespace trinity::firmware
