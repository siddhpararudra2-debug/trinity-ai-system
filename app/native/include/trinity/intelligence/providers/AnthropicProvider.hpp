#pragma once

#include "trinity/intelligence/providers/HttpChatProvider.hpp"

namespace trinity::intelligence::providers {

// Anthropic messages transport: POST {endpoint} (default
// https://api.anthropic.com/v1/messages), headers x-api-key +
// anthropic-version, tool calls via the `trinity_execute` tool_use block.
// Secrets come only from TRINITY_MODEL_API_KEY (or a programmatic
// configure({"api_key": ...}), used by fixture tests) and are never stored
// or logged.
class AnthropicProvider : public HttpChatProvider {
public:
    AnthropicProvider();

protected:
    std::string defaultModel() const override;
    std::string defaultEndpoint() const override;
    ModelResponse doGenerate(const ModelRequest& request) override;
    bool usesAnthropicWire() const override;
};

} // namespace trinity::intelligence::providers
