#include "trinity/intelligence/providers/LocalModelProvider.hpp"

namespace trinity::intelligence::providers {

LocalModelProvider::LocalModelProvider()
    : HttpChatProvider("local", "Local Model", /*requiresApiKey=*/false) {
    _modelName = defaultModel();
    _endpoint = defaultEndpoint();
}

std::string LocalModelProvider::defaultModel() const {
    return "llama-3-8b";
}

// One wire path for every keyless self-hosted option: Ollama, LM Studio and
// llama.cpp-server all expose the OpenAI-compatible /v1/chat/completions.
std::string LocalModelProvider::defaultEndpoint() const {
    return "http://localhost:11434/v1/chat/completions";
}

ModelResponse LocalModelProvider::doGenerate(const ModelRequest& request) {
    return openAiRoundTrip(request, "local");
}

}  // namespace trinity::intelligence::providers
