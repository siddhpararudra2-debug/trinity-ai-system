#pragma once

// Headless native bridge: one JSON EngineRequest on stdin, one
// ToolResponse-shaped envelope on stdout. Python POST /api/execute
// owns jobs and artifact storage; this path must not allocate a
// native job_id. Logs stay on stderr so stdout remains parseable.

#include <string>

#include "../core/Json.hpp"
#include "../engines/Engine.hpp"

namespace trinity::cli {

engines::EngineResult executeRequest(const engines::EngineRequest& request);

/// Parse request JSON, execute, return envelope JSON (never throws).
core::Json executeRequestJson(const std::string& input);

/// Read all of stdin, write one JSON envelope to stdout. Returns 0
/// when an envelope was written, 1 when stdin was empty/unreadable.
int runExecFromStdin();

}  // namespace trinity::cli
