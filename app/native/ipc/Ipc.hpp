// Trinity — IPC layer (brief §IPC).
//
// Protocol: length-prefixed newline JSON frames over a byte stream.
//   {"id":"<uuid>","type":"request","method":"...","payload":{...}}
//   {"id":"<uuid>","type":"response","ok":true|false,"result":{}|{"error":{}}}
//   {"id":"...","type":"event","name":"...","payload":{...}}
//
// The same framing runs in three transports:
//   1. in-process (IpcServer)      — tests, embedded hosts
//   2. child stdin/stdout          — Python engine host (ProcessRunner)
//   3. named pipes (future)        — upgrade path, protocol unchanged
//
// HTTP-localhost is deliberately not used for internal traffic.
#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "../core/Error.hpp"
#include "../core/Json.hpp"

namespace trinity::ipc {

struct Frame {
    std::string id;
    std::string type;     // "request" | "response" | "event"
    std::string method;   // requests only
    std::string name;     // events only
    core::Json payload = core::Json::object();
    bool ok = true;                 // responses only
    core::Error error;              // responses with ok=false

    std::string encode() const;
    static core::Result<Frame> decode(const std::string& line);
};

// Handler receives the request payload; returns the result payload.
using IpcHandler = std::function<core::Json(const core::Json& payload)>;

// Minimal in-process router used by tests, the shell, and as the reference
// semantics for the child-process transport.
class IpcServer {
public:
    void register_method(const std::string& method, IpcHandler handler);
    void register_event_listener(const std::string& name,
                                 std::function<void(const core::Json& payload)> listener);

    // Processes an encoded request frame; returns the encoded response frame.
    core::Result<std::string> handle_line(const std::string& line);

    void emit_event(const std::string& name, const core::Json& payload);

    // Direct call convenience (bypasses encoding).
    core::Result<core::Json> call(const std::string& method, const core::Json& payload);

private:
    std::mutex mutex_;
    std::map<std::string, IpcHandler> handlers_;
    std::map<std::string, std::vector<std::function<void(const core::Json&)>>> listeners_;
};

// Splits a byte stream into complete newline-terminated lines (frames).
class LineSplitter {
public:
    // Feeds bytes; complete lines (without the newline) are appended.
    void feed(const char* data, std::size_t length, std::vector<std::string>& out_lines);
    bool has_partial() const { return !buffer_.empty(); }

private:
    std::string buffer_;
};

}  // namespace trinity::ipc
