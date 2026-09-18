// Trinity — child process management (brief §Native / §Security).
//
// Launches helper processes (Python engine host) with:
//   - fully sanitized argv (no shell, no user text interpolation)
//   - sanitized environment (only whitelisted variables pass through)
//   - streamed stdout/stderr through the IPC LineSplitter
//   - graceful terminate then kill after a timeout
//
// Implementation uses std::system-free popen-free pipes; on Windows the
// CreateProcess path is isolated in the .cpp so the interface stays portable.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "../core/Error.hpp"

namespace trinity::process {

struct ProcessConfig {
    std::string executable;
    std::vector<std::string> arguments;        // argv-style, no shell
    std::vector<std::pair<std::string, std::string>> environment;  // extra env vars
    std::string working_directory;
};

struct ProcessHandles;

class ProcessRunner {
public:
    using OutputFn = std::function<void(const char* data, std::size_t length)>;
    using ExitFn = std::function<void(int exit_code)>;

    ProcessRunner() = default;
    ~ProcessRunner();

    // Launches the child. Returns false with an error when the executable
    // cannot be started. Output callbacks fire on this thread.
    core::Status launch(const ProcessConfig& config, OutputFn on_stdout, OutputFn on_stderr,
                        ExitFn on_exit);

    // Writes bytes to the child's stdin.
    core::Status write_stdin(const std::string& data);

    // Closes stdin (signals EOF to the child).
    void close_stdin();

    // Terminate politely; escalates to kill after timeout_ms.
    void terminate(int timeout_ms = 3000);

    bool is_running() const;
    int exit_code() const;

private:
    std::unique_ptr<ProcessHandles> handles_;
    std::atomic<int> exit_code_{-1};
    std::atomic<bool> running_{false};
};

}  // namespace trinity::process
