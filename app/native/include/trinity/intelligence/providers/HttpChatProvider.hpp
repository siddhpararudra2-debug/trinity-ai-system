#pragma once

// Shared HTTP plumbing for chat-completions style model providers.
//
// HttpChatProvider owns: transport injection (contract tests never touch
// WinHTTP), the config keys (api_key / model / endpoint / enabled /
// timeout_ms), the TRINITY_MODEL_API_KEY environment contract, truthful
// availability reporting, and the full IModelProvider surface — generate,
// generatePlan and streamPlan are implemented here once. Subclasses only
// pick defaults and a wire round trip: OpenAIProvider and LocalModelProvider
// share the OpenAI chat-completions format; AnthropicProvider uses the
// Anthropic messages format.

#include <memory>
#include <string>

#include "trinity/intelligence/HttpTransport.hpp"
#include "trinity/intelligence/IModelProvider.hpp"

namespace trinity::intelligence::providers {

class HttpChatProvider : public IModelProvider {
public:
    /// providerId/displayName for ModelProviderInfo; requiresApiKey=false lets
    /// keyless local servers (Ollama / LM Studio / llama.cpp-server) run,
    /// matching reality rather than demanding a key that is never checked.
    HttpChatProvider(std::string providerId, std::string displayName, bool requiresApiKey);

    /// Test hook: inject a fixture transport; never needed in production.
    void setTransport(std::shared_ptr<IHttpTransport> transport);

    /// Config keys: api_key (tests/programmatic only), model, endpoint,
    /// enabled, timeout_ms. When api_key is absent, falls back to
    /// core::modelApiKeyFromEnv() for providers that require one.
    core::Status configure(const core::Json& config) override;

    // ---- IModelProvider surface, implemented once for every subclass ----
    ModelProviderInfo info() const override;
    ModelResponse generate(const ModelRequest& request) final;
    core::Result<ModelResponse> generatePlan(const ModelRequest& request) final;
    void streamPlan(const ModelRequest& request, StreamCallback callback) final;

    bool ready() const;  // enabled + key policy satisfied + transport present

protected:
    /// Wire-specific round trip. Called only when ready() holds.
    virtual ModelResponse doGenerate(const ModelRequest& request) = 0;

    virtual std::string defaultModel() const = 0;
    virtual std::string defaultEndpoint() const = 0;

    HttpReply postJson(const std::string& url, const core::Json& body) const;
    ModelResponse unavailableResponse(const ModelRequest& request, const std::string& reason) const;

    /// OpenAI chat-completions round trip (used by openai and local).
    ModelResponse openAiRoundTrip(const ModelRequest& request, const std::string& source) const;
    /// Anthropic messages round trip.
    ModelResponse anthropicRoundTrip(const ModelRequest& request, const std::string& source) const;

    std::string _apiKey;
    std::string _modelName;  // subclasses assign defaultModel() in their ctor
    std::string _endpoint;   // subclasses assign defaultEndpoint() in their ctor
    int _timeoutMs = 30000;
    bool _enabled = true;

private:
    std::string _providerId;
    std::string _displayName;
    bool _requiresApiKey = true;
    std::shared_ptr<IHttpTransport> _transport = std::make_shared<DefaultHttpTransport>();
};

namespace chat_wire {

/// The single tool an LLM may call: arguments are exactly a ToolCall
/// {engine, operation, parameters}. Unknown engines fail later in the
/// workflow executor with a truthful EngineNotFound — providers never fake
/// validation they cannot do. Constant name so both wire formats agree.
constexpr const char* kToolName = "trinity_execute";

core::Json planSystemPrompt();
core::Json openAiToolDefinition();
core::Json anthroToolDefinition();

/// Roles: System merges into system text; Tool becomes an honest
/// "[tool result] " user message; User/Assistant map unchanged.
core::Json openAiBody(const ModelRequest& request, const std::string& model);
core::Json anthroBody(const ModelRequest& request, const std::string& model);

/// Parse helpers: fill text/toolCalls/toolCall/usage/responseId/model.
void mapOpenAiReply(const core::Json& body, ModelResponse& out);
void mapAnthropicReply(const core::Json& body, ModelResponse& out);

}  // namespace chat_wire

}  // namespace trinity::intelligence::providers
