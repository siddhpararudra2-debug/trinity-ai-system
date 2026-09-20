#pragma once

// IModelProvider: the future proprietary LLM boundary.
//
// Implementations for OpenAI, Anthropic, local models, or a custom
// Trinity model plug in here. NOTHING in this header implements
// intelligence: the only shipped implementation is NullModelProvider,
// which truthfully reports unavailable and refuses to plan, so the
// whole application runs without any model connected.

#include <functional>
#include <memory>
#include <string>

#include "../core/Error.hpp"
#include "../core/Json.hpp"
#include "../core/Result.hpp"
#include "ModelRequest.hpp"
#include "ModelResponse.hpp"

namespace trinity::intelligence {

struct ModelProviderInfo {
    std::string providerId;
    std::string displayName;
    bool available = false;
    std::string endpoint;  // configuration surface only — no network code ships
    std::string modelName;
    bool configured = false;
};

class IModelProvider {
public:
    virtual ~IModelProvider() = default;

    virtual ModelProviderInfo info() const = 0;

    // Canonical LLM seam (task §10): produce a structured response.
    // The application works fully without an LLM; NullModelProvider
    // returns success=false + capability_unavailable.
    virtual ModelResponse generate(const ModelRequest& request) = 0;

    // Produce a structured tool-call plan from a request. Deterministic
    // stand-ins emit fixed plans; a future model infers them.
    virtual core::Result<ModelResponse> generatePlan(const ModelRequest& request) = 0;

    // Streaming hook for the future model; stand-ins emit one chunk.
    using StreamCallback = std::function<void(const std::string& chunk, bool final)>;
    virtual void streamPlan(const ModelRequest& request, StreamCallback callback) = 0;

    virtual core::Status configure(const core::Json& config) = 0;
};

class NullModelProvider : public IModelProvider {
public:
    ModelProviderInfo info() const override;
    ModelResponse generate(const ModelRequest& request) override;
    core::Result<ModelResponse> generatePlan(const ModelRequest& request) override;
    void streamPlan(const ModelRequest& request, StreamCallback callback) override;
    core::Status configure(const core::Json& config) override;
};

}  // namespace trinity::intelligence
