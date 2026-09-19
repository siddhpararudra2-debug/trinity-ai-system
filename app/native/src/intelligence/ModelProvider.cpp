#include "trinity/intelligence/IModelProvider.hpp"

namespace trinity::intelligence {

ModelProviderInfo NullModelProvider::info() const {
    ModelProviderInfo info;
    info.providerId = "null";
    info.displayName = "No model connected";
    info.available = false;
    info.configured = false;
    return info;
}

core::Result<ModelResponse> NullModelProvider::generatePlan(const ModelRequest& /*request*/) {
    ModelResponse response;
    response.success = false;
    response.error = core::Json{{"code", "capability_unavailable"},
                                {"message", "No model provider is connected"},
                                {"details", core::Json::object()}};
    return core::Result<ModelResponse>::ok(response);
}

void NullModelProvider::streamPlan(const ModelRequest& /*request*/,
                                   StreamCallback callback) {
    // Single terminal chunk carrying the same refusal as generatePlan.
    callback(R"({"error":"No model provider is connected"})", true);
}

core::Status NullModelProvider::configure(const core::Json& /*config*/) {
    return core::okStatus();
}

}  // namespace trinity::intelligence
