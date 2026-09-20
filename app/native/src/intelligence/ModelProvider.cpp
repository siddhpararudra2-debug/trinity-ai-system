#include "trinity/intelligence/IModelProvider.hpp"

#include "trinity/core/Uuid.hpp"

namespace trinity::intelligence {

ModelProviderInfo NullModelProvider::info() const {
    ModelProviderInfo info;
    info.providerId = "null";
    info.displayName = "No model connected";
    info.available = false;
    info.configured = false;
    return info;
}

namespace {

ModelResponse refusal(const ModelRequest& request) {
    ModelResponse response;
    response.success = false;
    response.requestId = request.requestId;
    response.operation = "generate";
    response.error = core::makeError(core::ErrorCode::CapabilityUnavailableError,
                                     "No model provider is connected", "model")
                         .toJson();
    return response;
}

}  // namespace

ModelResponse NullModelProvider::generate(const ModelRequest& request) {
    return refusal(request);
}

core::Result<ModelResponse> NullModelProvider::generatePlan(const ModelRequest& request) {
    return core::Result<ModelResponse>::ok(refusal(request));
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
