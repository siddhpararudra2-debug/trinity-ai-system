#include <doctest.h>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/core/Error.hpp"
#include "trinity/engines/Engine.hpp"
#include "trinity/intelligence/Intent.hpp"
#include "trinity/intelligence/ModelRequest.hpp"
#include "trinity/intelligence/ModelResponse.hpp"
#include "trinity/intelligence/ToolCall.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/validation/ValidationResult.hpp"
#include "trinity/workflows/Workflow.hpp"

TEST_CASE("error info round-trips with source and timestamp") {
    trinity::core::ErrorInfo info = trinity::core::makeError(
        trinity::core::ErrorCode::EngineNotFoundError, "missing", "engines",
        {{"engine", "math"}});
    CHECK_FALSE(info.timestamp.empty());
    CHECK(info.source == "engines");
    const trinity::core::Json json = info.toJson();
    CHECK(json["code"] == "engine_not_found");
    CHECK(json["source"] == "engines");
    CHECK(json.contains("timestamp"));
    const trinity::core::ErrorInfo back =
        trinity::core::ErrorInfo::fromJson(json);
    CHECK(back.message == "missing");
    CHECK(back.source == "engines");
    CHECK(back.code == trinity::core::ErrorCode::EngineNotFoundError);
}

TEST_CASE("engine request/result round-trip") {
    trinity::engines::EngineRequest req;
    req.requestId = "req-1";
    req.engine = "math";
    req.operation = "solve";
    req.parameters = {{"x", 1}};
    const auto reqBack = trinity::engines::EngineRequest::fromJson(req.toJson());
    CHECK(reqBack.engine == "math");
    CHECK(reqBack.operation == "solve");
    CHECK(reqBack.requestId == "req-1");

    trinity::engines::EngineResult result;
    result.success = true;
    result.engine = "math";
    result.operation = "solve";
    result.jobId = "job-1";
    result.requestId = "req-1";
    result.result = {{"y", 2}};
    result.addError(trinity::core::makeError(trinity::core::ErrorCode::TrinityError,
                                             "note", "test"));
    const auto back = trinity::engines::EngineResult::fromJson(result.toJson());
    CHECK(back.success);
    CHECK(back.jobId == "job-1");
    CHECK(back.operation == "solve");
    CHECK(back.errors.size() == 1);
}

TEST_CASE("job round-trips") {
    trinity::jobs::Job job;
    job.jobId = "j1";
    job.engine = "math";
    job.operation = "solve";
    job.status = trinity::jobs::JobStatus::Running;
    const auto back = trinity::jobs::Job::fromJson(job.toJson());
    CHECK(back.jobId == "j1");
    CHECK(trinity::jobs::toString(back.status) == "running");
    CHECK(trinity::jobs::toString(trinity::jobs::JobStatus::Completed) == "completed");
}

TEST_CASE("workflow/node/edge round-trip") {
    trinity::workflows::WorkflowNode node;
    node.id = "n1";
    node.engine = "math";
    node.operation = "solve";
    const auto nodeBack = trinity::workflows::WorkflowNode::fromJson(node.toJson());
    CHECK(nodeBack.id == "n1");
    CHECK(nodeBack.engine == "math");

    trinity::workflows::WorkflowEdge edge;
    edge.edgeId = "e1";
    edge.fromNode = "n1";
    edge.toNode = "n2";
    const auto edgeBack = trinity::workflows::WorkflowEdge::fromJson(edge.toJson());
    CHECK(edgeBack.fromNode == "n1");

    trinity::workflows::Workflow workflow;
    workflow.workflowId = "w1";
    workflow.name = "demo";
    workflow.status = trinity::workflows::WorkflowStatus::Queued;
    workflow.nodes = {node};
    workflow.edges = {edge};
    const auto wfBack = trinity::workflows::Workflow::fromJson(workflow.toJson());
    CHECK(wfBack.workflowId == "w1");
    CHECK(wfBack.nodes.size() == 1);
    CHECK(wfBack.edges.size() == 1);
    CHECK(trinity::workflows::toString(trinity::workflows::WorkflowStatus::Completed) ==
          "completed");
    CHECK(trinity::workflows::toString(trinity::workflows::NodeStatus::Failed) == "failed");
}

TEST_CASE("artifact/intent/toolcall/model round-trip") {
    trinity::artifacts::Artifact artifact;
    artifact.artifactId = "a1";
    artifact.jobId = "j1";
    artifact.type = "mesh";
    const auto aBack = trinity::artifacts::Artifact::fromJson(artifact.toJson());
    CHECK(aBack.artifactId == "a1");
    CHECK(trinity::artifacts::toString(trinity::artifacts::ArtifactType::Mesh) == "mesh");

    trinity::intelligence::ToolCall call;
    call.engine = "cad";
    call.operation = "generate";
    CHECK(trinity::intelligence::ToolCall::fromJson(call.toJson()).engine == "cad");

    trinity::intelligence::Intent intent;
    intent.domain = "cad";
    intent.operation = "generate";
    CHECK(trinity::intelligence::Intent::fromJson(intent.toJson()).domain == "cad");

    trinity::intelligence::ModelRequest req;
    req.prompt = "hello";
    req.messages = {{trinity::intelligence::MessageRole::User, "hello"}};
    const auto reqBack = trinity::intelligence::ModelRequest::fromJson(req.toJson());
    CHECK(reqBack.prompt == "hello");
    CHECK(reqBack.messages.size() == 1);

    trinity::intelligence::ModelResponse resp;
    resp.success = false;
    resp.operation = "generate";
    resp.error = trinity::core::Json{{"code", "x"}};
    const auto respBack = trinity::intelligence::ModelResponse::fromJson(resp.toJson());
    CHECK_FALSE(respBack.success);
    CHECK(respBack.operation == "generate");
}

TEST_CASE("validation result round-trips with ids") {
    trinity::validation::ValidationResult validation;
    validation.status = trinity::validation::ValidationStatus::Validated;
    validation.operation = "generate";
    validation.jobId = "j1";
    const auto back = trinity::validation::ValidationResult::fromJson(validation.toJson());
    CHECK(back.passed());
    CHECK(back.jobId == "j1");
    CHECK(back.operation == "generate");
}
