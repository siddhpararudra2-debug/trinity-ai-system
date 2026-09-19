#pragma once

// Model response envelope for the future LLM boundary.

#include <string>

#include "../core/Json.hpp"
#include "ToolCall.hpp"

namespace trinity::intelligence {

struct ModelResponse {
    bool success = false;
    std::string text;
    ToolCall toolCall;
    bool hasToolCall = false;
    core::Json error = nullptr;

    core::Json toJson() const;
};

}  // namespace trinity::intelligence
