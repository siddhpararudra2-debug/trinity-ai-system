#pragma once

// Model request envelope for the future LLM boundary.
// Transport-agnostic: providers translate these to/from their own
// wire formats. No provider is connected yet (see IModelProvider).

#include <string>
#include <vector>

#include "../core/Json.hpp"

namespace trinity::intelligence {

enum class MessageRole {
    System,
    User,
    Assistant,
    Tool,
};

std::string messageRoleToString(MessageRole role);
MessageRole messageRoleFromString(const std::string& role);

struct Message {
    MessageRole role = MessageRole::User;
    std::string content;

    core::Json toJson() const;
    static Message fromJson(const core::Json& json);
};

struct ModelRequest {
    std::string requestId;
    std::string prompt;
    std::vector<Message> messages;
    std::string model;  // requested model name, empty = provider default
    double temperature = 0.2;
    double topP = 0.9;
    int maxTokens = 2048;
    core::Json metadata = core::Json::object();

    core::Json toJson() const;
    static ModelRequest fromJson(const core::Json& json);
};

}  // namespace trinity::intelligence
