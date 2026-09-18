#include "ProcessRunner.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
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
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "../core/Logging.hpp"

namespace trinity::process {
namespace {
core::ComponentLog log_("process");
}  // namespace

// One child process: platform handles plus live status, shared with the
// detached reader/watcher threads so they never touch the runner itself.
struct ProcessState {
    std::atomic<bool> running{false};
    std::atomic<int> exit_code{-1};
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

ProcessRunner::~ProcessRunner() { terminate(1000); }

core::Status ProcessRunner::launch(const ProcessConfig& config, OutputFn on_stdout,
                                   OutputFn on_stderr, ExitFn on_exit) {
    if (state_ && state_->running.load()) {
        return core::Status::fail(
            core::Error(core::ErrorCode::ProcessError, "process already running"));
    }

    auto state = std::make_shared<ProcessState>();
#if defined(_WIN32)
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE stdin_read = nullptr, stdout_write = nullptr, stderr_write = nullptr;
    if (!CreatePipe(&stdin_read, &state->stdin_write, &inherit, 0) ||
        !CreatePipe(&state->stdout_read, &stdout_write, &inherit, 0) ||
        !CreatePipe(&state->stderr_read, &stderr_write, &inherit, 0)) {
        for (HANDLE handle : {stdin_read, state->stdin_write, state->stdout_read, stdout_write,
                              state->stderr_read, stderr_write}) {
            if (handle != nullptr) CloseHandle(handle);
        }
        return core::Status::fail(
            core::Error(core::ErrorCode::ProcessError, "pipe creation failed"));
    }
    // Our ends must NOT be inherited by the child.
    SetHandleInformation(state->stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(state->stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(state->stderr_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_read;
    startup.hStdOutput = stdout_write;
    startup.hStdError = stderr_write;

    // Sanitised command line: quoted argv join, never a shell string.
    // Quotes inside an argument are rejected rather than escaped so no caller
    // can break out of the quoting (no arbitrary command execution).
    std::vector<std::string> argv_all;
    argv_all.reserve(config.arguments.size() + 1);
    argv_all.push_back(config.executable);
    argv_all.insert(argv_all.end(), config.arguments.begin(), config.arguments.end());

    std::string command_line;
    for (const std::string& argument : argv_all) {
        if (argument.find('"') != std::string::npos) {
            return core::Status::fail(core::Error(core::ErrorCode::ProcessError,
                                                  "argument contains a quote character"));
        }
        command_line += "\"" + argument + "\" ";
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

    // CreateProcessA takes an ANSI environment block, so the
    // CREATE_UNICODE_ENVIRONMENT flag must stay off (that flag is for W calls).
    PROCESS_INFORMATION info{};
    const BOOL created = CreateProcessA(
        nullptr, command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        env_block.data(), config.working_directory.empty() ? nullptr
                                                           : config.working_directory.c_str(),
        &startup, &info);
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
    state->child = info.hProcess;
#else
    int stdin_pipe[2] = {-1, -1}, stdout_pipe[2] = {-1, -1}, stderr_pipe[2] = {-1, -1};
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        for (int fd : {stdin_pipe[0], stdin_pipe[1], stdout_pipe[0], stdout_pipe[1],
                       stderr_pipe[0], stderr_pipe[1]}) {
            if (fd >= 0) ::close(fd);
        }
        return core::Status::fail(
            core::Error(core::ErrorCode::ProcessError, "pipe creation failed"));
    }
    const pid_t pid = fork();
    if (pid < 0) {
        return core::Status::fail(core::Error(core::ErrorCode::ProcessError, "fork failed"));
    }
    if (pid == 0) {
        // Child: wire the pipes, apply the sanitised environment, then exec.
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        for (int fd : {stdin_pipe[0], stdin_pipe[1], stdout_pipe[0], stdout_pipe[1],
                       stderr_pipe[0], stderr_pipe[1]}) {
            if (fd > STDERR_FILENO) ::close(fd);
        }
        if (!config.working_directory.empty()) {
            if (chdir(config.working_directory.c_str()) != 0) _exit(126);
        }
        for (const auto& [key, value] : config.environment) {
            setenv(key.c_str(), value.c_str(), 1);
        }
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(config.executable.c_str()));
        for (const std::string& argument : config.arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        execv(config.executable.c_str(), argv.data());
        _exit(127);
    }
    ::close(stdin_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);
    state->child = pid;
    state->stdin_write = stdin_pipe[1];
    state->stdout_read = stdout_pipe[0];
    state->stderr_read = stderr_pipe[0];
#endif

    state->running.store(true);
    state->exit_code.store(-1);
    state_ = state;
    log_.info("child process started", [&] {
        core::Json ctx = core::Json::object();
        ctx["executable"] = config.executable;
        return ctx;
    }());

    // Reader threads: pump stdout/stderr into the callbacks. They hold the
    // shared state, not the runner, so they cannot dangle.
    auto pump = [](std::shared_ptr<ProcessState> state, auto read_handle, OutputFn callback) {
        char buffer[4096];
        while (true) {
#if defined(_WIN32)
            DWORD read = 0;
            if (!ReadFile(read_handle, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
            const std::size_t count = static_cast<std::size_t>(read);
#else
            const ssize_t read = ::read(read_handle, buffer, sizeof(buffer));
            if (read <= 0) break;
            const std::size_t count = static_cast<std::size_t>(read);
#endif
            if (callback) callback(buffer, count);
            if (!state->running.load()) break;
        }
    };
    std::thread stdout_thread(pump, state, state->stdout_read, std::move(on_stdout));
    std::thread stderr_thread(pump, state, state->stderr_read, std::move(on_stderr));
    stdout_thread.detach();
    stderr_thread.detach();

    // Exit watcher.
    std::thread watcher([state, on_exit = std::move(on_exit)] {
#if defined(_WIN32)
        WaitForSingleObject(state->child, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(state->child, &code);
        state->exit_code.store(static_cast<int>(code));
#else
        int status = 0;
        waitpid(state->child, &status, 0);
        state->exit_code.store(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
#endif
        state->running.store(false);
        if (on_exit) on_exit(state->exit_code.load());
    });
    watcher.detach();

    return core::Status::ok();
}

core::Status ProcessRunner::write_stdin(const std::string& data) {
    if (!state_ || !state_->running.load()) {
        return core::Status::fail(
            core::Error(core::ErrorCode::ProcessError, "process not running"));
    }
#if defined(_WIN32)
    DWORD written = 0;
    if (!WriteFile(state_->stdin_write, data.data(), static_cast<DWORD>(data.size()), &written,
                   nullptr)) {
        return core::Status::fail(
            core::Error(core::ErrorCode::ProcessError, "stdin write failed"));
    }
#else
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t written = ::write(state_->stdin_write, data.data() + offset,
                                        data.size() - offset);
        if (written < 0) {
            if (errno == EINTR) continue;
            return core::Status::fail(
                core::Error(core::ErrorCode::ProcessError, "stdin write failed"));
        }
        offset += static_cast<std::size_t>(written);
    }
#endif
    return core::Status::ok();
}

void ProcessRunner::close_stdin() {
    if (!state_) return;
#if defined(_WIN32)
    if (state_->stdin_write != nullptr) {
        CloseHandle(state_->stdin_write);
        state_->stdin_write = nullptr;
    }
#else
    if (state_->stdin_write >= 0) {
        ::close(state_->stdin_write);
        state_->stdin_write = -1;
    }
#endif
}

void ProcessRunner::terminate(int timeout_ms) {
    if (!state_ || !state_->running.load()) {
        // Still release handles we own, even when the child already exited.
        if (state_) {
#if defined(_WIN32)
            if (state_->child != nullptr) {
                CloseHandle(state_->child);
                state_->child = nullptr;
            }
#endif
            close_stdin();
        }
        return;
    }
#if defined(_WIN32)
    TerminateProcess(state_->child, 1);
    WaitForSingleObject(state_->child, static_cast<DWORD>(timeout_ms));
    if (state_->child != nullptr) {
        CloseHandle(state_->child);
        state_->child = nullptr;
    }
#else
    kill(state_->child, SIGTERM);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (state_->running.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (state_->running.load()) kill(state_->child, SIGKILL);
#endif
    state_->running.store(false);
    close_stdin();
}

bool ProcessRunner::is_running() const { return state_ && state_->running.load(); }

int ProcessRunner::exit_code() const { return state_ ? state_->exit_code.load() : -1; }

}  // namespace trinity::process
