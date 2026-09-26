#include "trinity/intelligence/providers/LocalModelProvider.hpp"

namespace trinity::intelligence::providers {

LocalModelProvider::LocalModelProvider() = default;

ModelProviderInfo LocalModelProvider::info() const {
    ModelProviderInfo i;
    i.providerId = "local";
    i.displayName = "Local Model";
    i.available = false; // Not implemented yet
    i.endpoint = _endpoint;
    i.modelName = _modelName;
    i.configured = true; // Local provider doesn't strictly need API key
    return i;
}

ModelResponse LocalModelProvider::generate(const ModelRequest& request) {
    ModelResponse response;
    response.success = false;
    response.requestId = request.requestId;
    response.model = _modelName;
    response.error = core::makeError(core::ErrorCode::CapabilityUnavailableError, 
        "Local model provider networking is not implemented in this build.", 
        "local").toJson();
    return response;
}

core::Result<ModelResponse> LocalModelProvider::generatePlan(const ModelRequest& request) {
    return core::Result<ModelResponse>::fail(
        core::makeError(core::ErrorCode::CapabilityUnavailableError, "Not implemented", "local"));
}

void LocalModelProvider::streamPlan(const ModelRequest& request, StreamCallback callback) {
    if (callback) {
        callback("", true);
    }
}

core::Status LocalModelProvider::configure(const core::Json& config) {
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
