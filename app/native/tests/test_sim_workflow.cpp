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

namespace {

// Spec §13 chain: MathEngine -> create_simulation -> run_simulation ->
// export_results, passing structured JSON between nodes (never display
// strings): force = 10 N computed by F = m*a feeds force_N = 10.
trinity::workflows::Workflow makeMathToSimChain() {
    trinity::workflows::Workflow wf;
    wf.workflowId = "wf-math-sim";
    wf.name = "math-to-simulation";
    wf.description = "calculate force, create, run, and export a simulation";
    wf.status = trinity::workflows::WorkflowStatus::Queued;
    wf.createdAt = "2026-01-01T00:00:00+00:00";
    wf.updatedAt = wf.createdAt;

    trinity::workflows::WorkflowNode a;
    a.id = "A";
    a.nodeId = "A";
    a.name = "Calculate force";
    a.engine = "math";
    a.operation = "formula";
    a.parameters = {{"name", "force"}, {"inputs", {{"m", 1.0}, {"a", 10.0}}}};
    a.input = a.parameters;

    trinity::workflows::WorkflowNode b;
    b.id = "B";
    b.nodeId = "B";
    b.name = "Create simulation";
    b.engine = "simulation";
    b.operation = "create_simulation";
    b.parameters = {{"model", "force_mass"},
                    {"mass_kg", 1.0},
                    {"duration_s", 5.0},
                    {"dt", 0.01}};
    b.input = b.parameters;
    // Structured feed: math result force = 10 N -> simulation input.
    b.inputFrom = {{"force_N", "{{A.outputs.F}}"}};

    trinity::workflows::WorkflowNode c;
    c.id = "C";
    c.nodeId = "C";
    c.name = "Run simulation";
    c.engine = "simulation";
    c.operation = "run_simulation";
    c.parameters = {{"write_artifacts", false}};
    c.input = c.parameters;
    c.inputFrom = {{"project", "{{B.project}}"}};

    trinity::workflows::WorkflowNode d;
    d.id = "D";
    d.nodeId = "D";
    d.name = "Export simulation results";
    d.engine = "simulation";
    d.operation = "export_results";
    d.input = trinity::core::Json::object();
    d.parameters = d.input;
    d.inputFrom = {{"result", "{{C.result}}"}, {"project", "{{C.project}}"}};

    wf.nodes = {a, b, c, d};
    trinity::workflows::WorkflowEdge e1;
    e1.edgeId = "e1";
    e1.fromNode = "A";
    e1.toNode = "B";
    trinity::workflows::WorkflowEdge e2;
    e2.edgeId = "e2";
    e2.fromNode = "B";
    e2.toNode = "C";
    trinity::workflows::WorkflowEdge e3;
    e3.edgeId = "e3";
    e3.fromNode = "C";
    e3.toNode = "D";
    wf.edges = {e1, e2, e3};
    return wf;
}

}  // namespace

TEST_CASE("MathEngine feeds structured force into SimulationEngine workflow") {
    Fixture fx;
    auto wf = makeMathToSimChain();
    auto result = fx.executor->runInline(wf);
    REQUIRE(result.success);
    CHECK(trinity::workflows::toString(result.workflow.status) == "completed");
    REQUIRE(result.failedNodes.empty());

    // A: force = m * a = 10 N (structured output, not a display string).
    REQUIRE(result.nodeResults.count("A") == 1);
    CHECK(result.nodeResults.at("A")["outputs"]["F"] == doctest::Approx(10.0));

    // B: project created with the fed force.
    REQUIRE(result.nodeResults.count("B") == 1);
    const auto& created = result.nodeResults.at("B");
    CHECK(created["valid"] == true);
    CHECK(created["project"]["force_N"]["x"] == doctest::Approx(10.0));
    CHECK(created["project"]["mass_kg"] == doctest::Approx(1.0));

    // C: run used the project object; semi-implicit Euler, v = a*t = 50.
    REQUIRE(result.nodeResults.count("C") == 1);
    const auto& run = result.nodeResults.at("C");
    CHECK(run["project"]["force_N"]["x"] == doctest::Approx(10.0));
    CHECK(run["result"]["method"] == "semi_implicit_euler");
    CHECK(run["result"]["step_count"] == 500);
    CHECK(run["result"]["final_state"]["velocity"]["x"] == doctest::Approx(50.0));
    CHECK(run["checks"]["ok"] == true);

    // D: export produced CSV/JSON with the right sample count.
    REQUIRE(result.nodeResults.count("D") == 1);
    CHECK(result.nodeResults.at("D")["sample_count"] == 501);
}
