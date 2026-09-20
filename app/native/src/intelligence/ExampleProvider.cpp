#include "trinity/intelligence/ExampleProvider.hpp"

#include "trinity/core/Config.hpp"
#include "trinity/core/Uuid.hpp"

namespace trinity::intelligence {

ExampleProvider::ExampleProvider(core::Json config) : config_(std::move(config)) {}

ModelProviderInfo ExampleProvider::info() const {
    ModelProviderInfo info;
    info.providerId = "example";  // DEV: rename to your provider id.
    info.displayName = "Example provider (not connected)";
    info.available = false;  // DEV: true only after configure() succeeds.
    info.configured = configured_;
    info.endpoint = config_.value("endpoint", "");
    info.modelName = config_.value("model", "");
    return info;
}

core::Status ExampleProvider::configure(const core::Json& config) {
    config_ = config;
    // DEV: read the secret here. Keep it in a local, never a member that
    // could be serialized, and never include it in logs or errors.
    const std::string apiKey = core::modelApiKeyFromEnv();
    if (apiKey.empty()) {
        // Honest refusal: without credentials we must not pretend to work.
        configured_ = false;
        return core::okStatus();
    }
    // DEV: validate endpoint/model and run a cheap auth check here.
    configured_ = true;
    return core::okStatus();
}

namespace {

ModelResponse notImplemented(const ModelRequest& request) {
    ModelResponse response;
    response.success = false;
    response.requestId = request.requestId;
    response.operation = "generate";
    response.error = core::makeError(
        core::ErrorCode::CapabilityUnavailableError,
        "ExampleProvider has no transport: copy this class and implement generate()",
        "model", {{"provider", "example"}})
                         .toJson();
    return response;
}

}  // namespace

ModelResponse ExampleProvider::generate(const ModelRequest& request) {
    // DEV: replace this stub with real transport:
    //   1. Convert request.messages (role/content) to your API's format.
    //   2. POST with timeout config_.value("timeout_ms", 30000).
    //   3. Parse the reply into response.text and response.toolCalls —
    //      each ToolCall is {engine, operation, parameters} and maps 1:1
    //      onto JobManager::runSync. Validate engine/operation names
    //      against the registry BEFORE returning; drop anything unknown
    //      and record it in response.error rather than emitting it.
    //   4. Set responseId = core::newUuid(), model = your model name.
    return notImplemented(request);
}

core::Result<ModelResponse> ExampleProvider::generatePlan(const ModelRequest& request) {
    return core::Result<ModelResponse>::ok(generate(request));
}

void ExampleProvider::streamPlan(const ModelRequest& request, StreamCallback callback) {
    // DEV: call the streaming endpoint and invoke callback(chunk, false)
    // per chunk, then callback("", true) exactly once at the end.
    const ModelResponse refusal = generate(request);
    callback(refusal.error.value("message", "unavailable"), true);
}

}  // namespace trinity::intelligence
