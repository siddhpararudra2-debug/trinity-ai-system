#include <doctest.h>

#include <filesystem>
#include <iostream>
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
        dir = (std::filesystem::temp_directory_path() / "trinity-test-robotics-wf").string();
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

trinity::workflows::Workflow makeRoboticsChain() {
    trinity::workflows::Workflow wf;
    wf.workflowId = "wf-robotics-chain";
    wf.name = "robotics-chain";
    wf.description = "create robot, add joint, forward kinematics";
    wf.status = trinity::workflows::WorkflowStatus::Queued;
    wf.createdAt = "2026-01-01T00:00:00+00:00";
    wf.updatedAt = wf.createdAt;

    trinity::workflows::WorkflowNode a;
    a.id = "A";
    a.nodeId = "A";
    a.name = "Create robot";
    a.engine = "robotics";
    a.operation = "create_robot";
    a.parameters = {{"robot_name", "arm"}, {"link_count", 1}};
    a.input = a.parameters;

    trinity::workflows::WorkflowNode c;
    c.id = "B";
    c.nodeId = "B";
    c.name = "Set joint state";
    c.engine = "robotics";
    c.operation = "set_joint_state";
    c.parameters = {
        {"joint_positions", trinity::core::Json::array({1.57079632679})}
    };
    c.input = c.parameters;
    c.inputFrom = {{"project_id", "{{A.project_id}}"}};

    trinity::workflows::WorkflowNode d;
    d.id = "C";
    d.nodeId = "C";
    d.name = "Forward kinematics";
    d.engine = "robotics";
    d.operation = "compute_forward_kinematics";
    d.parameters = {{"use_project_state", true}};
    d.input = d.parameters;
    d.inputFrom = {{"project_id", "{{B.project_id}}"}};

    wf.nodes = {a, c, d};
    trinity::workflows::WorkflowEdge e1;
    e1.edgeId = "e1"; e1.fromNode = "A"; e1.toNode = "B";
    trinity::workflows::WorkflowEdge e2;
    e2.edgeId = "e2"; e2.fromNode = "B"; e2.toNode = "C";
    wf.edges = {e1, e2};
    return wf;
}

}  // namespace

TEST_CASE("RoboticsEngine executes a complete authoring workflow") {
    Fixture fx;
    auto wf = makeRoboticsChain();
    auto result = fx.executor->runInline(wf);
    if (!result.success) {
        for (const auto& [nodeId, res] : result.nodeResults) {
            std::cout << "Node " << nodeId << " result: " << res.dump(2) << std::endl;
        }
        for (const auto& node : result.failedNodes) {
            std::cout << "Failed node: " << node << std::endl;
        }
    }
    REQUIRE(result.success);
    CHECK(trinity::workflows::toString(result.workflow.status) == "completed");
    REQUIRE(result.failedNodes.empty());

    REQUIRE(result.nodeResults.count("A") == 1);
    CHECK(result.nodeResults.at("A")["robot_name"] == "arm");

    REQUIRE(result.nodeResults.count("B") == 1);
    CHECK(result.nodeResults.at("B")["positions"]["joint_1"] == doctest::Approx(1.57079632679));

    REQUIRE(result.nodeResults.count("C") == 1);
    const auto& fk = result.nodeResults.at("C");
    CHECK(fk["method"] == "transform_chain");
}
