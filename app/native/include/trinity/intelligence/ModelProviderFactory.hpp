#pragma once

// Model provider factory: how another developer plugs an LLM in.
//
//   1. Subclass IModelProvider (see examples/ExampleProvider for a
//      commented skeleton to copy).
//   2. Call registerProvider("my-id", [](const core::Json& config) {
//          return std::make_shared<MyProvider>(...); });
//   3. Set TRINITY_MODEL_PROVIDER=my-id (plus endpoint/model/key env).
//
// Unknown ids fall back to the "null" provider so the application
// always boots. Mirrors the EngineRegistry pattern: list() for
// discovery, structured errors naming the available set.

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../core/Json.hpp"
#include "IModelProvider.hpp"

namespace trinity::intelligence {

class ModelProviderFactory {
public:
    using Creator = std::function<std::shared_ptr<IModelProvider>(const core::Json&)>;

    static ModelProviderFactory& instance();

    ModelProviderFactory(const ModelProviderFactory&) = delete;
    ModelProviderFactory& operator=(const ModelProviderFactory&) = delete;

    /// Register a provider id. Throws RequestValidationError on empty id
    /// or duplicate registration.
    void registerProvider(const std::string& id, Creator creator);
    bool has(const std::string& id) const;
    std::vector<std::string> list() const;

    /// Create a provider. Unknown ids throw EngineNotFoundError-style
    /// CapabilityUnavailableError? No — RequestValidationError naming
    /// the requested id and the available set.
    std::shared_ptr<IModelProvider> create(const std::string& id,
                                           const core::Json& config) const;

    /// Create with fallback: unknown ids return NullModelProvider instead
    /// of throwing, so startup never fails for a typo.
    std::shared_ptr<IModelProvider> createOrNull(const std::string& id,
                                                 const core::Json& config) const;

private:
    ModelProviderFactory();
    std::map<std::string, Creator> creators_;
};

/// Registers the built-in "null" provider. Called automatically on first
/// factory use; exposed for tests that need a fresh state.
void ensureNullProviderRegistered();

}  // namespace trinity::intelligence
