#include <doctest.h>

#include <filesystem>
#include <memory>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/StubEngines.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/storage/Database.hpp"
#include "trinity/storage/Repositories.hpp"
#include "trinity/workflows/Executor.hpp"
#include "trinity/workflows/Workflow.hpp"

namespace {

struct Fixture {
    std::string dir;
    std::shared_ptr<trinity::storage::Database> db;
    std::shared_ptr<trinity::engines::EngineRegistry> registry;
    std::shared_ptr<trinity::artifacts::ArtifactManager> artifacts;
    std::shared_ptr<trinity::jobs::JobManager> jobs;
    std::shared_ptr<trinity::storage::WorkflowRepository> workflows;
    std::shared_ptr<trinity::workflows::WorkflowExecutor> executor;

    Fixture() {
        dir = (std::filesystem::temp_directory_path() / "trinity-test-sim-wf").string();
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir + "/artifacts");
        db = std::make_shared<trinity::storage::Database>(dir + "/trinity.db");
        db->init();
        registry = std::make_shared<trinity::engines::EngineRegistry>();
        trinity::engines::registerAllEngines(*registry);
        artifacts = std::make_shared<trinity::artifacts::ArtifactManager>(db, dir + "/artifacts");
        jobs = std::make_shared<trinity::jobs::JobManager>(db, registry, artifacts);
        workflows = std::make_shared<trinity::storage::WorkflowRepository>(db);
        executor = std::make_shared<trinity::workflows::WorkflowExecutor>(jobs, registry, workflows);
    }
    ~Fixture() {
        executor.reset();
        workflows.reset();
        jobs.reset();
        artifacts.reset();
        registry.reset();
        db.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

trinity::workflows::Workflow makeSimChain() {
    trinity::workflows::Workflow wf;
    wf.workflowId = "wf-sim";
    wf.name = "sim-chain";
    wf.description = "simulation workflow";
    wf.status = trinity::workflows::WorkflowStatus::Queued;
    wf.createdAt = "2026-01-01T00:00:00+00:00";
    wf.updatedAt = wf.createdAt;

    trinity::workflows::WorkflowNode a;
    a.id = "SIM";
    a.nodeId = "SIM";
    a.engine = "simulation";
    a.operation = "simulate_linear_motion";
    a.parameters = {{"duration_s", 1.0},
                    {"dt", 0.01},
                    {"initial_velocity_m_s", 4.0},
                    {"acceleration_m_s2", 2.0},
                    {"write_artifacts", false}};
    a.input = a.parameters;

    trinity::workflows::WorkflowNode b;
    b.id = "MATH";
    b.nodeId = "MATH";
    b.engine = "math";
    b.operation = "evaluate_expression";
    b.parameters = {{"expression", "10 * 10"}};
    b.input = b.parameters;

    wf.nodes = {a, b};
    trinity::workflows::WorkflowEdge e;
    e.edgeId = "e1";
    e.fromNode = "SIM";
    e.toNode = "MATH";
    wf.edges = {e};
    return wf;
}

}  // namespace

TEST_CASE("DAG accepts simulation engine and capabilities") {
    Fixture fx;
    auto wf = makeSimChain();
    trinity::workflows::deriveDependencies(wf);
    CHECK_NOTHROW(trinity::workflows::validateDag(wf, fx.registry.get()));
    REQUIRE(trinity::workflows::ordered(wf).size() == 2);
}

TEST_CASE("sequential workflow runs simulation then math") {
    Fixture fx;
    auto wf = makeSimChain();
    auto result = fx.executor->runInline(wf);
    CHECK(result.success);
    CHECK(trinity::workflows::toString(result.workflow.status) == "completed");
    REQUIRE(result.nodeResults.count("SIM") == 1);
    REQUIRE(result.nodeResults.count("MATH") == 1);
    const auto& sim = result.nodeResults.at("SIM");
    CHECK(sim["result"]["method"] == "closed_form");
    CHECK(sim["checks"]["ok"] == true);
    CHECK(result.nodeResults.at("MATH")["value"] == doctest::Approx(100.0));
}

TEST_CASE("failed simulation node blocks dependents") {
    Fixture fx;
    auto wf = makeSimChain();
    // Missing required duration for constant-accel is incomplete; instead
    // force an unsupported operation that fails truthfully.
    wf.nodes[0].operation = "simulate_dynamics";
    wf.nodes[0].parameters = {{"duration_s", 1.0}};
    wf.nodes[0].input = wf.nodes[0].parameters;
    auto result = fx.executor->runInline(wf);
    CHECK_FALSE(result.success);
    CHECK(trinity::workflows::toString(result.workflow.status) == "failed");
    bool mathSkipped = false;
    for (const auto& n : result.workflow.nodes) {
        if (n.effectiveId() == "MATH") {
            mathSkipped = (n.status == trinity::workflows::NodeStatus::Skipped);
        }
    }
    CHECK(mathSkipped);
}

TEST_CASE("workflow persists and reloads with simulation node") {
    Fixture fx;
    auto wf = makeSimChain();
    auto result = fx.executor->runInline(wf);
    CHECK(result.success);
    auto loaded = fx.executor->get("wf-sim");
    CHECK(loaded.name == "sim-chain");
    REQUIRE(loaded.nodes.size() == 2);
    CHECK(loaded.nodes[0].engine == "simulation");
}
