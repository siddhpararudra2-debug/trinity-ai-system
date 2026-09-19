#include "trinity/intelligence/ModelResponse.hpp"

namespace trinity::intelligence {

core::Json ModelResponse::toJson() const {
    core::Json json{{"success", success}, {"text", text}, {"has_tool_call", hasToolCall}};
    json["tool_call"] = hasToolCall ? toolCall.toJson() : core::Json(nullptr);
    json["error"] = error;
    return json;
}

}  // namespace trinity::intelligence
