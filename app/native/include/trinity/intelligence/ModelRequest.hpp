#pragma once

// Model request envelope for the future LLM boundary.
// Transport-agnostic: providers translate these to/from their own
// wire formats. No provider is connected yet (see IModelProvider).

#include <string>

#include "../core/Json.hpp"

namespace trinity::intelligence {

struct ModelRequest {
    std::string prompt;
    double temperature = 0.2;
    double topP = 0.9;
    int maxTokens = 2048;

    core::Json toJson() const;
};

}  // namespace trinity::intelligence
