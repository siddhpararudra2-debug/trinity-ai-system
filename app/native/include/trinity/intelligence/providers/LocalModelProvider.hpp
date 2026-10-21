#pragma once

#include "trinity/intelligence/providers/HttpChatProvider.hpp"

namespace trinity::intelligence::providers {

// Local/self-hosted models over an OpenAI-compatible endpoint. The default is
// Ollama's /v1 surface (http://localhost:11434/v1/chat/completions), which
// LM Studio and llama.cpp-server also serve; one wire path, three hosts.
// No API key is required, matching keyless local servers.
class LocalModelProvider : public HttpChatProvider {
public:
    LocalModelProvider();

protected:
    std::string defaultModel() const override;
    std::string defaultEndpoint() const override;
    ModelResponse doGenerate(const ModelRequest& request) override;
};

} // namespace trinity::intelligence::providers
