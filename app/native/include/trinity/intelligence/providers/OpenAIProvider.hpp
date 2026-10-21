#pragma once

#include "trinity/intelligence/providers/HttpChatProvider.hpp"

namespace trinity::intelligence::providers {

// OpenAI chat-completions transport: POST {endpoint} (default
// https://api.openai.com/v1/chat/completions), Authorization: Bearer, tool
// calls via the `trinity_execute` function tool. Secrets come only from
// TRINITY_MODEL_API_KEY (or a programmatic configure({"api_key": ...}), used
// by fixture tests) and are never stored or logged.
class OpenAIProvider : public HttpChatProvider {
public:
    OpenAIProvider();

protected:
    std::string defaultModel() const override;
    std::string defaultEndpoint() const override;
    ModelResponse doGenerate(const ModelRequest& request) override;
};

} // namespace trinity::intelligence::providers
