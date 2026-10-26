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

core::Json openAiBody(const ModelRequest& request, const std::string& model, bool stream) {
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
    if (stream) {
        body["stream"] = true;
    }
    return body;
}

core::Json anthroBody(const ModelRequest& request, const std::string& model, bool stream) {
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
    if (stream) {
        body["stream"] = true;
    }
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

// ---------------------------------------------------------------------------
// SseDeltaParser — incremental SSE parsing for streamPlan
// ---------------------------------------------------------------------------

SseDeltaParser::SseDeltaParser(bool anthropicWire)
    : _anthropicWire(anthropicWire) {}

void SseDeltaParser::feed(const std::string& chunk) {
    std::string buffer = std::move(_pendingLine);
    buffer += chunk;
    size_t start = 0;
    for (;;) {
        const size_t nl = buffer.find('\n', start);
        if (nl == std::string::npos) {
            break;
        }
        std::string line = buffer.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        handleLine(line);
        start = nl + 1;
    }
    _pendingLine = buffer.substr(start);
}

void SseDeltaParser::handleLine(const std::string& line) {
    // `event:`/comment lines carry no payload for either wire dialect —
    // everything lives in the data: line. Empty and partial lines are noise.
    if (line.rfind("data:", 0) != 0) {
        return;
    }
    std::string payload = line.substr(5);
    if (!payload.empty() && payload.front() == ' ') {
        payload.erase(0, 1);
    }
    if (payload == "[DONE]") {
        _sawDone = true;
        return;
    }
    core::Json event;
    try {
        event = core::Json::parse(payload);
    } catch (const std::exception&) {
        return;  // malformed line: ignore, never invent a delta from garbage
    }
    _sawEvents = true;

    // Truthful error surfacing: either dialect can stream a failure object.
    if (event.is_object() && (event.contains("error") || event.value("type", "") == "error")) {
        const core::Json& err = event.contains("error") ? event["error"] : event;
        _errorMessage = err.is_object() ? err.value("message", "model stream error")
                                        : "model stream error";
        _sawError = true;
        return;
    }

    if (_anthropicWire) {
        const std::string type = event.value("type", "");
        if (type == "message_start" && event.contains("message") &&
            event["message"].is_object()) {
            const core::Json& message = event["message"];
            if (message.contains("id") && message["id"].is_string()) {
                _responseId = message["id"].get<std::string>();
            }
            if (message.contains("model") && message["model"].is_string()) {
                _modelName = message["model"].get<std::string>();
            }
            if (message.contains("usage") && message["usage"].is_object()) {
                _usage = message["usage"];
            }
        } else if (type == "content_block_start" && event.contains("content_block") &&
                   event["content_block"].is_object() &&
                   event["content_block"].value("type", "") == "tool_use") {
            // A tool_use block opens; its name/id arrive whole, arguments
            // stream later as input_json_delta fragments.
            _toolCallId = event["content_block"].value("id", "");
            _toolName = event["content_block"].value("name", "");
        } else if (type == "content_block_delta" && event.contains("delta") &&
                   event["delta"].is_object()) {
            const core::Json& delta = event["delta"];
            const std::string deltaType = delta.value("type", "");
            if (deltaType == "text_delta" && delta.contains("text") &&
                delta["text"].is_string()) {
                _text += delta["text"].get<std::string>();
            } else if (deltaType == "input_json_delta" && delta.contains("partial_json") &&
                       delta["partial_json"].is_string()) {
                _arguments += delta["partial_json"].get<std::string>();
            }
        }
        return;
    }

    // OpenAI chat-completions shape: one choice, delta either text or
    // tool-call fragments; id/model/usage ride developmentally on chunks.
    if (event.contains("id") && event["id"].is_string()) {
        _responseId = event["id"].get<std::string>();
    }
    if (event.contains("model") && event["model"].is_string()) {
        _modelName = event["model"].get<std::string>();
    }
    if (event.contains("usage") && event["usage"].is_object()) {
        _usage = event["usage"];
    }
    if (event.contains("choices") && event["choices"].is_array() &&
        !event["choices"].empty() && event["choices"].at(0).contains("delta") &&
        event["choices"].at(0)["delta"].is_object()) {
        const core::Json& delta = event["choices"].at(0)["delta"];
        if (delta.contains("content") && delta["content"].is_string()) {
            _text += delta["content"].get<std::string>();
        }
        if (delta.contains("tool_calls") && delta["tool_calls"].is_array() &&
            !delta["tool_calls"].empty() && delta["tool_calls"].at(0).is_object()) {
            const core::Json& callDelta = delta["tool_calls"].at(0);
            if (callDelta.contains("id") && callDelta["id"].is_string()) {
                _toolCallId = callDelta["id"].get<std::string>();
            }
            if (callDelta.contains("function") && callDelta["function"].is_object()) {
                const core::Json& fn = callDelta["function"];
                const std::string name = fn.value("name", "");
                if (!name.empty()) {
                    _toolName += name;
                }
                if (fn.contains("arguments") && fn["arguments"].is_string()) {
                    _arguments += fn["arguments"].get<std::string>();
                }
            }
        }
    }
}

ModelResponse SseDeltaParser::finish(const std::string& source) {
    ModelResponse response;
    if (_sawError) {
        response.success = false;
        response.error = core::makeError(core::ErrorCode::EngineExecutionError, _errorMessage,
                                         source)
                             .toJson();
        return response;
    }
    response.responseId = _responseId;
    response.model = _modelName;
    response.usage = _usage;
    response.text = _text;
    // Only trinity_execute may execute — mirroring mapOpenAiReply/mapAnthropicReply.
    if (!_toolName.empty()) {
        core::Json arguments = core::Json::object();
        if (!_arguments.empty()) {
            try {
                arguments = core::Json::parse(_arguments);
            } catch (const std::exception&) {
                response.success = false;
                response.error =
                    core::makeError(core::ErrorCode::EngineExecutionError,
                                    "tool call arguments were not valid JSON", source)
                        .toJson();
                return response;
            }
        }
        if (_toolName == kToolName) {
            response.toolCalls.push_back(parseToolArguments(_toolCallId, arguments));
            response.hasToolCall = true;
            response.toolCall = response.toolCalls.front();
        }
    }
    response.success = true;
    return response;
}
}  // namespace chat_wire

}  // namespace trinity::intelligence::providers
