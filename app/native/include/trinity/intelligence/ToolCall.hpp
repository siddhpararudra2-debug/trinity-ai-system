#pragma once

// Structured tool-call: the only thing an LLM is ever allowed to emit.
// {engine, operation, parameters} maps 1:1 onto JobManager::runSync
// and POST /api/execute in the Python backend. Models never execute.

#include <string>

#include "../core/Json.hpp"

namespace trinity::intelligence {

struct ToolCall {
    std::string toolCallId;  // UUID for tracing; empty when unset
    std::string engine;
    std::string operation;
    core::Json parameters = core::Json::object();

    core::Json toJson() const;
    static ToolCall fromJson(const core::Json& json);
};

}  // namespace trinity::intelligence
