#include "trinity/intelligence/ModelRequest.hpp"

namespace trinity::intelligence {

std::string messageRoleToString(MessageRole role) {
    switch (role) {
        case MessageRole::System:
            return "system";
        case MessageRole::Assistant:
            return "assistant";
        case MessageRole::Tool:
            return "tool";
        case MessageRole::User:
        default:
            return "user";
    }
}

MessageRole messageRoleFromString(const std::string& role) {
    if (role == "system") return MessageRole::System;
    if (role == "assistant") return MessageRole::Assistant;
    if (role == "tool") return MessageRole::Tool;
    return MessageRole::User;
}

core::Json Message::toJson() const {
    return core::Json{{"role", messageRoleToString(role)}, {"content", content}};
}

Message Message::fromJson(const core::Json& json) {
    Message message;
    message.role = messageRoleFromString(json.value("role", "user"));
    message.content = json.value("content", "");
    return message;
}

core::Json ModelRequest::toJson() const {
    core::Json messagesJson = core::Json::array();
    for (const auto& message : messages) {
        messagesJson.push_back(message.toJson());
    }
    return core::Json{{"request_id", requestId},
                      {"prompt", prompt},
                      {"messages", messagesJson},
                      {"model", model},
                      {"temperature", temperature},
                      {"top_p", topP},
                      {"max_tokens", maxTokens},
                      {"metadata", metadata}};
}

ModelRequest ModelRequest::fromJson(const core::Json& json) {
    ModelRequest request;
    request.requestId = json.value("request_id", "");
    request.prompt = json.value("prompt", "");
    request.model = json.value("model", "");
    request.temperature = json.value("temperature", 0.2);
    request.topP = json.value("top_p", 0.9);
    request.maxTokens = json.value("max_tokens", 2048);
    request.metadata = json.value("metadata", core::Json::object());
    if (json.contains("messages") && json["messages"].is_array()) {
        for (const auto& item : json["messages"]) {
            request.messages.push_back(Message::fromJson(item));
        }
    }
    return request;
}

}  // namespace trinity::intelligence
