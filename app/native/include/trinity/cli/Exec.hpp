#pragma once

// Headless native bridge: one JSON EngineRequest on stdin, one
// ToolResponse-shaped envelope on stdout. Python POST /api/execute
// owns jobs and artifact storage; this path must not allocate a
// native job_id. Logs stay on stderr so stdout remains parseable.

#include <iosfwd>
#include <string>

#include "../core/Json.hpp"
#include "../engines/Engine.hpp"

namespace trinity::engines {
class EngineRegistry;
}

namespace trinity::cli {

engines::EngineResult executeRequest(const engines::EngineRequest& request);

/// Parse request JSON, execute, return envelope JSON (never throws).
core::Json executeRequestJson(const std::string& input);

/// Execute request using an existing engine registry (reuses engines across calls).
core::Json executeRequestJsonWithRegistry(engines::EngineRegistry& registry,
                                         const std::string& input);

/// Read all of stdin, write one JSON envelope to stdout. Returns 0
/// when an envelope was written, 1 when stdin was empty/unreadable.
int runExecFromStdin();

/// Persistent daemon: reads JSON lines from stream, writes JSON envelope lines to stream.
/// Reuses the EngineRegistry across all calls for high throughput.
int runDaemon(std::istream& in, std::ostream& out);

/// Persistent daemon reading from stdin and writing to stdout.
int runDaemonFromStdin();

}  // namespace trinity::cli
