#include "trinity/intelligence/ModelProviderFactory.hpp"

#include <algorithm>

#include "trinity/core/Logger.hpp"

namespace trinity::intelligence {

ModelProviderFactory& ModelProviderFactory::instance() {
    static ModelProviderFactory factory;
    return factory;
}

ModelProviderFactory::ModelProviderFactory() {
    // Direct emplace (not via registerProvider/ensure*) — the instance
    // is still under construction here, so re-entering instance() would
    // recurse.
    creators_.emplace("null", [](const core::Json&) {
        return std::make_shared<NullModelProvider>();
    });
}

void ensureNullProviderRegistered() {
    auto& factory = ModelProviderFactory::instance();
    if (!factory.has("null")) {
        factory.registerProvider("null", [](const core::Json&) {
            return std::make_shared<NullModelProvider>();
        });
    }
}

void ModelProviderFactory::registerProvider(const std::string& id, Creator creator) {
    if (id.empty()) {
        throw core::RequestValidationError("Provider id must not be empty", {}, "model");
    }
    if (!creator) {
        throw core::RequestValidationError(
            "Provider creator must not be empty", {{"provider", id}}, "model");
    }
    if (creators_.find(id) != creators_.end()) {
        throw core::RequestValidationError(
            "Model provider '" + id + "' is already registered", {{"provider", id}},
            "model");
    }
    creators_[id] = std::move(creator);
    core::Logger::instance().info("model", "provider registered",
                                  core::Json{{"provider", id}});
}

bool ModelProviderFactory::has(const std::string& id) const {
    return creators_.find(id) != creators_.end();
}

std::vector<std::string> ModelProviderFactory::list() const {
    std::vector<std::string> ids;
    ids.reserve(creators_.size());
    for (const auto& [id, _] : creators_) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::shared_ptr<IModelProvider> ModelProviderFactory::create(
    const std::string& id, const core::Json& config) const {
    const auto it = creators_.find(id);
    if (it == creators_.end()) {
        throw core::RequestValidationError(
            "Unknown model provider '" + id + "'",
            {{"provider", id}, {"available", list()}}, "model");
    }
    auto provider = it->second(config);
    if (!provider) {
        throw core::EngineExecutionError(
            "Provider factory for '" + id + "' returned null",
            {{"provider", id}}, "model");
    }
    return provider;
}

std::shared_ptr<IModelProvider> ModelProviderFactory::createOrNull(
    const std::string& id, const core::Json& config) const {
    if (id.empty() || !has(id)) {
        ensureNullProviderRegistered();
        return create("null", core::Json::object());
    }
    return create(id, config);
}

}  // namespace trinity::intelligence
