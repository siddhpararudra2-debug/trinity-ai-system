#include "ModelInterfaces.hpp"

namespace trinity::model {
namespace {

core::Error model_unavailable(const std::string& what) {
    core::Json details = core::Json::object();
    details["reason"] = "proprietary model not connected (by design at this stage)";
    details["component"] = what;
    return core::Error(core::ErrorCode::CapabilityUnavailableError,
                       what + " requires the proprietary model, which is not connected",
                       details);
}

}  // namespace

// ------------------------------------------------------- NullModelProvider

ModelProviderInfo NullModelProvider::info() const {
    ModelProviderInfo info;
    info.provider_id = "null";
    info.display_name = "No model connected";
    info.available = false;
    info.configured = false;
    return info;
}

core::Json NullModelProvider::generate_plan(const core::Json& request) {
    (void)request;
    throw core::TrinityException(model_unavailable("IModelProvider::generate_plan"));
}

void NullModelProvider::stream_plan(const core::Json& request, StreamCallback callback) {
    (void)request;
    if (callback) {
        callback("model provider unavailable: the proprietary model is not connected",
                 true);
    }
}

core::Status NullModelProvider::configure(const core::Json& config) {
    // Configuration surface exists (brief §Settings: Model Provider section
    // defines configuration interfaces only). Accepting and storing the shape
    // is honest; nothing is dialled anywhere.
    (void)config;
    return core::Status::ok();
}

// -------------------------------------------------------- MockModelProvider

ModelProviderInfo MockModelProvider::info() const {
    ModelProviderInfo info;
    info.provider_id = "mock";
    info.display_name = "Mock model (deterministic, offline)";
    info.available = true;
    info.configured = true;
    return info;
}

core::Json MockModelProvider::generate_plan(const core::Json& request) {
    // Deterministic template: echo the request into a fixed plan shape.
    core::Json plan = core::Json::object();
    plan["plan_version"] = 1;
    plan["source"] = "mock";
    plan["steps"] = core::Json::array();
    core::Json step = core::Json::object();
    step["action"] = request.contains("intent") ? request.find("intent")->as_string()
                                                : std::string("unknown");
    step["parameters"] = request.contains("parameters")
                             ? *request.find("parameters")
                             : core::Json::object();
    plan["steps"].push_back(step);
    return plan;
}

void MockModelProvider::stream_plan(const core::Json& request, StreamCallback callback) {
    if (callback) {
        callback(generate_plan(request).dump(), true);
    }
}

core::Status MockModelProvider::configure(const core::Json& config) {
    (void)config;
    return core::Status::ok();
}

// ---------------------------------------------------------- null generators

core::Result<core::Json> NullCADGenerator::generate_ir(const CADGenerationRequest& request) {
    (void)request;
    return core::Result<core::Json>::fail(model_unavailable("ICADGenerator::generate_ir"));
}

core::Result<core::Json> NullPCBGenerator::generate_layout(const PCBGenerationRequest& request) {
    (void)request;
    return core::Result<core::Json>::fail(model_unavailable("IPCBGenerator::generate_layout"));
}

core::Result<core::Json> NullEngineeringReasoner::explain(const core::Json& subject) {
    (void)subject;
    return core::Result<core::Json>::fail(model_unavailable("IEngineeringReasoner::explain"));
}

// ------------------------------------------------------------------ ModelHub

ModelHub::ModelHub()
    : provider_(std::make_shared<NullModelProvider>()),
      cad_generator_(std::make_shared<NullCADGenerator>()),
      pcb_generator_(std::make_shared<NullPCBGenerator>()),
      reasoner_(std::make_shared<NullEngineeringReasoner>()) {}

ModelHub& ModelHub::instance() {
    static ModelHub hub;
    return hub;
}

void ModelHub::set_provider(std::shared_ptr<IModelProvider> provider) {
    if (provider) provider_ = std::move(provider);
}

void ModelHub::set_cad_generator(std::shared_ptr<ICADGenerator> generator) {
    if (generator) cad_generator_ = std::move(generator);
}

void ModelHub::set_pcb_generator(std::shared_ptr<IPCBGenerator> generator) {
    if (generator) pcb_generator_ = std::move(generator);
}

void ModelHub::set_reasoner(std::shared_ptr<IEngineeringReasoner> reasoner) {
    if (reasoner) reasoner_ = std::move(reasoner);
}

IModelProvider& ModelHub::provider() { return *provider_; }
ICADGenerator& ModelHub::cad_generator() { return *cad_generator_; }
IPCBGenerator& ModelHub::pcb_generator() { return *pcb_generator_; }
IEngineeringReasoner& ModelHub::reasoner() { return *reasoner_; }

}  // namespace trinity::model
