// Trinity — honest scaffold engines for domains not yet implemented.
// Port of backend/app/engines/scaffold.py: executing any capability raises
// capability_unavailable. They still register and describe themselves so the
// registry, UI and future model layer can discover planned capabilities.
#pragma once

#include <string>
#include <vector>

#include "../Engine.hpp"

namespace trinity::engines {

class ScaffoldEngine : public IEngine {
public:
    ScaffoldEngine(std::string id, std::string name, std::vector<std::string> capabilities)
        : id_(std::move(id)), name_(std::move(name)), capabilities_(std::move(capabilities)) {}

    EngineDescriptor describe() const override {
        EngineDescriptor descriptor;
        descriptor.id = id_;
        descriptor.name = name_;
        descriptor.version = "0.1";
        descriptor.capabilities = capabilities_;
        descriptor.health = EngineHealth::Scaffolded;
        descriptor.health_detail = "scaffolded: architecture registered, execution not implemented";
        return descriptor;
    }

    ExecutionOutput execute(const std::string& capability, const core::Json& parameters) override {
        (void)parameters;
        core::Json details = core::Json::object();
        details["engine"] = id_;
        details["operation"] = capability;
        details["status"] = "scaffolded";
        throw core::TrinityException(core::Error(
            core::ErrorCode::CapabilityUnavailableError,
            "The " + id_ + " engine is scaffolded and cannot execute '" + capability + "' yet",
            details));
    }

    core::Json validate(const core::Json& payload) override {
        (void)payload;
        core::Json out = core::Json::object();
        out["status"] = "scaffolded";
        return out;
    }

    EngineHealth health() const override { return EngineHealth::Scaffolded; }

private:
    std::string id_;
    std::string name_;
    std::vector<std::string> capabilities_;
};

void register_scaffold_engines(std::vector<std::string>& registered);

}  // namespace trinity::engines
