#include <doctest.h>

#include <cmath>
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
        dir = (std::filesystem::temp_directory_path() / "trinity-test-robot-wf").string();
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

trinity::workflows::Workflow baseWorkflow(const std::string& id, const std::string& name) {
    trinity::workflows::Workflow wf;
    wf.workflowId = id;
    wf.name = name;
    wf.status = trinity::workflows::WorkflowStatus::Queued;
    wf.createdAt = "2026-01-01T00:00:00+00:00";
    wf.updatedAt = wf.createdAt;
    return wf;
}

trinity::workflows::WorkflowNode makeNode(const std::string& id, const std::string& engine,
                                          const std::string& operation,
                                          trinity::core::Json params) {
    trinity::workflows::WorkflowNode n;
    n.id = id;
    n.nodeId = id;
    n.engine = engine;
    n.operation = operation;
    n.parameters = params;
    n.input = params;
    return n;
}

trinity::workflows::WorkflowEdge makeEdge(const std::string& from, const std::string& to,
                                          const std::string& id) {
    trinity::workflows::WorkflowEdge e;
    e.edgeId = id;
    e.fromNode = from;
    e.toNode = to;
    return e;
}

const double kQ1 = 0.5235987755982988;   // 30 deg
const double kQ2 = -0.2617993877991494;  // -15 deg

// create_robot -> set_joint_state -> compute_forward_kinematics ->
// inverse_kinematics (target fed from the FK pose) -> generate_trajectory.
// project_id and structured JSON flow through inputFrom at every hop.
trinity::workflows::Workflow makeRobotChain() {
    trinity::workflows::Workflow wf = baseWorkflow("wf-robotics", "robotics-chain");

    trinity::workflows::WorkflowNode a = makeNode(
        "A", "robotics", "create_robot",
        {{"link_count", 2},
         {"link_length_mm", 100.0},
         {"limit_lower_deg", -150.0},
         {"limit_upper_deg", 150.0}});

    trinity::workflows::WorkflowNode b = makeNode(
        "B", "robotics", "set_joint_state",
        {{"joint_positions", {{"joint_1", kQ1}, {"joint_2", kQ2}}}});
    b.inputFrom = trinity::core::Json{{"project_id", "{{A.project_id}}"}};

    trinity::workflows::WorkflowNode c = makeNode(
        "C", "robotics", "compute_forward_kinematics", trinity::core::Json::object());
    c.inputFrom = trinity::core::Json{{"project_id", "{{B.project_id}}"}};

    trinity::workflows::WorkflowNode d = makeNode(
        "D", "robotics", "inverse_kinematics", trinity::core::Json::object());
    d.inputFrom = trinity::core::Json{{"project_id", "{{C.project_id}}"},
                                      {"target_xyz_m", "{{C.end_effector.position}}"},
                                      {"joint_state", "{{B.joint_state}}"}};

    trinity::workflows::WorkflowNode e = makeNode(
        "E", "robotics", "generate_trajectory",
        {{"joint_start", trinity::core::Json::array({0.0, 0.0})},
         {"joint_goal", trinity::core::Json::array({kQ1, kQ2})},
         {"duration_s", 1.0},
         {"dt", 0.01}});
    e.inputFrom = trinity::core::Json{{"project_id", "{{C.project_id}}"}};

    wf.nodes = {a, b, c, d, e};
    wf.edges = {makeEdge("A", "B", "e1"), makeEdge("B", "C", "e2"),
                makeEdge("C", "D", "e3"), makeEdge("C", "E", "e4")};
    return wf;
}

}  // namespace

