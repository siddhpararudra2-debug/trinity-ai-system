#include "trinity/intelligence/providers/AnthropicProvider.hpp"

namespace trinity::intelligence::providers {

AnthropicProvider::AnthropicProvider()
    : HttpChatProvider("anthropic", "Anthropic", /*requiresApiKey=*/true) {
    _modelName = defaultModel();
    _endpoint = defaultEndpoint();
}

std::string AnthropicProvider::defaultModel() const {
    return "claude-sonnet-4-20250514";
}

std::string AnthropicProvider::defaultEndpoint() const {
    return "https://api.anthropic.com/v1/messages";
}

ModelResponse AnthropicProvider::doGenerate(const ModelRequest& request) {
    return anthropicRoundTrip(request, "anthropic");
}

}  // namespace trinity::intelligence::providers
