#pragma once

// EXAMPLE provider skeleton — copy, don't instantiate.
//
// How another developer plugs an LLM in (5 steps):
//   1. Copy ExampleProvider.{hpp,cpp} to MyProvider.{hpp,cpp} and rename
//      the class, provider id, and display name.
//   2. Implement configure(): read TRINITY_MODEL_API_KEY via
//      core::modelApiKeyFromEnv() (never add a key member, never log it).
//   3. Implement generate(): translate request.messages to your wire
//      format, call your endpoint, and parse back ONLY {text, toolCalls}
//      — the Planner executes tool calls, never the provider.
//   4. Implement streamPlan(): emit text chunks, final=true exactly once.
//   5. Register once at startup:
//        ModelProviderFactory::instance().registerProvider(
//            "mine", [](const core::Json& cfg) {
//                return std::make_shared<MyProvider>(cfg); });
//      then set TRINITY_MODEL_PROVIDER=mine.
//
// This file compiles so the sample never rots, but it is deliberately
// NOT registered with the factory: selecting it is impossible until a
// developer copies and registers their own copy. Its generate() refuses
// truthfully with guidance instead of fake intelligence.

#include <string>

#include "IModelProvider.hpp"

namespace trinity::intelligence {

class ExampleProvider : public IModelProvider {
public:
    explicit ExampleProvider(core::Json config = core::Json::object());

    ModelProviderInfo info() const override;
    ModelResponse generate(const ModelRequest& request) override;
    core::Result<ModelResponse> generatePlan(const ModelRequest& request) override;
    void streamPlan(const ModelRequest& request, StreamCallback callback) override;
    core::Status configure(const core::Json& config) override;

private:
    core::Json config_ = core::Json::object();
    bool configured_ = false;
};

}  // namespace trinity::intelligence