TEST_CASE("robotics workflow: create, state, FK, IK, trajectory chain") {
    Fixture fx;
    auto wf = makeRobotChain();
    auto result = fx.executor->runInline(wf);
    REQUIRE(result.success);
    CHECK(trinity::workflows::toString(result.workflow.status) == "completed");
    REQUIRE(result.failedNodes.empty());
    REQUIRE(result.nodeResults.size() == 5);

    // A: 2-link arm created; documented defaults reported.
    const auto& created = result.nodeResults.at("A");
    REQUIRE(created["project_id"].is_string());
    CHECK(created["project_id"].get<std::string>().empty() == false);
    CHECK(created["joint_count"] == 2);
    CHECK(created["actuated_joint_count"] == 2);
    CHECK(created["link_count"] == 3);  // base_link + 2 links
    CHECK(created["link_length_m"] == doctest::Approx(0.1));
    CHECK(created["link_length_source"] == "parameters");
    CHECK(created["model_validation"]["passed"] == true);

    // B: joint state stored on the project.
    const auto& state = result.nodeResults.at("B");
    CHECK(state["project_id"] == created["project_id"]);
    CHECK(state["joint_state_source"] == "parameters");
    CHECK(state["positions"]["joint_1"] == doctest::Approx(kQ1));
    CHECK(state["positions"]["joint_2"] == doctest::Approx(kQ2));
    CHECK(state["state_validation"]["passed"] == true);

    // C: FK used the stored state (not parameters) and returned a pose.
    const auto& fk = result.nodeResults.at("C");
    CHECK(fk["method"] == "transform_chain");
    CHECK(fk["joint_positions_source"] == "initial_state");
    CHECK(fk["project_id"] == created["project_id"]);
    const auto& ee = fk["end_effector"]["position"];
    const double ex = ee.value("x", -1.0);
    const double ey = ee.value("y", -1.0);
    const double ez = ee.value("z", -1.0);
    CHECK(std::fabs(ex) < 0.3);
    CHECK(std::fabs(ey) < 0.3);
    CHECK(std::fabs(ez) < 1e-9);
    REQUIRE(fk["frame_order"].is_array());
    CHECK(fk["frame_order"].size() >= 3);

    // D: IK got the FK pose as a structured target and converged to it.
    const auto& ik = result.nodeResults.at("D");
    CHECK(ik["method"] == "damped_least_squares");
    CHECK(ik["position_only"] == true);
    CHECK(ik["ik"]["converged"] == true);
    CHECK(ik["ik"]["within_limits"] == true);
    CHECK(ik["ik"]["final_error_m"].get<double>() <= ik["ik"]["tolerance_m"].get<double>());
    CHECK(ik["seed_source"] == "parameters");
    CHECK(ik["target_position_m"]["x"] == doctest::Approx(ex));
    CHECK(ik["target_position_m"]["y"] == doctest::Approx(ey));
    CHECK(ik["target_position_m"]["z"] == doctest::Approx(ez));
    const auto& ikPos = ik["end_effector"]["position"];
    CHECK(std::fabs(ikPos.value("x", 0.0) - ex) < 1e-4);
    CHECK(std::fabs(ikPos.value("y", 0.0) - ey) < 1e-4);

    // E: trajectory reaches the requested endpoints and ships artifacts.
    const auto& traj = result.nodeResults.at("E");
    CHECK(traj["method"] == "linear_joint_space");
    CHECK(traj["project_id"] == created["project_id"]);
    CHECK(traj["start_reached"] == true);
    CHECK(traj["goal_reached"] == true);
    CHECK(traj["sample_count"] == 101);
    CHECK(traj["duration_source"] == "parameters");
    CHECK(traj["dt_source"] == "parameters");
    REQUIRE(traj["trajectory"].is_array());
    CHECK(traj["trajectory"].size() >= 2);
    CHECK(traj["joint_order"].size() == 2);
    REQUIRE(traj["artifacts"].is_array());
    REQUIRE(traj["artifacts"].size() == 2);
    CHECK(traj["artifacts"][0]["filename"] == "trajectory.csv");
    CHECK(traj["artifacts"][0]["checksum"].get<std::string>().size() == 64);
    CHECK(traj["artifacts"][1]["filename"] == "robot.json");
}

