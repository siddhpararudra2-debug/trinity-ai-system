#pragma once

// Intent execution pipeline (§8):
//   User Request -> RequirementParser -> IntentValidator ->
//   IntentRouter -> JobManager -> EngineRegistry -> Engine ->
//   Validation -> Result
// Validation is never bypassed. Math requests (e.g. "Calculate 25 * 8")
// reach MathEngine; valid CAD requests route to CadEngine but return
// truthful CAPABILITY_UNAVAILABLE instead of fake geometry when the
// operation is not implemented.

#include <memory>
#include <string>

#include "../core/Json.hpp"
#include "../jobs/Job.hpp"
#include "Intent.hpp"
#include "IntentRouter.hpp"
#include "IntentValidator.hpp"
#include "RequirementParser.hpp"

namespace trinity::engines {
class EngineRegistry;
}

namespace trinity::jobs {
class JobManager;
class JobWorker;
}  // namespace trinity::jobs

namespace trinity::intelligence {

struct PipelineResult {
    bool success = false;
    Intent intent;
    validation::ValidationResult validation;
    RouteResult routing;
    core::Json engineEnvelope = core::Json::object();
    std::string jobId;
    core::Json error = nullptr;

    core::Json toJson() const;
};

class RequestPipeline {
public:
    RequestPipeline(std::shared_ptr<jobs::JobManager> jobs,
                    std::shared_ptr<engines::EngineRegistry> registry);
    void setWorker(std::shared_ptr<jobs::JobWorker> worker);

    // Blocking: parse -> validate -> route -> create+execute job.
    PipelineResult executeSync(const std::string& requestText);
    // Non-blocking for the Qt UI: parse -> validate -> route -> create job
    // + enqueue on the worker. Returns jobId immediately; poll
    // JobManager::get(jobId) for lifecycle. Throws/returns error result
    // before any job is created when parse/validate/route fail.
    PipelineResult submit(const std::string& requestText);

private:
    PipelineResult failResult(const Intent& intent,
                              const validation::ValidationResult& validation,
                              const RouteResult& routing, const core::Json& error);

    std::shared_ptr<jobs::JobManager> jobs_;
    std::shared_ptr<engines::EngineRegistry> registry_;
    std::shared_ptr<jobs::JobWorker> worker_;
};

}  // namespace trinity::intelligence
