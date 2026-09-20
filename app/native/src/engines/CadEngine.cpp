#include "trinity/engines/CadEngine.hpp"

#include "trinity/core/Logger.hpp"

namespace trinity::engines {

CadEngine::CadEngine() {
    name_ = "cad";
    version_ = "0.1.0";
    // Skeleton capabilities: routing + contracts only. Real geometry ops
    // (e.g. quadcopter frame generation) arrive in a later phase.
    capabilities_ = {"describe"};
}

EngineResult CadEngine::execute(const EngineRequest& request) {
    if (request.operation == "describe") {
        EngineResult out = successResult(
            request, {{"engine", name_},
                      {"version", version_},
                      {"capabilities", capabilities_},
                      {"note", "CAD geometry not implemented yet"}});
        out.validation = validate(out);
        return out;
    }
    if (!hasCapability(request.operation)) {
        // Skeleton phase: route known-shape but unimplemented ops to a
        // structured refusal rather than fake geometry or an exception.
        EngineResult out = capabilityUnavailable(
            request, "CAD operation '" + request.operation + "' is not implemented yet");
        core::Logger::instance().warning(
            "engines", "cad unsupported operation",
            core::Json{{"operation", request.operation}});
        return out;
    }
    EngineResult out = capabilityUnavailable(
        request, "CAD operation '" + request.operation + "' is not implemented yet");
    return out;
}

validation::ValidationResult CadEngine::validate(const EngineResult& result) const {
    validation::ValidationResult validation;
    validation.operation = result.operation;
    validation.jobId = result.jobId;
    validation.checks = {{"engine", "cad"}, {"operation", result.operation}};
    if (!result.success) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "CAD operation unavailable or failed";
        validation::ValidationMessage msg;
        msg.rule = "cad.available";
        msg.severity = validation::Severity::Warning;
        msg.passed = false;
        msg.message = "No CAD geometry produced in skeleton phase";
        validation.addMessage(std::move(msg));
        if (!result.errors.empty()) {
            validation.error = result.errors.front();
        }
        return validation;
    }
    validation.status = validation::ValidationStatus::Generated;
    validation.message = "CAD describe completed; geometry pending later phase";
    validation::ValidationMessage msg;
    msg.rule = "cad.describe";
    msg.severity = validation::Severity::Info;
    msg.passed = true;
    msg.message = "Capability metadata returned, no geometry claimed";
    validation.addMessage(std::move(msg));
    return validation;
}

}  // namespace trinity::engines
