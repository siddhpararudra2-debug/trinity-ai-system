#include "trinity/intelligence/providers/HttpChatProvider.hpp"

#include "trinity/core/Config.hpp"
#include "trinity/core/Error.hpp"

namespace trinity::intelligence::providers {

HttpChatProvider::HttpChatProvider(std::string providerId, std::string displayName,
                                   bool requiresApiKey)
    : _providerId(std::move(providerId)),
      _displayName(std::move(displayName)),
      _requiresApiKey(requiresApiKey) {
    // defaultModel()/defaultEndpoint() are virtual: the dynamic type is
    // incomplete during base construction, so subclasses assign
    // _modelName/_endpoint from their own ctor bodies.
}

void HttpChatProvider::setTransport(std::shared_ptr<IHttpTransport> transport) {
    if (transport) {
        _transport = std::move(transport);
    }
}

bool HttpChatProvider::ready() const {
    // Truthful availability: enabled, key policy satisfied (a key is
    // required for openai/anthropic; local servers run keyless), and a
    // transport exists — which the constructor guarantees.
    return _enabled && _transport != nullptr && (!_requiresApiKey || !_apiKey.empty());
}

core::Status HttpChatProvider::configure(const core::Json& config) {
    if (config.contains("api_key") && config["api_key"].is_string()) {
        _apiKey = config["api_key"].get<std::string>();
    } else if (_requiresApiKey) {
        _apiKey = core::modelApiKeyFromEnv();
    }
    if (config.contains("model") && config["model"].is_string()) {
        _modelName = config["model"].get<std::string>();
    }
    if (config.contains("endpoint") && config["endpoint"].is_string()) {
        _endpoint = config["endpoint"].get<std::string>();
    }
    if (config.contains("enabled") && config["enabled"].is_boolean()) {
        _enabled = config["enabled"].get<bool>();
    }
    if (config.contains("timeout_ms") && config["timeout_ms"].is_number_integer()) {
        _timeoutMs = config["timeout_ms"].get<int>();
    }
    return core::okStatus();
}

ModelProviderInfo HttpChatProvider::info() const {
    ModelProviderInfo info;
    info.providerId = _providerId;
    info.displayName = _displayName;
    info.available = ready();
    info.endpoint = _endpoint;
    info.modelName = _modelName;
    info.configured = !_requiresApiKey || !_apiKey.empty();
    return info;
}

HttpReply HttpChatProvider::postJson(const std::string& url, const core::Json& body) const {
    HttpRequest request;
    request.url = url;
    request.body = body.dump();
    request.timeoutMs = _timeoutMs;
    // Bearer auth for OpenAI-style endpoints (openai, local over /v1) — only
    // when a key is configured; keyless local servers accept header-less
    // requests. Anthropic uses its own header set in anthropicRoundTrip.
    if (!_apiKey.empty()) {
        request.header = {{"Authorization", "Bearer " + _apiKey}};
    }
    return _transport->post(request);
}

ModelResponse HttpChatProvider::unavailableResponse(const ModelRequest& request,
                                                    const std::string& reason) const {
    ModelResponse response;
    response.success = false;
    response.requestId = request.requestId;
    response.operation = "generate";
    response.model = _modelName;
    response.error =
        core::makeError(core::ErrorCode::CapabilityUnavailableError, reason, _providerId).toJson();
    return response;
}

ModelResponse HttpChatProvider::generate(const ModelRequest& request) {
    if (!ready()) {
        return unavailableResponse(request, _providerId + " is not configured: " +
                                                (_requiresApiKey
                                                     ? "no API key (TRINITY_MODEL_API_KEY)"
                                                     : "provider disabled"));
    }
    return doGenerate(request);
}

core::Result<ModelResponse> HttpChatProvider::generatePlan(const ModelRequest& request) {
    // Refusals travel inside ok(): the Planner checks response.success and
    // logs "refused; executed nothing" — a hard Result failure would lose
    // the structured error text.
    return core::Result<ModelResponse>::ok(generate(request));
}

void HttpChatProvider::streamPlan(const ModelRequest& request, StreamCallback callback) {
    // Honest streaming scope: a full planning round trip emitted as one final
    // chunk. Incremental SSE is intentionally unshipped — do not pretend.
    if (!callback) {
        return;
    }
    const ModelResponse response = generate(request);
    if (response.hasToolCall && !response.toolCalls.empty()) {
        core::Json steps = core::Json::array();
        for (const auto& call : response.toolCalls) {
            steps.push_back(call.toJson());
        }
        callback(steps.dump(), true);
        return;
    }
    if (response.success) {
        callback(response.text.empty() ? response.toolCall.toJson().dump() : response.text, true);
        return;
    }
    callback(response.error.value("message", "model refused"), true);
}

namespace {

/// Map an HTTP-level failure (>=400 or provider error object) onto the
/// structured ModelResponse error — status preserved, message extracted,
/// secrets never echoed.
ModelResponse httpErrorResponse(const ModelRequest& request, long statusCode,
                                const core::Json& body, const std::string& source,
                                const std::string& model) {
    ModelResponse response;
    response.success = false;
    response.requestId = request.requestId;
    response.model = model;
    std::string message = "provider error (HTTP " + std::to_string(statusCode) + ")";
    if (body.contains("error") && body["error"].is_object() &&
        body["error"].contains("message") && body["error"]["message"].is_string()) {
        message = body["error"]["message"].get<std::string>();
    } else if (body.is_string() && !body.get<std::string>().empty()) {
        message = body.get<std::string>();
    }
    response.error = core::makeError(core::ErrorCode::CapabilityUnavailableError, message, source,
                                     core::Json{{"http_status", statusCode}})
                         .toJson();
    return response;
}

}  // namespace

ModelResponse HttpChatProvider::openAiRoundTrip(const ModelRequest& request,
                                                const std::string& source) const {
    const HttpReply reply = postJson(_endpoint, chat_wire::openAiBody(request, _modelName));
    ModelResponse response;
    response.requestId = request.requestId;
    response.model = _modelName;
    if (!reply.transportOk) {
        response.success = false;
        response.error = core::makeError(core::ErrorCode::CapabilityUnavailableError,
                                         "transport failure: " + reply.errorText, source)
                             .toJson();
        return response;
    }
    try {
        core::Json body = core::Json::parse(reply.body);
        if (reply.statusCode >= 400 || body.contains("error")) {
            response = httpErrorResponse(request, reply.statusCode, body, source, _modelName);
            return response;
        }
        chat_wire::mapOpenAiReply(body, response);
        return response;
    } catch (const std::exception& error) {
        // Provider boundary: wire failures must surface as structured
        // refusals — never as escapes through IModelProvider.
        response.success = false;
        response.error = core::makeError(core::ErrorCode::EngineExecutionError,
                                         "openai round trip failed: " + std::string(error.what()),
                                         source)
                             .toJson();
        return response;
    }
}

ModelResponse HttpChatProvider::anthropicRoundTrip(const ModelRequest& request,
                                                   const std::string& source) const {
    HttpRequest requestOut;
    requestOut.url = _endpoint;
    requestOut.body = chat_wire::anthroBody(request, _modelName).dump();
    requestOut.timeoutMs = _timeoutMs;
    requestOut.header = {{"x-api-key", _apiKey}, {"anthropic-version", "2023-06-01"}};
    const HttpReply reply = _transport->post(requestOut);

    ModelResponse response;
    response.requestId = request.requestId;
    response.model = _modelName;
    if (!reply.transportOk) {
        response.success = false;
        response.error = core::makeError(core::ErrorCode::CapabilityUnavailableError,
                                         "transport failure: " + reply.errorText, source)
                             .toJson();
        return response;
    }
    try {
        core::Json body = core::Json::parse(reply.body);
        if (reply.statusCode >= 400 || body.contains("error")) {
            response = httpErrorResponse(request, reply.statusCode, body, source, _modelName);
            return response;
        }
        chat_wire::mapAnthropicReply(body, response);
        return response;
    } catch (const std::exception& error) {
        response.success = false;
        response.error = core::makeError(core::ErrorCode::EngineExecutionError,
                                         "anthropic round trip failed: " + std::string(error.what()),
                                         source)
                             .toJson();
        return response;
    }
}

}  // namespace trinity::intelligence::providers
