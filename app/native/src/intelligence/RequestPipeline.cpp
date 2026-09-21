#include "trinity/intelligence/RequestPipeline.hpp"

#include "trinity/core/Logger.hpp"
#include "trinity/core/Time.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/jobs/JobWorker.hpp"

namespace trinity::intelligence {

core::Json PipelineResult::toJson() const {
    return core::Json{{"success", success},
                      {"intent", intent.toJson()},
                      {"validation", validation.toJson()},
                      {"routing", routing.toJson()},
                      {"engine_envelope", engineEnvelope},
                      {"job_id", jobId},
                      {"error", error}};
}

RequestPipeline::RequestPipeline(std::shared_ptr<jobs::JobManager> jobs,
                                 std::shared_ptr<engines::EngineRegistry> registry)
    : jobs_(std::move(jobs)), registry_(std::move(registry)) {
}

void RequestPipeline::setWorker(std::shared_ptr<jobs::JobWorker> worker) {
    worker_ = std::move(worker);
}

PipelineResult RequestPipeline::failResult(const Intent& intent,
                                           const validation::ValidationResult& validation,
                                           const RouteResult& routing,
                                           const core::Json& error) {
    PipelineResult out;
    out.success = false;
    out.intent = intent;
    out.validation = validation;
    out.routing = routing;
    out.error = error;
    return out;
}

PipelineResult RequestPipeline::executeSync(const std::string& requestText) {
    auto& log = core::Logger::instance();
    RequirementParser parser;
    const ParseResult parsed = parser.parse(requestText);

    IntentValidator validator;
    validation::ValidationResult validation = validator.validate(parsed.intent, *registry_);

    if (parsed.status != ParseStatus::Valid || !validation.passed()) {
        core::Json err = core::makeError(core::ErrorCode::RequestValidationError,
                                         "Request failed intent validation", "pipeline",
                                         {{"validation", validation.toJson()}})
                             .toJson();
        // Include parser errors for debuggability.
        core::Json parserErrors = core::Json::array();
        for (const auto& e : parsed.errors) {
            if (e.rfind("__no_", 0) == 0) {
                continue;
            }
            parserErrors.push_back(e);
        }
        err["details"]["parser_errors"] = parserErrors;
        log.info("pipeline", "request rejected at validation",
                 core::Json{{"request", requestText}});
        RouteResult emptyRoute;
        return failResult(parsed.intent, validation, emptyRoute, err);
    }

    IntentRouter router;
    const RouteResult route = router.route(parsed.intent, *registry_);
    if (!route.routed) {
        core::Json err = core::makeError(
            route.status == "CAPABILITY_UNAVAILABLE" ? core::ErrorCode::CapabilityUnavailableError
                                                     : core::ErrorCode::RequestValidationError,
            route.reason.empty() ? "Request could not be routed" : route.reason, "pipeline",
            route.toJson())
                             .toJson();
        log.info("pipeline", "request rejected at routing",
                 core::Json{{"request", requestText}, {"reason", route.reason}});
        return failResult(parsed.intent, validation, route, err);
    }

    // Validated + routed: create and execute the job through the registry.
    jobs::JobCreateOptions opts;
    opts.requestId = core::newUuid();
    opts.metadata = core::Json{{"raw_request", requestText},
                               {"intent_id", parsed.intent.intentId},
                               {"route", route.toJson()}};
    jobs::Job job = jobs_->createJob(route.toolCall.engine, route.toolCall.operation,
                                     route.toolCall.parameters, opts);
    core::Json envelope = jobs_->executeJob(job.jobId);
    PipelineResult out;
    out.intent = parsed.intent;
    out.validation = validation;
    out.routing = route;
    out.engineEnvelope = envelope;
    out.jobId = job.jobId;
    out.success = envelope.value("success", false);
    if (!out.success) {
        // Truthful failure (e.g. CAD CAPABILITY_UNAVAILABLE): surface the
        // engine errors, never invent results.
        core::Json errs = envelope.value("errors", core::Json::array());
        out.error = errs.is_array() && !errs.empty() ? errs.front() : envelope;
    } else {
        out.error = nullptr;
    }
    log.info("pipeline", "request executed",
             core::Json{{"job_id", job.jobId}, {"success", out.success}});
    return out;
}

PipelineResult RequestPipeline::submit(const std::string& requestText) {
    auto& log = core::Logger::instance();
    RequirementParser parser;
    const ParseResult parsed = parser.parse(requestText);

    IntentValidator validator;
    validation::ValidationResult validation = validator.validate(parsed.intent, *registry_);

    if (parsed.status != ParseStatus::Valid || !validation.passed()) {
        core::Json err = core::makeError(core::ErrorCode::RequestValidationError,
                                         "Request failed intent validation", "pipeline")
                             .toJson();
        RouteResult emptyRoute;
        return failResult(parsed.intent, validation, emptyRoute, err);
    }

    IntentRouter router;
    const RouteResult route = router.route(parsed.intent, *registry_);
    if (!route.routed) {
        core::Json err = core::makeError(
            route.status == "CAPABILITY_UNAVAILABLE" ? core::ErrorCode::CapabilityUnavailableError
                                                     : core::ErrorCode::RequestValidationError,
            route.reason.empty() ? "Request could not be routed" : route.reason, "pipeline")
                             .toJson();
        return failResult(parsed.intent, validation, route, err);
    }

    jobs::JobCreateOptions opts;
    opts.requestId = core::newUuid();
    opts.metadata = core::Json{{"raw_request", requestText},
                               {"intent_id", parsed.intent.intentId},
                               {"route", route.toJson()}};
    std::string jobId;
    if (worker_ && worker_->running()) {
        jobId = worker_->submit(route.toolCall.engine, route.toolCall.operation,
                                route.toolCall.parameters, opts);
    } else {
        jobs::Job job = jobs_->createJob(route.toolCall.engine, route.toolCall.operation,
                                         route.toolCall.parameters, opts);
        // Enqueue path unavailable: leave QUEUED for the worker/polling loop.
        // Fall back to inline execution only when explicitly headless? For UI
        // responsiveness keep QUEUED here; executeSync() is the blocking API.
        // To avoid stranding, execute inline when no worker exists.
        if (!worker_) {
            core::Json envelope = jobs_->executeJob(job.jobId);
            PipelineResult out;
            out.intent = parsed.intent;
            out.validation = validation;
            out.routing = route;
            out.engineEnvelope = envelope;
            out.jobId = job.jobId;
            out.success = envelope.value("success", false);
            out.error = out.success ? core::Json(nullptr) : envelope;
            return out;
        }
        jobId = job.jobId;
        worker_->submitExisting(jobId);
    }

    PipelineResult out;
    out.success = true;  // accepted (not yet completed — poll jobId)
    out.intent = parsed.intent;
    out.validation = validation;
    out.routing = route;
    out.jobId = jobId;
    out.engineEnvelope = core::Json{{"accepted", true}, {"job_id", jobId}};
    out.error = nullptr;
    log.info("pipeline", "request submitted",
             core::Json{{"job_id", jobId}, {"request", requestText}});
    return out;
}

}  // namespace trinity::intelligence
