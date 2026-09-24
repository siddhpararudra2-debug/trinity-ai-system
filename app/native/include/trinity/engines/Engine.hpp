#pragma once

// Engine contract. Mirrors src/engines/base.py: every engine — math,
// cad, pcb, firmware, vision, research — implements IEngine so the job
// manager treats them interchangeably and a future LLM tool-caller can
// discover and invoke them uniformly.
//
// EngineResult.pendingArtifacts holds (temp_path, type) pairs for files
// an engine wrote to scratch; the JobManager promotes them through the
// artifact manager so storage/artifacts/ keeps a single writer.
//
// EngineRequest::cancelCheck is an optional cooperative probe (not
// serialized). Long-running engines (simulation) poll it between steps
// and return a non-success result when it reports cancellation.

#include <functional>
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
    static ArtifactRef fromJson(const core::Json& json);
};

struct EngineRequest {
    std::string requestId;  // UUID, assigned by caller or JobManager
    std::string engine;
    std::string operation;
    core::Json parameters = core::Json::object();
    // Cooperative cancellation probe; empty when not applicable.
    // Never serialized — in-process only.
    std::function<bool()> cancelCheck;

    core::Json toJson() const;
    static EngineRequest fromJson(const core::Json& json);
};

struct EngineResult {
    bool success = false;
    std::string engine;
    std::string operation;
    std::string jobId;      // filled by JobManager
    std::string requestId;  // echoes EngineRequest::requestId when known
    core::Json result = core::Json::object();
    std::vector<ArtifactRef> artifacts;
    std::vector<std::pair<std::string, std::string>> pendingArtifacts;
    std::optional<validation::ValidationResult> validation;
    std::vector<core::Json> errors;  // each entry is an ErrorInfo JSON envelope
    core::Json metadata = core::Json::object();

    bool failed() const noexcept { return !success; }
    void addError(const core::ErrorInfo& error) { errors.push_back(error.toJson()); }

    core::Json toJson() const;
    static EngineResult fromJson(const core::Json& json);
};

struct EngineCapability {
    std::string name;
    std::string version;
    std::vector<std::string> capabilities;

    core::Json toJson() const;
    static EngineCapability fromJson(const core::Json& json);
};

/// Main engine abstraction (§1). Independent from specific engines.
/// New canonical API: name()/capabilities()/execute(request)/validate().
/// Legacy helpers (version/describe/execute(op,params)) remain as
/// non-pure compat wrappers so existing callers keep compiling while
/// new engines override only the canonical methods.
class IEngine {
public:
    virtual ~IEngine() = default;

    virtual std::string name() const = 0;
    virtual std::vector<std::string> capabilities() const = 0;
    virtual EngineResult execute(const EngineRequest& request) = 0;
    virtual validation::ValidationResult validate(const EngineResult& result) const = 0;

    // ---- compat (defaulted, override only if needed) ----
    virtual std::string version() const { return "0.0"; }
    virtual EngineCapability describe() const;
    virtual EngineResult execute(const std::string& operation,
                                 const core::Json& parameters);
};

/// Reusable base with request validation, capability checking,
/// structured errors, timing, logging, metadata and validation
/// handling. Engines may inherit or compose these helpers.
class EngineBase : public IEngine {
public:
    using IEngine::execute;
    EngineCapability describe() const override;
    EngineResult execute(const std::string& operation,
                         const core::Json& parameters) override;
    validation::ValidationResult validate(const EngineResult& result) const override;

protected:
    std::string name_ = "base";
    std::string version_ = "0.0";
    std::vector<std::string> capabilities_;

    bool hasCapability(const std::string& operation) const noexcept;
    void requireCapability(const EngineRequest& request) const;
    void requireParams(const EngineRequest& request,
                       const std::vector<std::string>& keys) const;
    EngineResult capabilityUnavailable(const EngineRequest& request,
                                       const std::string& detail = "") const;
    EngineResult invalidRequest(const std::string& message,
                                const core::Json& details = core::Json::object()) const;
    EngineResult failureResult(const EngineRequest& request, const std::string& message,
                               const core::Json& details = core::Json::object()) const;
    EngineResult successResult(const EngineRequest& request,
                               const core::Json& data = core::Json::object()) const;

public:
    std::string name() const override { return name_; }
    std::vector<std::string> capabilities() const override { return capabilities_; }
    std::string version() const override { return version_; }
};

}  // namespace trinity::engines
