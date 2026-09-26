#pragma once

#include "trinity/intelligence/IModelProvider.hpp"
#include <string>

namespace trinity::intelligence::providers {

class OpenAIProvider : public IModelProvider {
public:
    OpenAIProvider();

    ModelProviderInfo info() const override;
    ModelResponse generate(const ModelRequest& request) override;
    core::Result<ModelResponse> generatePlan(const ModelRequest& request) override;
    void streamPlan(const ModelRequest& request, StreamCallback callback) override;
    core::Status configure(const core::Json& config) override;

private:
    std::string _apiKey;
    std::string _modelName = "gpt-4o";
    std::string _endpoint = "https://api.openai.com/v1/chat/completions";
    bool _enabled = true;
};

} // namespace trinity::intelligence::providers
