#include "trinity/intelligence/ModelResponse.hpp"

namespace trinity::intelligence {

core::Json ModelResponse::toJson() const {
    core::Json json{{"success", success},
                    {"response_id", responseId},
                    {"request_id", requestId},
                    {"model", model},
                    {"operation", operation},
                    {"text", text},
                    {"has_tool_call", hasToolCall},
                    {"usage", usage}};
    json["tool_call"] = hasToolCall ? toolCall.toJson() : core::Json(nullptr);
    core::Json callsJson = core::Json::array();
    for (const auto& call : toolCalls) {
        callsJson.push_back(call.toJson());
    }
    json["tool_calls"] = callsJson;
    json["error"] = error;
    return json;
}

ModelResponse ModelResponse::fromJson(const core::Json& json) {
    ModelResponse response;
    response.success = json.value("success", false);
    response.responseId = json.value("response_id", "");
    response.requestId = json.value("request_id", "");
    response.model = json.value("model", "");
    response.operation = json.value("operation", "generate");
    response.text = json.value("text", "");
    response.hasToolCall = json.value("has_tool_call", false);
    response.usage = json.value("usage", core::Json::object());
    response.error = json.value("error", core::Json(nullptr));
    if (json.contains("tool_call") && !json["tool_call"].is_null()) {
        response.toolCall = ToolCall::fromJson(json["tool_call"]);
        response.hasToolCall = true;
    }
    if (json.contains("tool_calls") && json["tool_calls"].is_array()) {
        for (const auto& item : json["tool_calls"]) {
            response.toolCalls.push_back(ToolCall::fromJson(item));
        }
        if (!response.toolCalls.empty()) {
            response.hasToolCall = true;
            response.toolCall = response.toolCalls.front();
        }
    }
    return response;
}

}  // namespace trinity::intelligence
