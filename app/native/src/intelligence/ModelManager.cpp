#include "trinity/intelligence/ModelManager.hpp"
#include "trinity/intelligence/ModelProviderFactory.hpp"

namespace trinity::intelligence {

ModelManager& ModelManager::instance() {
    static ModelManager inst;
    return inst;
}

ModelManager::ModelManager() {
    // Start with NullModelProvider by default
    _activeProvider = ModelProviderFactory::instance().createOrNull("null", core::Json::object());
}

void ModelManager::setActiveProvider(const std::string& providerId, const core::Json& config) {
    _activeProvider = ModelProviderFactory::instance().createOrNull(providerId, config);
    if (_activeProvider) {
        _activeProvider->configure(config);
    }
}

std::shared_ptr<IModelProvider> ModelManager::getActiveProvider() const {
    return _activeProvider;
}

ModelResponse ModelManager::generate(const ModelRequest& request) const {
    if (_activeProvider) {
        return _activeProvider->generate(request);
    }
    ModelResponse response;
    response.success = false;
    response.requestId = request.requestId;
    response.error = core::Json{{"error", "No active model provider configured"}};
    return response;
}

} // namespace trinity::intelligence
