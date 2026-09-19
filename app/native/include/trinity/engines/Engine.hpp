#pragma once

// Engine contract. Mirrors src/engines/base.py: every engine — math,
// cad, pcb, firmware, vision, research — implements IEngine so the job
// manager treats them interchangeably and a future LLM tool-caller can
// discover and invoke them uniformly.
//
// EngineResult.pendingArtifacts holds (temp_path, type) pairs for files
// an engine wrote to scratch; the JobManager promotes them through the
// artifact manager so storage/artifacts/ keeps a single writer.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../core/Error.hpp"
#include "../core/Json.hpp"
#include "../validation/ValidationResult.hpp"

namespace trinity::engines {

struct ArtifactRef {
    std::string artifactId;
    std::string type;
    std::string path;
    long long sizeBytes = 0;
    std::string checksum;

    core::Json toJson() const;
};

struct EngineRequest {
    std::string engine;
    std::string operation;
    core::Json parameters = core::Json::object();
};

struct EngineResult {
    bool success = false;
    std::string engine;
    std::string operation;
    core::Json result = core::Json::object();
    std::vector<ArtifactRef> artifacts;
    std::vector<std::pair<std::string, std::string>> pendingArtifacts;
    std::optional<validation::ValidationResult> validation;
    std::vector<core::Json> errors;

    core::Json toJson() const;
};

struct EngineCapability {
    std::string name;
    std::string version;
    std::vector<std::string> capabilities;

    core::Json toJson() const;
};

class IEngine {
public:
    virtual ~IEngine() = default;

    virtual const std::string& name() const noexcept = 0;
    virtual const std::string& version() const noexcept = 0;
    virtual const std::vector<std::string>& capabilities() const noexcept = 0;

    virtual EngineResult execute(const std::string& operation,
                                 const core::Json& parameters) = 0;
    virtual EngineCapability describe() const = 0;
};

class EngineBase : public IEngine {
public:
    EngineCapability describe() const override;

protected:
    std::string name_ = "base";
    std::string version_ = "0.0";
    std::vector<std::string> capabilities_;

public:
    const std::string& name() const noexcept override { return name_; }
    const std::string& version() const noexcept override { return version_; }
    const std::vector<std::string>& capabilities() const noexcept override {
        return capabilities_;
    }
};

}  // namespace trinity::engines
