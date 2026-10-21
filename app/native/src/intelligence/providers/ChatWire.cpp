#include "trinity/intelligence/providers/HttpChatProvider.hpp"

namespace trinity::intelligence::providers {
namespace chat_wire {

// ---------------------------------------------------------------------------
// The single tool an LLM may call. Arguments are exactly a ToolCall:
// {engine, operation, parameters}. Unknown engines fail later in the
// workflow executor with a truthful EngineNotFound — providers never fake
// validation they cannot do.
// ---------------------------------------------------------------------------

core::Json planSystemPrompt() {
    // NOTE: Json{"literal"} would produce an ARRAY wrapping the string (nlohmann's
    // braced-init always takes the initializer-list constructor). The explicit
    // std::string ctor keeps this a JSON string.
    return core::Json(std::string{
        "You are the planning model of Trinity, an engineering operating system. "
        "The ONLY thing you may do is call the tool trinity_execute — never describe "
        "work in prose. Every call maps one engine operation: engine (e.g. math, cad, "
        "pcb, firmware, vision, research, simulation, robotics), operation (which "
        "capability of that engine), and parameters (the operation's input object). "
        "Prefer a single precise call; emit multiple calls only for a genuine "
        "sequence. If the request cannot be mapped to any engine operation, do not "
        "call the tool and answer in one short sentence instead."});
}

core::Json openAiToolDefinition() {
    return core::Json::array(
        {{{"type", "function"},
          {"function",
           {{"name", kToolName},
            {"description",
             "Execute one Trinity engine operation and return its structured result."},
            {"parameters",
             {{"type", "object"},
              {"properties",
               {{"engine", {{"type", "string"}}},
                {"operation", {{"type", "string"}}},
                {"parameters", {{"type", "object"}}}}},
              {"required", core::Json::array({"engine", "operation", "parameters"})}}}}}}});
}

core::Json anthroToolDefinition() {
    return core::Json::array(
        {{{"name", kToolName},
          {"description",
           "Execute one Trinity engine operation and return its structured result."},
          {"input_schema",
           {{"type", "object"},
            {"properties",
             {{"engine", {{"type", "string"}}},
              {"operation", {{"type", "string"}}},
              {"parameters", {{"type", "object"}}}}},
            {"required", core::Json::array({"engine", "operation", "parameters"})}}}}});
}

// ---------------------------------------------------------------------------
// Body builders
// ---------------------------------------------------------------------------

namespace {

// Roles: System merges into the system text; Tool becomes an honest
// "[tool result] " user message; User/Assistant map unchanged.
void splitMessages(const ModelRequest& request, core::Json& systemText, core::Json& messages) {
    std::string system;
    messages = core::Json::array();
    if (!request.prompt.empty()) {
        messages.push_back({{"role", "user"}, {"content", request.prompt}});
    }
    if (!system.empty()) {
        // (kept intentionally empty; loop below appends)
    }
    for (const auto& message : request.messages) {
        switch (message.role) {
            case MessageRole::System:
                if (!system.empty()) {
                    system += "\n";
                }
                system += message.content;
                break;
            case MessageRole::User:
                messages.push_back({{"role", "user"}, {"content", message.content}});
                break;
            case MessageRole::Assistant:
                messages.push_back({{"role", "assistant"}, {"content", message.content}});
                break;
            case MessageRole::Tool:
                messages.push_back(
                    {{"role", "user"}, {"content", "[tool result] " + message.content}});
                break;
        }
    }
    systemText = system;
}

ToolCall parseToolArguments(const std::string& toolCallId, const core::Json& arguments) {
    ToolCall call;
    call.toolCallId = toolCallId;
    if (arguments.is_object()) {
        call.engine = arguments.value("engine", "");
        call.operation = arguments.value("operation", "");
        if (arguments.contains("parameters") && arguments["parameters"].is_object()) {
            call.parameters = arguments["parameters"];
        }
    }
    return call;
}

}  // namespace

core::Json openAiBody(const ModelRequest& request, const std::string& model) {
    core::Json systemText;
    core::Json messages;
    splitMessages(request, systemText, messages);
    std::string system = systemText.get<std::string>();
    if (!system.empty()) {
        system += "\n";
    }
    system += planSystemPrompt().get<std::string>();

    core::Json all = core::Json::array();
    all.push_back({{"role", "system"}, {"content", system}});
    for (const auto& item : messages) {
        all.push_back(item);
    }

    core::Json body = core::Json::object();
    body["model"] = model;
    body["temperature"] = request.temperature;
    body["top_p"] = request.topP;
    body["max_tokens"] = request.maxTokens;
    body["messages"] = all;
    body["tools"] = openAiToolDefinition();
    body["tool_choice"] = "auto";
    return body;
}

core::Json anthroBody(const ModelRequest& request, const std::string& model) {
    core::Json systemText;
    core::Json messages;
    splitMessages(request, systemText, messages);
    std::string system = systemText.get<std::string>();
    if (!system.empty()) {
        system += "\n";
    }
    system += planSystemPrompt().get<std::string>();

    core::Json body = core::Json::object();
    body["model"] = model;
    body["max_tokens"] = request.maxTokens > 0 ? request.maxTokens : 1024;
    body["temperature"] = request.temperature;
    body["top_p"] = request.topP;
    body["system"] = system;
    body["messages"] = messages;
    body["tools"] = anthroToolDefinition();
    return body;
}

// ---------------------------------------------------------------------------
// Reply parsing
// ---------------------------------------------------------------------------

void mapOpenAiReply(const core::Json& body, ModelResponse& out) {
    if (body.contains("id") && body["id"].is_string()) {
        out.responseId = body["id"].get<std::string>();
    }
    if (body.contains("model") && body["model"].is_string()) {
        out.model = body["model"].get<std::string>();
    }
    if (body.contains("usage") && body["usage"].is_object()) {
        out.usage = body["usage"];
    }
    core::Json choice;
    if (body.contains("choices") && body["choices"].is_array() && !body["choices"].empty() &&
        body["choices"].at(0).contains("message") && body["choices"].at(0)["message"].is_object()) {
        choice = body["choices"].at(0)["message"];
    }
    if (!choice.is_object()) {
        out.success = false;
        out.error = core::makeError(core::ErrorCode::EngineExecutionError,
                                    "OpenAI reply had no choices[0].message", "openai")
                        .toJson();
        return;
    }
    if (choice.contains("tool_calls") && choice["tool_calls"].is_array()) {
        for (const auto& entry : choice["tool_calls"]) {
            if (!entry.is_object() || !entry.contains("function") ||
                !entry["function"].is_object()) {
                continue;
            }
            const std::string name = entry["function"].value("name", "");
            if (name != kToolName) {
                continue;  // only trinity_execute may execute
            }
            core::Json arguments = core::Json::object();
            if (entry["function"].contains("arguments")) {
                const auto& raw = entry["function"]["arguments"];
                if (raw.is_string()) {
                    try {
                        arguments = core::Json::parse(raw.get<std::string>());
                    } catch (const std::exception&) {
                        out.success = false;
                        out.error = core::makeError(
                                        core::ErrorCode::EngineExecutionError,
                                        "tool call arguments were not valid JSON", "openai")
                                        .toJson();
                        return;
                    }
                } else if (raw.is_object()) {
                    arguments = raw;
                }
            }
            out.toolCalls.push_back(
                parseToolArguments(entry.value("id", ""), arguments));
        }
        out.hasToolCall = !out.toolCalls.empty();
        if (out.hasToolCall) {
            out.toolCall = out.toolCalls.front();
        }
    }
    if (choice.contains("content") && choice["content"].is_string()) {
        out.text = choice["content"].get<std::string>();
    }
    out.success = true;
}

void mapAnthropicReply(const core::Json& body, ModelResponse& out) {
    if (body.contains("id") && body["id"].is_string()) {
        out.responseId = body["id"].get<std::string>();
    }
    if (body.contains("model") && body["model"].is_string()) {
        out.model = body["model"].get<std::string>();
    }
    if (body.contains("usage") && body["usage"].is_object()) {
        out.usage = body["usage"];
    }
    if (!body.contains("content") || !body["content"].is_array()) {
        out.success = false;
        out.error = core::makeError(core::ErrorCode::EngineExecutionError,
                                    "Anthropic reply had no content array", "anthropic")
                        .toJson();
        return;
    }
    for (const auto& block : body["content"]) {
        if (!block.is_object()) {
            continue;
        }
        const std::string type = block.value("type", "");
        if (type == "text" && block.contains("text") && block["text"].is_string()) {
            out.text += block["text"].get<std::string>();
        } else if (type == "tool_use") {
            const std::string name = block.value("name", "");
            if (name != kToolName) {
                continue;  // only trinity_execute may execute
            }
            out.toolCalls.push_back(
                parseToolArguments(block.value("id", ""),
                                   block.contains("input") ? block["input"] : core::Json::object()));
        }
    }
    out.hasToolCall = !out.toolCalls.empty();
    if (out.hasToolCall) {
        out.toolCall = out.toolCalls.front();
    }
    out.success = true;
}

}  // namespace chat_wire
}  // namespace trinity::intelligence::providers
