#include "trinity/intelligence/providers/OpenAIProvider.hpp"

namespace trinity::intelligence::providers {

OpenAIProvider::OpenAIProvider() = default;

ModelProviderInfo OpenAIProvider::info() const {
    ModelProviderInfo i;
    i.providerId = "openai";
    i.displayName = "OpenAI";
    i.available = false; // Not implemented yet
    i.endpoint = _endpoint;
    i.modelName = _modelName;
    i.configured = !_apiKey.empty();
    return i;
}

ModelResponse OpenAIProvider::generate(const ModelRequest& request) {
    ModelResponse response;
    response.success = false;
    response.requestId = request.requestId;
    response.model = _modelName;
    response.error = core::makeError(core::ErrorCode::CapabilityUnavailableError, 
        "OpenAI provider networking is not implemented in this build.", 
        "openai").toJson();
    return response;
}

core::Result<ModelResponse> OpenAIProvider::generatePlan(const ModelRequest& request) {
    return core::Result<ModelResponse>::fail(
        core::makeError(core::ErrorCode::CapabilityUnavailableError, "Not implemented", "openai"));
}

void OpenAIProvider::streamPlan(const ModelRequest& request, StreamCallback callback) {
    if (callback) {
        callback("", true);
    }
}

core::Status OpenAIProvider::configure(const core::Json& config) {
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
