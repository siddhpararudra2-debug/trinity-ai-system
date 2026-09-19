#include "trinity/intelligence/ToolCall.hpp"

namespace trinity::intelligence {

core::Json ToolCall::toJson() const {
    return core::Json{{"engine", engine}, {"operation", operation}, {"parameters", parameters}};
}

ToolCall ToolCall::fromJson(const core::Json& json) {
    ToolCall call;
    call.engine = json.value("engine", "");
    call.operation = json.value("operation", "");
    call.parameters = json.value("parameters", core::Json::object());
    return call;
}

}  // namespace trinity::intelligence
