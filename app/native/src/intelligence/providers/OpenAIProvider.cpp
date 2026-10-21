#include "trinity/intelligence/providers/OpenAIProvider.hpp"

namespace trinity::intelligence::providers {

OpenAIProvider::OpenAIProvider()
    : HttpChatProvider("openai", "OpenAI", /*requiresApiKey=*/true) {
    _modelName = defaultModel();
    _endpoint = defaultEndpoint();
}

std::string OpenAIProvider::defaultModel() const {
    return "gpt-4o";
}

std::string OpenAIProvider::defaultEndpoint() const {
    return "https://api.openai.com/v1/chat/completions";
}

ModelResponse OpenAIProvider::doGenerate(const ModelRequest& request) {
    return openAiRoundTrip(request, "openai");
}

}  // namespace trinity::intelligence::providers
