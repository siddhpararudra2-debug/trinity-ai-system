#pragma once

#include <memory>
#include <string>
#include "../core/Json.hpp"
#include "IModelProvider.hpp"

namespace trinity::intelligence {

class ModelManager {
public:
    static ModelManager& instance();
    
    void setActiveProvider(const std::string& providerId, const core::Json& config);
    std::shared_ptr<IModelProvider> getActiveProvider() const;
    
    // Convenience forward to the active provider
    ModelResponse generate(const ModelRequest& request) const;

private:
    ModelManager();
    std::shared_ptr<IModelProvider> _activeProvider;
};

} // namespace trinity::intelligence
