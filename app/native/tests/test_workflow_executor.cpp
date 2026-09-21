#include <doctest.h>

#include <filesystem>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/engines/CadEngine.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/MathEngine.hpp"
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
        dir = (std::filesystem::temp_directory_path() / "trinity-test-wfexec").string();
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir + "/artifacts");
        db = std::make_shared<trinity::storage::Database>(dir + "/trinity.db");
        db->init();
        registry = std::make_shared<trinity::engines::EngineRegistry>();
        registry->registerEngine(std::make_shared<trinity::engines::MathEngine>());
        registry->registerEngine(std::make_shared<trinity::engines::CadEngine>());
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

trinity::workflows::Workflow makeMathChain(bool failFirst = false) {
    trinity::workflows::Workflow wf;
    wf.workflowId = "wf-chain";
    wf.name = "chain";
    wf.description = "demo";
    wf.status = trinity::workflows::WorkflowStatus::Queued;
    wf.createdAt = "2026-01-01T00:00:00+00:00";
    wf.updatedAt = wf.createdAt;

    trinity::workflows::WorkflowNode a;
    a.id = "A";
    a.nodeId = "A";
    a.engine = "math";
    a.operation = "evaluate_expression";
    a.parameters = {{"expression", failFirst ? std::string("1/0") : std::string("25 * 8")}};
    a.input = a.parameters;

    trinity::workflows::WorkflowNode b;
    b.id = "B";
    b.nodeId = "B";
    b.engine = "math";
    b.operation = "evaluate";
    b.parameters = {{"expression", "x * 2"}};
    b.input = b.parameters;
    b.inputFrom = {{"variables", {{"x", "{{A.value}}"}}}};

    wf.nodes = {a, b};
    trinity::workflows::WorkflowEdge e;
    e.edgeId = "e1";
    e.fromNode = "A";
    e.toNode = "B";
    wf.edges = {e};
    return wf;
}

}  // namespace

TEST_CASE("edges are authoritative for dependencies") {
    trinity::workflows::Workflow wf = makeMathChain();
    // Manual deps empty; derive from edges.
    wf.nodes[1].dependencies.clear();
    trinity::workflows::deriveDependencies(wf);
    REQUIRE(wf.nodes[1].dependencies.size() == 1);
    CHECK(wf.nodes[1].dependencies[0] == "A");
}

TEST_CASE("DAG rejects cycles") {
    Fixture fx;
    trinity::workflows::Workflow wf;
    wf.workflowId = "wf-cycle";
    wf.name = "cycle";
    trinity::workflows::WorkflowNode a;
    a.id = "a";
    a.nodeId = "a";
    a.engine = "math";
    a.operation = "evaluate_expression";
    a.parameters = {{"expression", "1+1"}};
    a.input = a.parameters;
    trinity::workflows::WorkflowNode b;
    b.id = "b";
    b.nodeId = "b";
    b.engine = "math";
    b.operation = "evaluate_expression";
    b.parameters = {{"expression", "2+2"}};
    b.input = b.parameters;
    wf.nodes = {a, b};
    trinity::workflows::WorkflowEdge e1, e2;
    e1.edgeId = "e1";
    e1.fromNode = "a";
    e1.toNode = "b";
    e2.edgeId = "e2";
    e2.fromNode = "b";
    e2.toNode = "a";
    wf.edges = {e1, e2};
    CHECK_THROWS_AS(trinity::workflows::validateDag(wf), std::invalid_argument);
}

TEST_CASE("DAG rejects missing nodes and unknown engines") {
    Fixture fx;
    auto wf = makeMathChain();
    wf.edges[0].toNode = "ghost";
    CHECK_THROWS_AS(trinity::workflows::validateDag(wf), std::invalid_argument);

    auto wf2 = makeMathChain();
    wf2.nodes[0].engine = "ghost-engine";
    CHECK_THROWS_AS(trinity::workflows::validateDag(wf2, fx.registry.get()),
                    std::invalid_argument);

    auto wf3 = makeMathChain();
    wf3.nodes[0].operation = "teleport";
    CHECK_THROWS_AS(trinity::workflows::validateDag(wf3, fx.registry.get()),
                    std::invalid_argument);
}

TEST_CASE("topological ordering respects edges") {
    auto wf = makeMathChain();
    // Input order B,A but edges force A first.
    trinity::workflows::Workflow shuffled = wf;
    shuffled.nodes = {wf.nodes[1], wf.nodes[0]};
    auto order = trinity::workflows::ordered(shuffled);
    REQUIRE(order.size() == 2);
    CHECK(order[0].effectiveId() == "A");
    CHECK(order[1].effectiveId() == "B");
}

TEST_CASE("sequential workflow executes and propagates structured results") {
    Fixture fx;
    auto wf = makeMathChain(false);
    auto result = fx.executor->runInline(wf);
    CHECK(result.success);
    CHECK(trinity::workflows::toString(result.workflow.status) == "completed");
    REQUIRE(result.nodeResults.count("A") == 1);
    REQUIRE(result.nodeResults.count("B") == 1);
    CHECK(result.nodeResults.at("A")["value"] == doctest::Approx(200.0));
    // B: x * 2 with x=200 -> 400.
    CHECK(result.nodeResults.at("B")["value"] == doctest::Approx(400.0));
}

TEST_CASE("node failure blocks dependents by default") {
    Fixture fx;
    auto wf = makeMathChain(true);
    auto result = fx.executor->runInline(wf);
    CHECK_FALSE(result.success);
    CHECK(trinity::workflows::toString(result.workflow.status) == "failed");
    // B skipped because A failed and allowFailure=false.
    bool bSkipped = false;
    for (const auto& n : result.workflow.nodes) {
        if (n.effectiveId() == "B") {
            bSkipped = (n.status == trinity::workflows::NodeStatus::Skipped);
        }
    }
    CHECK(bSkipped);
}

TEST_CASE("allowFailure lets dependents continue") {
    Fixture fx;
    auto wf = makeMathChain(true);
    wf.nodes[1].allowFailure = true;
    // B no longer depends on A value; use literal so it can succeed.
    wf.nodes[1].inputFrom = trinity::core::Json::object();
    wf.nodes[1].parameters = {{"expression", "10 + 5"}, {"variables", trinity::core::Json::object()}};
    wf.nodes[1].input = wf.nodes[1].parameters;
    wf.nodes[1].operation = "evaluate_expression";
    wf.nodes[1].parameters = {{"expression", "10 + 5"}};
    wf.nodes[1].input = wf.nodes[1].parameters;
    auto result = fx.executor->runInline(wf);
    // Workflow still failed (A failed) but B completed.
    CHECK_FALSE(result.success);
    bool bCompleted = false;
    for (const auto& n : result.workflow.nodes) {
        if (n.effectiveId() == "B") {
            bCompleted = (n.status == trinity::workflows::NodeStatus::Completed);
        }
    }
    CHECK(bCompleted);
}

TEST_CASE("workflow persists and reloads") {
    Fixture fx;
    auto wf = makeMathChain(false);
    auto result = fx.executor->runInline(wf);
    CHECK(result.success);
    auto loaded = fx.executor->get("wf-chain");
    CHECK(loaded.name == "chain");
    CHECK(loaded.nodes.size() == 2);
}
