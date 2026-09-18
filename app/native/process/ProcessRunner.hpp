// Trinity — child process management (brief §Native / §Security).
//
// Launches helper processes (Python engine host) with:
//   - fully sanitized argv (no shell, no user text interpolation)
//   - sanitized environment (only whitelisted variables pass through)
//   - streamed stdout/stderr through the IPC LineSplitter
//   - graceful terminate then kill after a timeout
//
// Lifetimes: the reader/watcher threads are detached but hold a shared_ptr to
// the process state, never a pointer to the ProcessRunner, so destroying the
// runner mid-run cannot turn into a use-after-free.
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../core/Error.hpp"

namespace trinity::process {

struct ProcessConfig {
    std::string executable;
    std::vector<std::string> arguments;        // argv-style, no shell
    std::vector<std::pair<std::string, std::string>> environment;  // extra env vars
    std::string working_directory;
};

// Platform handles + live status for one child process. Defined in the .cpp so
// <windows.h> stays out of this header; shared with the worker threads.
struct ProcessState;

class ProcessRunner {
public:
    using OutputFn = std::function<void(const char* data, std::size_t length)>;
    using ExitFn = std::function<void(int exit_code)>;

    ProcessRunner() = default;
    ~ProcessRunner();

    ProcessRunner(const ProcessRunner&) = delete;
    ProcessRunner& operator=(const ProcessRunner&) = delete;

    // Launches the child. Returns an error when the executable cannot be
    // started. Output callbacks fire on background reader threads.
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
    std::shared_ptr<ProcessState> state_;
};

}  // namespace trinity::process
