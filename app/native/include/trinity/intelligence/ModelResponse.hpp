#pragma once

// Model response envelope for the future LLM boundary.
// Every response carries success/failure, operation context, IDs and
// structured error information so callers never parse free text.

#include <string>
#include <vector>

#include "../core/Json.hpp"
#include "ToolCall.hpp"

namespace trinity::intelligence {

struct ModelResponse {
    bool success = false;
    std::string responseId;
    std::string requestId;
    std::string model;
    std::string operation = "generate";
    std::string text;
    ToolCall toolCall;
    bool hasToolCall = false;
    std::vector<ToolCall> toolCalls;
    core::Json usage = core::Json::object();
    core::Json error = nullptr;

    core::Json toJson() const;
    static ModelResponse fromJson(const core::Json& json);
};

}  // namespace trinity::intelligence
