#include "ProcessRunner.hpp"

#include <chrono>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#endif

#include "../core/Logging.hpp"

namespace trinity::process {
namespace {
core::ComponentLog log_("process");

struct ProcessHandles {
#if defined(_WIN32)
    HANDLE child = nullptr;
    HANDLE stdin_write = nullptr;
    HANDLE stdout_read = nullptr;
    HANDLE stderr_read = nullptr;
#else
    pid_t child = -1;
    int stdin_write = -1;
    int stdout_read = -1;
    int stderr_read = -1;
#endif
};
}  // namespace

ProcessRunner::~ProcessRunner() { terminate(1000); }

core::Status ProcessRunner::launch(const ProcessConfig& config, OutputFn on_stdout,
                                   OutputFn on_stderr, ExitFn on_exit) {
    if (running_.load()) {
        return core::Status::fail(
            core::Error(core::ErrorCode::ProcessError, "process already running"));
    }

    handles_ = std::make_unique<ProcessHandles>();

#if defined(_WIN32)
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE stdin_read = nullptr, stdout_write = nullptr, stderr_write = nullptr;
    if (!CreatePipe(&stdin_read, &handles_->stdin_write, &inherit, 0) ||
        !CreatePipe(&handles_->stdout_read, &stdout_write, &inherit, 0) ||
        !CreatePipe(&handles_->stderr_read, &stderr_write, &inherit, 0)) {
        return core::Status::fail(
            core::Error(core::ErrorCode::ProcessError, "pipe creation failed"));
    }
    // Our ends must NOT be inherited by the child.
    SetHandleInformation(handles_->stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(handles_->stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(handles_->stderr_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_read;
    startup.hStdOutput = stdout_write;
    startup.hStdError = stderr_write;

    // Sanitised command line: quoted argv join, no shell interpretation.
    std::string command_line = "\"" + config.executable + "\"";
    for (const std::string& argument : config.arguments) {
        command_line += " \"" + argument + "\"";
    }

    // Environment block: allow-list essentials + caller-provided extras.
    std::string env_block;
    for (const char* key : {"SYSTEMROOT", "TEMP", "TMP", "PATH"}) {
        if (const char* value = std::getenv(key)) {
            env_block += std::string(key) + "=" + value + "\0";
        }
    }
    for (const auto& [key, value] : config.environment) {
        env_block += key + "=" + value + "\0";
    }
    env_block += "\0";

    PROCESS_INFORMATION info{};
    const BOOL created = CreateProcessA(
        nullptr, command_line.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, env_block.data(),
        config.working_directory.empty() ? nullptr : config.working_directory.c_str(), &startup,
        &info);
    CloseHandle(stdin_read);
    CloseHandle(stdout_write);
    CloseHandle(stderr_write);
    if (!created) {
        return core::Status::fail(core::Error(
            core::ErrorCode::ProcessError,
            "CreateProcess failed for '" + config.executable + "' (error " +
                std::to_string(GetLastError()) + ")"));
    }
    CloseHandle(info.hThread);
    handles_->child = info.hProcess;
#else
    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        return core::Status::fail(
            core::Error(core::ErrorCode::ProcessError, "pipe creation failed"));
    }
    const pid_t pid = fork();
    if (pid < 0) {
        return core::Status::fail(
            core::Error(core::ErrorCode::ProcessError, "fork failed"));
    }
    if (pid == 0) {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(config.executable.c_str()));
        for (const std::string& a : config.arguments) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);
        execv(config.executable.c_str(), argv.data());
        _exit(127);
    }
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    handles_->child = pid;
    handles_->stdin_write = stdin_pipe[1];
    handles_->stdout_read = stdout_pipe[0];
    handles_->stderr_read = stderr_pipe[0];
#endif

    running_.store(true);
    exit_code_.store(-1);
    log_.info("child process started", [&] {
        core::Json ctx = core::Json::object();
        ctx["executable"] = config.executable;
        return ctx;
    }());

    // Reader threads: pump stdout/stderr to the callbacks.
    auto pump = [this](auto read_handle, OutputFn callback) {
        char buffer[4096];
        while (true) {
#if defined(_WIN32)
            DWORD read = 0;
            if (!ReadFile(read_handle, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
#else
            const ssize_t read = ::read(read_handle, buffer, sizeof(buffer));
            if (read <= 0) break;
#endif
            if (callback) callback(buffer, static_cast<std::size_t>(read));
        }
    };
    std::thread stdout_thread(pump, handles_->stdout_read, std::move(on_stdout));
    std::thread stderr_thread(pump, handles_->stderr_read, std::move(on_stderr));
    stdout_thread.detach();
    stderr_thread.detach();

    // Exit watcher.
    std::thread watcher([this, on_exit = std::move(on_exit)] {
#if defined(_WIN32)
        WaitForSingleObject(handles_->child, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(handles_->child, &code);
        exit_code_.store(static_cast<int>(code));
#else
        int status = 0;
        waitpid(handles_->child, &status, 0);
        exit_code_.store(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
#endif
        running_.store(false);
        if (on_exit) on_exit(exit_code_.load());
    });
    watcher.detach();

    return core::Status::ok();
}

core::Status ProcessRunner::write_stdin(const std::string& data) {
    if (!running_.load() || handles_ == nullptr) {
        return core::Status::fail(
            core::Error(core::ErrorCode::ProcessError, "process not running"));
    }
#if defined(_WIN32)
    DWORD written = 0;
    if (!WriteFile(handles_->stdin_write, data.data(), static_cast<DWORD>(data.size()),
                   &written, nullptr)) {
        return core::Status::fail(
            core::Error(core::ErrorCode::ProcessError, "stdin write failed"));
    }
#else
    ssize_t written = ::write(handles_->stdin_write, data.data(), data.size());
    if (written < 0) {
        return core::Status::fail(
            core::Error(core::ErrorCode::ProcessError, "stdin write failed"));
    }
#endif
    return core::Status::ok();
}

void ProcessRunner::close_stdin() {
    if (handles_ == nullptr) return;
#if defined(_WIN32)
    if (handles_->stdin_write != nullptr) {
        CloseHandle(handles_->stdin_write);
        handles_->stdin_write = nullptr;
    }
#else
    if (handles_->stdin_write >= 0) {
        ::close(handles_->stdin_write);
        handles_->stdin_write = -1;
    }
#endif
}

void ProcessRunner::terminate(int timeout_ms) {
    if (handles_ == nullptr || !running_.load()) return;
#if defined(_WIN32)
    TerminateProcess(handles_->child, 1);
    WaitForSingleObject(handles_->child, static_cast<DWORD>(timeout_ms));
    if (handles_->child) CloseHandle(handles_->child);
#else
    kill(handles_->child, SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (running_.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (running_.load()) kill(handles_->child, SIGKILL);
#endif
    running_.store(false);
}

bool ProcessRunner::is_running() const { return running_.load(); }

int ProcessRunner::exit_code() const { return exit_code_.load(); }

}  // namespace trinity::process
