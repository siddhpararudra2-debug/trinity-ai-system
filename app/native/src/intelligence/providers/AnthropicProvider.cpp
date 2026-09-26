#include "trinity/intelligence/providers/AnthropicProvider.hpp"

namespace trinity::intelligence::providers {

AnthropicProvider::AnthropicProvider() = default;

ModelProviderInfo AnthropicProvider::info() const {
    ModelProviderInfo i;
    i.providerId = "anthropic";
    i.displayName = "Anthropic";
    i.available = false; // Not implemented yet
    i.endpoint = _endpoint;
    i.modelName = _modelName;
    i.configured = !_apiKey.empty();
    return i;
}

ModelResponse AnthropicProvider::generate(const ModelRequest& request) {
    ModelResponse response;
    response.success = false;
    response.requestId = request.requestId;
    response.model = _modelName;
    response.error = core::makeError(core::ErrorCode::CapabilityUnavailableError, 
        "Anthropic provider networking is not implemented in this build.", 
        "anthropic").toJson();
    return response;
}

core::Result<ModelResponse> AnthropicProvider::generatePlan(const ModelRequest& request) {
    return core::Result<ModelResponse>::fail(
        core::makeError(core::ErrorCode::CapabilityUnavailableError, "Not implemented", "anthropic"));
}

void AnthropicProvider::streamPlan(const ModelRequest& request, StreamCallback callback) {
    if (callback) {
        callback("", true);
    }
}

core::Status AnthropicProvider::configure(const core::Json& config) {
    if (config.contains("api_key") && config["api_key"].is_string()) {
        _apiKey = config["api_key"].get<std::string>();
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
    return core::okStatus();
}

} // namespace trinity::intelligence::providers
