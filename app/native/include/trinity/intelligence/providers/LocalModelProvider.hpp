#pragma once

#include "trinity/intelligence/IModelProvider.hpp"
#include <string>

namespace trinity::intelligence::providers {

class LocalModelProvider : public IModelProvider {
public:
    LocalModelProvider();

    ModelProviderInfo info() const override;
    ModelResponse generate(const ModelRequest& request) override;
    core::Result<ModelResponse> generatePlan(const ModelRequest& request) override;
    void streamPlan(const ModelRequest& request, StreamCallback callback) override;
    core::Status configure(const core::Json& config) override;

private:
    std::string _modelName = "llama-3-8b";
    std::string _endpoint = "http://localhost:11434/api/chat";
    bool _enabled = true;
};

} // namespace trinity::intelligence::providers
