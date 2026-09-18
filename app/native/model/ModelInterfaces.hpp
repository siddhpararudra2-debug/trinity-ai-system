// Trinity — model layer interfaces (brief §Critical Rule).
//
//   IModelProvider         — the future proprietary LLM boundary
//   ICADGenerator          — the future proprietary CAD-generation model
//   IPCBGenerator          — the future proprietary PCB-generation model
//   IEngineeringReasoner   — the future engineering reasoning model
//
// NOTHING in this file implements intelligence. The shipped implementations
// are deterministic mocks/null objects so the entire application works
// without the proprietary model. The future model is a drop-in replacement
// behind the same interfaces — no application code changes required.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../core/Error.hpp"
#include "../core/Json.hpp"

namespace trinity::model {

// ---------------------------------------------------------- IModelProvider

struct ModelProviderInfo {
    std::string provider_id;
    std::string display_name;
    bool available = false;
    std::string endpoint;      // configuration surface only — no network code ships
    std::string model_name;
    bool configured = false;
};

class IModelProvider {
public:
    virtual ~IModelProvider() = default;

    virtual ModelProviderInfo info() const = 0;

    // Produces a structured tool-call plan (JSON) from a natural-language or
    // structured request. Deterministic mocks emit fixed plans; the future
    // model will produce genuinely inferred plans.
    virtual core::Json generate_plan(const core::Json& request) = 0;

    // Streaming hook reserved for the future model; mocks emit one chunk.
    using StreamCallback = std::function<void(const std::string& chunk, bool final)>;
    virtual void stream_plan(const core::Json& request, StreamCallback callback) = 0;

    virtual core::Status configure(const core::Json& config) = 0;
};

// NullModelProvider: always reports unavailable and refuses to plan.
// This is the DEFAULT at ship time — truthful about the missing model.
class NullModelProvider : public IModelProvider {
public:
    ModelProviderInfo info() const override;
    core::Json generate_plan(const core::Json& request) override;
    void stream_plan(const core::Json& request, StreamCallback callback) override;
    core::Status configure(const core::Json& config) override;
};

// MockModelProvider: deterministic plan generation from a template table.
// Useful for tests, demos and UI development. It NEVER calls any network.
class MockModelProvider : public IModelProvider {
public:
    ModelProviderInfo info() const override;
    core::Json generate_plan(const core::Json& request) override;
    void stream_plan(const core::Json& request, StreamCallback callback) override;
    core::Status configure(const core::Json& config) override;
};

// ---------------------------------------------------------- ICADGenerator

struct CADGenerationRequest {
    std::string part_type;
    core::Json parameters;
    std::string prompt;  // free-form description; generators may ignore it
};

// The future proprietary CAD-generation model implements this. The mock
// always refuses: CAD generation deterministically belongs to the engines.
class ICADGenerator {
public:
    virtual ~ICADGenerator() = default;
    virtual std::string generator_id() const = 0;
    virtual bool available() const = 0;
    virtual core::Result<core::Json> generate_ir(const CADGenerationRequest& request) = 0;
};

class NullCADGenerator : public ICADGenerator {
public:
    std::string generator_id() const override { return "proprietary-cad-model"; }
    bool available() const override { return false; }
    core::Result<core::Json> generate_ir(const CADGenerationRequest& request) override;
};

// ---------------------------------------------------------- IPCBGenerator

struct PCBGenerationRequest {
    core::Json netlist_hint;
    core::Json constraints;
    std::string prompt;
};

class IPCBGenerator {
public:
    virtual ~IPCBGenerator() = default;
    virtual std::string generator_id() const = 0;
    virtual bool available() const = 0;
    virtual core::Result<core::Json> generate_layout(const PCBGenerationRequest& request) = 0;
};

class NullPCBGenerator : public IPCBGenerator {
public:
    std::string generator_id() const override { return "proprietary-pcb-model"; }
    bool available() const override { return false; }
    core::Result<core::Json> generate_layout(const PCBGenerationRequest& request) override;
};

// ---------------------------------------------------- IEngineeringReasoner

class IEngineeringReasoner {
public:
    virtual ~IEngineeringReasoner() = default;
    virtual std::string reasoner_id() const = 0;
    virtual bool available() const = 0;
    // Explains/annotates a plan or result. Mocks return a fixed deterministic
    // annotation; the future model will provide genuine reasoning.
    virtual core::Result<core::Json> explain(const core::Json& subject) = 0;
};

class NullEngineeringReasoner : public IEngineeringReasoner {
public:
    std::string reasoner_id() const override { return "proprietary-reasoning-model"; }
    bool available() const override { return false; }
    core::Result<core::Json> explain(const core::Json& subject) override;
};

// ------------------------------------------------------------- model hub

// Central access point wired at startup. Holds one implementation per
// interface; all ship-time defaults are null/mock objects.
class ModelHub {
public:
    static ModelHub& instance();

    void set_provider(std::shared_ptr<IModelProvider> provider);
    void set_cad_generator(std::shared_ptr<ICADGenerator> generator);
    void set_pcb_generator(std::shared_ptr<IPCBGenerator> generator);
    void set_reasoner(std::shared_ptr<IEngineeringReasoner> reasoner);

    IModelProvider& provider();
    ICADGenerator& cad_generator();
    IPCBGenerator& pcb_generator();
    IEngineeringReasoner& reasoner();

private:
    ModelHub();
    std::shared_ptr<IModelProvider> provider_;
    std::shared_ptr<ICADGenerator> cad_generator_;
    std::shared_ptr<IPCBGenerator> pcb_generator_;
    std::shared_ptr<IEngineeringReasoner> reasoner_;
};

}  // namespace trinity::model