namespace {

// Math -> Robotics chain: the link length is computed by MathEngine and
// reaches RoboticsEngine as a typed number, not a string.
trinity::workflows::Workflow makeMathToRobotChain() {
    trinity::workflows::Workflow wf = baseWorkflow("wf-math-robot", "math-to-robotics");

    trinity::workflows::WorkflowNode a =
        makeNode("A", "math", "evaluate_expression", {{"expression", "0.1 * 2"}});

    trinity::workflows::WorkflowNode b =
        makeNode("B", "robotics", "create_robot", {{"link_count", 2}});
    b.inputFrom = trinity::core::Json{{"link_length_m", "{{A.value}}"}};

    trinity::workflows::WorkflowNode c = makeNode(
        "C", "robotics", "compute_forward_kinematics", trinity::core::Json::object());
    c.inputFrom = trinity::core::Json{{"project_id", "{{B.project_id}}"}};

    wf.nodes = {a, b, c};
    wf.edges = {makeEdge("A", "B", "e1"), makeEdge("B", "C", "e2")};
    return wf;
}

}  // namespace

TEST_CASE("MathEngine feeds structured link length into RoboticsEngine workflow") {
    Fixture fx;
    auto wf = makeMathToRobotChain();
    auto result = fx.executor->runInline(wf);
    REQUIRE(result.success);
    CHECK(trinity::workflows::toString(result.workflow.status) == "completed");
    REQUIRE(result.failedNodes.empty());

    REQUIRE(result.nodeResults.count("A") == 1);
    CHECK(result.nodeResults.at("A")["value"] == doctest::Approx(0.2));

    const auto& created = result.nodeResults.at("B");
    CHECK(created["link_length_m"] == doctest::Approx(0.2));
    CHECK(created["link_length_source"] == "parameters");
    CHECK(created["joint_count"] == 2);

    // Zero configuration: two 0.2 m links along +x, so the end effector is
    // exactly one full span from the base frame origin.
    const auto& ee = result.nodeResults.at("C")["end_effector"]["position"];
    const double span = created["link_length_m"].get<double>() * 2.0;
    CHECK(ee.value("x", -1.0) == doctest::Approx(span));
    CHECK(ee.value("y", 0.0) == doctest::Approx(0.0));
    CHECK(ee.value("z", 0.0) == doctest::Approx(0.0));
}

namespace {

// A rejected joint state must fail its node and skip dependents instead of
// running FK against a state the engine refused.
trinity::workflows::Workflow makeRejectedStateChain() {
    trinity::workflows::Workflow wf = baseWorkflow("wf-robot-reject", "rejected-state");

    trinity::workflows::WorkflowNode a = makeNode(
        "A", "robotics", "create_robot",
        {{"link_count", 2}, {"link_length_mm", 100.0}});

    trinity::workflows::WorkflowNode b = makeNode(
        "B", "robotics", "set_joint_state",
        {{"joint_positions", {{"joint_9", 0.1}}}});
    b.inputFrom = trinity::core::Json{{"project_id", "{{A.project_id}}"}};

    trinity::workflows::WorkflowNode c = makeNode(
        "C", "robotics", "compute_forward_kinematics", trinity::core::Json::object());
    c.inputFrom = trinity::core::Json{{"project_id", "{{B.project_id}}"}};

    wf.nodes = {a, b, c};
    wf.edges = {makeEdge("A", "B", "e1"), makeEdge("B", "C", "e2")};
    return wf;
}

}  // namespace

TEST_CASE("rejected joint state fails node and skips dependent FK") {
    Fixture fx;
    auto wf = makeRejectedStateChain();
    auto result = fx.executor->runInline(wf);
    CHECK_FALSE(result.success);
    CHECK(trinity::workflows::toString(result.workflow.status) == "failed");
    REQUIRE(result.nodeResults.count("A") == 1);
    CHECK(result.nodeResults.count("B") == 0);
    CHECK(result.nodeResults.count("C") == 0);
    bool fkSkipped = false;
    for (const auto& n : result.workflow.nodes) {
        if (n.effectiveId() == "C") {
            fkSkipped = (n.status == trinity::workflows::NodeStatus::Skipped);
        }
    }
    CHECK(fkSkipped);
}
