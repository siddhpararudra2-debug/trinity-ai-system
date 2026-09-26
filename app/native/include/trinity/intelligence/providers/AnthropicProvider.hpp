#pragma once

#include "trinity/intelligence/IModelProvider.hpp"
#include <string>

namespace trinity::intelligence::providers {

class AnthropicProvider : public IModelProvider {
public:
    AnthropicProvider();

    ModelProviderInfo info() const override;
    ModelResponse generate(const ModelRequest& request) override;
    core::Result<ModelResponse> generatePlan(const ModelRequest& request) override;
    void streamPlan(const ModelRequest& request, StreamCallback callback) override;
    core::Status configure(const core::Json& config) override;

private:
    std::string _apiKey;
    std::string _modelName = "claude-3-5-sonnet-20240620";
    std::string _endpoint = "https://api.anthropic.com/v1/messages";
    bool _enabled = true;
};

} // namespace trinity::intelligence::providers
